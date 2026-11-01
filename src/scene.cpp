// Turns the live desktop into surfaces for the simulation: windows, taskbar and icons.
#include "app.h"
#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include <map>

namespace {

constexpr int kCoverTolerance = 2;
constexpr int kMinWidth = 64, kMinHeight = 40;

App* hookedApp = nullptr;
HWINEVENTHOOK hooks[3]{};

struct WindowMove {
    Screen* screen;
    std::vector<Carried> cells;
    int dx, dy;
    float vx, vy, dt;
};

bool classIs(const wchar_t* name, const wchar_t* expected) { return wcscmp(name, expected) == 0; }

bool isCloaked(HWND h) {
    DWORD cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0;
}

Rect visibleFrame(HWND h) {
    RECT r{};
    if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) GetWindowRect(h, &r);
    return { r.left, r.top, r.right, r.bottom };
}

bool contains(const Rect& outer, const Rect& inner) {
    return outer.l <= inner.l + kCoverTolerance && outer.t <= inner.t + kCoverTolerance &&
        outer.r >= inner.r - kCoverTolerance && outer.b >= inner.b - kCoverTolerance;
}

bool isIgnoredClass(const wchar_t* cls) {
    return classIs(cls, kOverlayClass) || classIs(cls, kHostClass) || classIs(cls, L"Progman") || classIs(cls, L"WorkerW") ||
        classIs(cls, L"#32768") || classIs(cls, L"tooltips_class32");
}

// Click-through and fully transparent windows are invisible to the user, so they are not surfaces.
bool isSeeThrough(HWND h) {
    LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if (ex & WS_EX_TRANSPARENT) return true;
    if (ex & WS_EX_LAYERED) {
        BYTE alpha = 255;
        DWORD flags = 0;
        if (GetLayeredWindowAttributes(h, nullptr, &alpha, &flags) && (flags & LWA_ALPHA) && alpha == 0) return true;
    }
    return false;
}

void markCoveredScreens(App& app, HWND h, const Rect& frame) {
    RECT raw{};
    GetWindowRect(h, &raw);
    Rect outer{ raw.left, raw.top, raw.right, raw.bottom };
    bool zoomed = IsZoomed(h) != FALSE;

    for (auto& screen : app.screens) {
        const Rect& s = screen->bounds;
        int overlapW = std::min(frame.r, s.r) - std::max(frame.l, s.l);
        int overlapH = std::min(frame.b, s.b) - std::max(frame.t, s.t);
        if (overlapW <= 0 || overlapH <= 0) continue;
        bool mostlyHere = int64_t(overlapW) * overlapH * 2 > int64_t(s.w()) * s.h();
        bool fullscreen = contains(frame, s) || contains(outer, s);
        bool maximized = contains(frame, screen->work) || (zoomed && mostlyHere);
        // With flakes in front of windows, only a real fullscreen (a game, a video) still stops the snow.
        if (app.settings.flakesInFront ? (fullscreen && !zoomed) : (fullscreen || maximized)) screen->covered = true;
    }
}

BOOL CALLBACK onWindow(HWND h, LPARAM param) {
    App& app = *reinterpret_cast<App*>(param);
    if (!IsWindowVisible(h) || IsIconic(h)) return TRUE;

    wchar_t cls[96]{};
    GetClassNameW(h, cls, 96);
    if (isIgnoredClass(cls) || isCloaked(h) || isSeeThrough(h)) return TRUE;

    Rect frame = visibleFrame(h);
    bool taskbar = classIs(cls, L"Shell_TrayWnd") || classIs(cls, L"Shell_SecondaryTrayWnd");
    if (taskbar ? (frame.w() < 2 || frame.h() < 2) : (frame.w() < kMinWidth || frame.h() < kMinHeight)) return TRUE;

    if (!taskbar) markCoveredScreens(app, h, frame);
    bool rounded = !taskbar && !IsZoomed(h);
    app.windows.push_back({ h, frame, uint8_t(taskbar ? S_GROUND : S_WINDOW), rounded });
    return TRUE;
}

// Snow resting on a window comes along with it; collectMove picks it up before the surface changes.
void collectMove(App& app, const Rect& before, const ScannedWindow& w, float dt, std::vector<WindowMove>& moves) {
    int dx = before.w() == w.rc.w() ? w.rc.l - before.l : 0;
    int dy = w.rc.t - before.t;
    if ((!dx && !dy) || w.kind != S_WINDOW) return;

    for (auto& screen : app.screens) {
        if (screen->covered) continue;
        Rect local = screen->toLocal(before);
        if (local.r <= 0 || local.l >= screen->bounds.w() || local.b <= 0 || local.t >= screen->bounds.h()) continue;
        WindowMove m;
        m.screen = screen.get();
        m.dx = dx;
        m.dy = dy;
        m.dt = std::min(dt, 0.1f);
        float d = std::max(dt, 1 / 240.0f);
        m.vx = std::clamp(dx / d, -6000.0f, 6000.0f);
        m.vy = std::clamp(dy / d, -6000.0f, 6000.0f);
        screen->world.liftAttached(local, m.cells);
        screen->playground.windowMoved(local, dx, dy);
        moves.push_back(std::move(m));
    }
}

