// Follows the mouse: its shape, the snow it pushes around, the snow it collects, and icon drags.
#include <algorithm>
#include <cmath>
#include "app.h"

namespace {

constexpr float kDragStartDistance = 3;

// Drawing the cursor on black and on white reveals every pixel it touches, inverting ones included.
void loadCursorShape(SnowCursor& cursor, HCURSOR handle) {
    ICONINFO info{};
    if (!GetIconInfo(handle, &info)) return;
    BITMAP bm{};
    GetObjectW(info.hbmColor ? info.hbmColor : info.hbmMask, sizeof(bm), &bm);
    int w = bm.bmWidth, h = info.hbmColor ? bm.bmHeight : bm.bmHeight / 2;
    int hotX = info.xHotspot, hotY = info.yHotspot;
    if (info.hbmColor) DeleteObject(info.hbmColor);
    if (info.hbmMask) DeleteObject(info.hbmMask);
    if (w <= 0 || h <= 0) return;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC dc = CreateCompatibleDC(nullptr);
    void* bits[2]{};
    HBITMAP bitmaps[2]{};
    for (int pass = 0; pass < 2; pass++) {
        bitmaps[pass] = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits[pass], nullptr, 0);
        if (!bitmaps[pass]) continue;
        HGDIOBJ old = SelectObject(dc, bitmaps[pass]);
        RECT r{ 0, 0, w, h };
        FillRect(dc, &r, (HBRUSH)GetStockObject(pass ? WHITE_BRUSH : BLACK_BRUSH));
        DrawIconEx(dc, 0, 0, handle, w, h, 0, nullptr, DI_NORMAL);
        SelectObject(dc, old);
    }

    std::vector<uint8_t> mask(size_t(w) * h, 0);
    if (bits[0] && bits[1]) {
        GdiFlush();
        const uint32_t* onBlack = static_cast<const uint32_t*>(bits[0]);
        const uint32_t* onWhite = static_cast<const uint32_t*>(bits[1]);
        for (size_t i = 0; i < mask.size(); i++) {
            bool untouched = (onBlack[i] & 0xFFFFFF) == 0 && (onWhite[i] & 0xFFFFFF) == 0xFFFFFF;
            mask[i] = untouched ? 0 : 1;
        }
    }
    for (HBITMAP b : bitmaps) {
        if (b) DeleteObject(b);
    }
    DeleteDC(dc);
    cursor.setShape(mask.data(), w, h, hotX, hotY);
}

// Moving shakes the snow off: part of it each frame, left hanging behind the pointer as it falls.
void shakeOffSnow(SnowCursor& c, World& world, uint32_t seed) {
    float ox = c.originX(), oy = c.originY();
    uint32_t r = seed | 1;
    for (int i = 0; i < SnowCursor::S * SnowCursor::S; i++) {
        if (!c.snow[i]) continue;
        r ^= r << 13;
        r ^= r >> 17;
        r ^= r << 5;
        if (r & 1) continue;
        c.snow[i] = 0;
        c.snowCount--;
        world.spawnGrain(ox + i % SnowCursor::S, oy + i / SnowCursor::S,
            -c.vx * 0.3f + float(int(r >> 8 & 127) - 64),
            -c.vy * 0.2f - float(r >> 16 & 63));
    }
}

int iconUnder(const App& app, const Screen& screen, float x, float y) {
    if (!app.icons || screen.world.solidAt(int(x), int(y)) != S_ICON) return -1;
    int index = 0;
    for (const DesktopIcon& ic : *app.icons) {
        Rect r = screen.toLocal(ic.rc);
        if (x >= r.l && x < r.r && y >= r.t && y < r.b) return index;
        index++;
    }
    return -1;
}

// The real icon only moves when it is dropped, so the start of a drag has to be seen from the mouse. The list lets
// go of the mouse as soon as a drag begins, so a release after some movement counts too.
void followIconPress(App& app, Screen& screen, bool down) {
    IconPress& press = app.press;
    SnowCursor& c = app.cursor;
    if (down && !press.down) {
        press.x = c.x;
        press.y = c.y;
        press.thrown = false;
        press.icon = iconUnder(app, screen, c.x, c.y);
    }
    if ((down || press.down) && press.icon >= 0 && !press.thrown && app.icons && size_t(press.icon) < app.icons->size()) {
        float dx = c.x - press.x, dy = c.y - press.y;
        if (dx * dx + dy * dy > kDragStartDistance * kDragStartDistance) {
            Rect above = screen.toLocal((*app.icons)[press.icon].rc);
            above.t -= 24;
            size_t before = screen.world.particleCount();
            screen.world.releaseRegion(above, -c.vx * 0.3f, -c.vy * 0.2f);
            if (app.log) fprintf(app.log, "icon drag: %zu cells thrown\n", screen.world.particleCount() - before);
            press.thrown = true;
        }
    }
    if (!down && press.down && press.icon >= 0) app.iconWatcher.poke();     // dropped: it may have moved
    press.down = down;
}

}

PointerStep trackPointer(App& app, float dt, double now) {
    PointerStep step;
    SnowCursor& c = app.cursor;
    CURSORINFO info{ sizeof(info) };
    if (!GetCursorInfo(&info)) return step;

    float px = float(info.ptScreenPos.x), py = float(info.ptScreenPos.y);
    bool pressed = app.desktopPressed;
    if (app.options.scriptedPointer) scriptedPointerPosition(now, px, py, pressed);

    c.visible = (info.flags & CURSOR_SHOWING) != 0;
    if (info.hCursor && info.hCursor != app.cursorHandle) {
        app.cursorHandle = info.hCursor;
        loadCursorShape(c, info.hCursor);
    }

    for (auto& screen : app.screens) {
        const Rect& b = screen->bounds;
        if (px >= b.l && px < b.r && py >= b.t && py < b.b) step.screen = screen.get();
    }
    if (!step.screen) {
        c.visible = false;
        return step;
    }
    Screen& screen = *step.screen;

    if (!app.hasLastPosition) {
        app.lastX = px;
        app.lastY = py;
        app.hasLastPosition = true;
    }
    float d = std::max(dt, 1 / 240.0f);
    float vx = (px - app.lastX) / d, vy = (py - app.lastY) / d;
    float fromX = app.lastX - screen.bounds.l, fromY = app.lastY - screen.bounds.t;
    bool moved = px != app.lastX || py != app.lastY;
    app.lastX = px;
    app.lastY = py;

    c.x = px - screen.bounds.l;
    c.y = py - screen.bounds.t;
    float keep = std::exp(-dt / 0.02f);
    c.vx = c.vx * keep + vx * (1 - keep);
    c.vy = c.vy * keep + vy * (1 - keep);
    if (!moved) {
        c.vx *= keep;
        c.vy *= keep;
    }
    c.idle = moved ? 0 : c.idle + dt;

    if (moved && c.visible && app.settings.mousePush) screen.world.plow(fromX, fromY, c.x, c.y, vx, vy);
    if (moved && c.snowCount > 0) shakeOffSnow(c, screen.world, uint32_t(now * 977));
    if (moved) c.resetFrost();
    c.overWindow = !app.settings.flakesInFront && screen.world.windowAt(c.x, c.y);
    if (c.overWindow) {
        // A window opened under a still pointer: its snow goes, and none gathers while it stays there.
        if (c.snowCount > 0) c.clear();
        c.resetFrost();
    } else if (c.visible && app.settings.cursorSnow && app.settings.settle) {
        c.growFrost(dt);
    }
    c.ease(dt);

    followIconPress(app, screen, pressed);
    return step;
}