bool isDesktopIconList(HWND h) {
    wchar_t cls[32]{}, parent[32]{};
    GetClassNameW(h, cls, 32);
    GetClassNameW(GetParent(h), parent, 32);
    return classIs(cls, L"SysListView32") && classIs(parent, L"SHELLDLL_DefView");
}

void CALLBACK onWinEvent(HWINEVENTHOOK, DWORD event, HWND h, LONG object, LONG child, DWORD, DWORD) {
    if (!hookedApp || !h) return;
    if ((event == EVENT_SYSTEM_CAPTURESTART || event == EVENT_SYSTEM_CAPTUREEND) && isDesktopIconList(h)) {
        hookedApp->desktopPressed = event == EVENT_SYSTEM_CAPTURESTART;
        if (hookedApp->log) fprintf(hookedApp->log, "desktop list %s the mouse\n", hookedApp->desktopPressed ? "took" : "released");
    }
    if (event == EVENT_SYSTEM_DRAGDROPEND) hookedApp->iconWatcher.poke();
    if (object == OBJID_WINDOW && child == CHILDID_SELF) hookedApp->windowsDirty = true;
}

}

void installWindowHooks(App& app) {
    hookedApp = &app;
    DWORD flags = WINEVENT_OUTOFCONTEXT;
    if (!app.options.testWindow) flags |= WINEVENT_SKIPOWNPROCESS;
    hooks[0] = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, onWinEvent, 0, 0, flags);
    hooks[1] = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_HIDE, nullptr, onWinEvent, 0, 0, flags);
    hooks[2] = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_MINIMIZEEND, nullptr, onWinEvent, 0, 0, flags);
}

void removeWindowHooks() {
    for (HWINEVENTHOOK& h : hooks) {
        if (h) UnhookWinEvent(h);
        h = nullptr;
    }
    hookedApp = nullptr;
}

void rebuildSolids(App& app, Screen& screen) {
    std::vector<SolidRect> rects;
    rects.reserve(app.windows.size());
    for (const ScannedWindow& w : app.windows) rects.push_back({ screen.toLocal(w.rc), w.kind, w.rounded });

    std::vector<IconShape> shapes;
    if (app.icons) {
        for (const DesktopIcon& ic : *app.icons) shapes.push_back({ screen.toLocal(ic.rc), ic.w, ic.h, ic.mask });
    }
    screen.world.setSolids(rects, shapes);
    screen.solidsDirty = false;
}

void scanDesktop(App& app, double now) {
    float sinceLast = std::min(float(now - app.lastScan), 0.5f);
    app.lastScan = now;
    app.windowsDirty = false;

    for (auto& screen : app.screens) screen->covered = false;
    app.windows.clear();
    EnumWindows(onWindow, reinterpret_cast<LPARAM>(&app));
    if (app.options.ignoreFullscreen) {
        for (auto& screen : app.screens) screen->covered = false;
    }

    std::vector<WindowMove> moves;
    std::unordered_map<HWND, Rect> rects;
    bool changed = false;
    for (const ScannedWindow& w : app.windows) {
        auto it = app.lastRects.find(w.hwnd);
        if (it == app.lastRects.end()) {
            changed = true;
        } else if (!(it->second == w.rc)) {
            changed = true;
            collectMove(app, it->second, w, sinceLast, moves);
        }
        rects[w.hwnd] = w.rc;
    }
    changed = changed || rects.size() != app.lastRects.size();
    app.lastRects.swap(rects);

    if (changed) {
        for (auto& screen : app.screens) screen->solidsDirty = true;
    }
    for (auto& screen : app.screens) {
        if (screen->solidsDirty) rebuildSolids(app, *screen);
    }
    for (WindowMove& m : moves) m.screen->world.placeCarried(m.cells, m.dx, m.dy, m.vx, m.vy, m.dt);
    if (!moves.empty()) app.draggingUntil = now + 0.12;
}

// An icon that moved leaves its snow behind, flung against the direction of travel.
void applyIconChanges(App& app) {
    unsigned version = app.iconWatcher.version();
    if (version == app.iconVersion) return;
    app.iconVersion = version;
    auto fresh = app.iconWatcher.latest();

    if (app.icons && fresh) {
        std::map<std::wstring, Rect> before;
        for (const DesktopIcon& ic : *app.icons) before[ic.key] = ic.rc;
        for (const DesktopIcon& ic : *fresh) {
            auto it = before.find(ic.key);
            if (it == before.end() || it->second == ic.rc) continue;
            float dx = float(ic.rc.l - it->second.l), dy = float(ic.rc.t - it->second.t);
            float length = std::max(std::sqrt(dx * dx + dy * dy), 1.0f);
            for (auto& screen : app.screens) {
                Rect above = screen->toLocal(it->second);
                above.t -= 24;
                screen->world.releaseRegion(above, -dx / length * 150, -dy / length * 90);
            }
        }
    }
    app.icons = fresh;
    for (auto& screen : app.screens) screen->solidsDirty = true;
}
