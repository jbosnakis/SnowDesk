// The settings panel: a small window drawn with Direct2D. Every change applies at once.
#include "app.h"
#include <d2d1.h>
#include <dwmapi.h>
#include <dwrite.h>
#include <shellscalingapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include "resource.h"

namespace {

const wchar_t kPanelClass[] = L"SnowDeskPanel";

constexpr float kMargin = 18, kGap = 26, kColumn = 306;
constexpr float kPanelWidth = kMargin * 2 + kColumn * 2 + kGap;
constexpr float kHeadingHeight = 34, kToggleHeight = 34, kSliderHeight = 46, kButtonHeight = 40, kFooterHeight = 38;
constexpr float kSwitchWidth = 38, kSwitchHeight = 20;
constexpr UINT_PTR kAnimationTimer = 1;

// Colors follow the snow: deep night behind, ice blue for what is on.
const D2D1_COLOR_F kBack = D2D1::ColorF(0x10161F);
const D2D1_COLOR_F kText = D2D1::ColorF(0xE9EFF7);
const D2D1_COLOR_F kMuted = D2D1::ColorF(0x8C9AAD);
const D2D1_COLOR_F kLine = D2D1::ColorF(0x263242);
const D2D1_COLOR_F kOff = D2D1::ColorF(0x35435A);
const D2D1_COLOR_F kAccent = D2D1::ColorF(0x8FD3FF);
const D2D1_COLOR_F kAccentDark = D2D1::ColorF(0x0D2233);
const D2D1_COLOR_F kButton = D2D1::ColorF(0x1D2736);
const D2D1_COLOR_F kButtonHover = D2D1::ColorF(0x2A384C);

using Flag = bool Settings::*;
using Number = int Settings::*;
using Rule = std::function<bool(const Settings&)>;

enum class Kind { Heading, Toggle, Slider, Button };

struct Item {
    Kind kind = Kind::Heading;
    std::wstring label;
    std::wstring hint;
    std::function<bool(const App&)> read;
    std::function<void(App&, bool)> write;
    Number number = nullptr;
    int low = 0, high = 100;
    std::function<void(App&)> press;
    Rule enabledWhen;
    std::wstring whyOff;
    D2D1_RECT_F box{};
    float knob = 0;     // where the switch knob is drawn, 0..1: it glides to its place
};

struct Panel {
    App* app = nullptr;
    HWND hwnd = nullptr;
    ComPtr<ID2D1Factory> d2d;
    ComPtr<IDWriteFactory> write;
    ComPtr<ID2D1HwndRenderTarget> target;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<IDWriteTextFormat> bodyText, headingText, valueText, hintText, buttonText;
    std::vector<Item> items;
    float height = 0;
    int hover = -1, pressed = -1, focus = -1;
    bool dragging = false;
    bool shiftDown = false;
};
Panel panel;

bool usable(const Item& item, const App& app) {
    return item.kind != Kind::Heading && (!item.enabledWhen || item.enabledWhen(app.settings));
}

void addHeading(std::vector<Item>& list, const wchar_t* label) {
    Item item;
    item.kind = Kind::Heading;
    item.label = label;
    list.push_back(item);
}

Item& addToggle(std::vector<Item>& list, const wchar_t* label, const wchar_t* hint, Flag flag, bool inverted = false) {
    Item item;
    item.kind = Kind::Toggle;
    item.label = label;
    item.hint = hint;
    item.read = [flag, inverted](const App& a) { return (a.settings.*flag) != inverted; };
    item.write = [flag, inverted](App& a, bool on) { a.settings.*flag = on != inverted; };
    list.push_back(item);
    return list.back();
}

Item& addSlider(std::vector<Item>& list, const wchar_t* label, const wchar_t* hint, Number number, int low, int high) {
    Item item;
    item.kind = Kind::Slider;
    item.label = label;
    item.hint = hint;
    item.number = number;
    item.low = low;
    item.high = high;
    list.push_back(item);
    return list.back();
}

Item& addButton(std::vector<Item>& list, const wchar_t* label, const wchar_t* hint, std::function<void(App&)> press) {
    Item item;
    item.kind = Kind::Button;
    item.label = label;
    item.hint = hint;
    item.press = std::move(press);
    list.push_back(item);
    return list.back();
}

void requestScene(App& app, PlayScene scene) {
    for (auto& screen : app.screens) screen->playground.request(scene);
}

float layoutColumn(std::vector<Item>& list, size_t from, size_t to, float x) {
    float y = 8;
    for (size_t i = from; i < to; i++) {
        Item& item = list[i];
        bool half = item.kind == Kind::Button && i + 1 < to && list[i + 1].kind == Kind::Button;
        float h = item.kind == Kind::Heading ? kHeadingHeight : item.kind == Kind::Toggle ? kToggleHeight
            : item.kind == Kind::Slider ? kSliderHeight : kButtonHeight;
        if (half) {
            float w = (kColumn - 8) / 2;
            item.box = D2D1::RectF(x, y, x + w, y + h - 6);
            list[i + 1].box = D2D1::RectF(x + w + 8, y, x + kColumn, y + h - 6);
            i++;
        } else if (item.kind == Kind::Button) {
            item.box = D2D1::RectF(x, y, x + kColumn, y + h - 6);
        } else {
            item.box = D2D1::RectF(x, y, x + kColumn, y + h);
        }
        y += h;
    }
    return y;
}

void buildItems() {
    std::vector<Item>& list = panel.items;
    list.clear();
    const std::wstring whenSettling = L"Needs snow that settles, so not in Low CPU mode or Snowfall only";
    Rule settles = [](const Settings& s) { return s.settle && !s.lowCpu; };
    Rule notLowCpu = [](const Settings& s) { return !s.lowCpu; };
    const std::wstring offInLowCpu = L"Not used in Low CPU mode";

    addHeading(list, L"SNOWFALL");
    addToggle(list, L"Snowing", L"Pause or resume the snow", &Settings::enabled);
    addSlider(list, L"Intensity", L"How many flakes fall at once", &Settings::density, 20, 300);
    addSlider(list, L"Flake size", L"From fine powder to big fat flakes", &Settings::flakeSize, 50, 250);
    addSlider(list, L"Fall speed", L"How fast the flakes fall", &Settings::fallSpeed, 40, 250);
    addSlider(list, L"Opacity", L"How strongly the snow shows", &Settings::opacity, 30, 100);
    addToggle(list, L"Flakes in front of windows too",
        L"Snows over everything, even a maximized window. Games and videos stay clear", &Settings::flakesInFront);

    addHeading(list, L"WHERE SNOW SETTLES");
    Item& onlyFalling = addToggle(list, L"Snowfall only, nothing settles", L"Flakes fall and vanish; snow already lying is cleared too", &Settings::settle, true);
    onlyFalling.enabledWhen = notLowCpu;
    onlyFalling.whyOff = offInLowCpu;
    Item& unlimited = addToggle(list, L"No limit, let it fill the screen", L"Drifts grow very tall, to about the middle of the screen", &Settings::unlimited);
    unlimited.enabledWhen = settles;
    unlimited.whyOff = whenSettling;
    for (auto [label, flag] : { std::pair<const wchar_t*, Flag>{ L"On icons", &Settings::onIcons },
                                { L"On windows", &Settings::onWindows },
                                { L"On the taskbar", &Settings::onTaskbar } }) {
        Item& where = addToggle(list, label, L"Turn it off and the snow there slowly melts", flag);
        where.enabledWhen = settles;
        where.whyOff = whenSettling;
    }
    Item& speed = addSlider(list, L"Settling speed", L"Flakes melt on contact at first, then snow slowly takes hold and builds up", &Settings::settleSpeed, 25, 400);
    speed.enabledWhen = settles;
    speed.whyOff = whenSettling;
    size_t rightColumn = list.size();

    addHeading(list, L"WEATHER");
    Item& wind = addToggle(list, L"Wind", L"Gusts to the left and right, stronger at night", &Settings::wind);
    wind.enabledWhen = notLowCpu;
    wind.whyOff = offInLowCpu;
    Item& strength = addSlider(list, L"Wind strength", L"How hard it blows", &Settings::windStrength, 30, 200);
    strength.enabledWhen = [](const Settings& s) { return s.wind && !s.lowCpu; };
    strength.whyOff = L"Only available while wind is on";
    Item& sun = addToggle(list, L"Sun melts snow during the day", L"From 7:30 to 17:30, in patches that move with the hour", &Settings::sun);
    sun.enabledWhen = settles;
    sun.whyOff = whenSettling;
    Item& storm = addButton(list, L"Snowstorm now", L"A minute of heavy snow, with gusts if wind is on", [](App& a) { a.weather.startStorm(60); });
    storm.enabledWhen = notLowCpu;
    storm.whyOff = offInLowCpu;

    addHeading(list, L"CHILDREN AND MOUSE");
    Item& kids = addToggle(list, L"Show children", L"They come out to play wherever there is snow and room", &Settings::kids);
    kids.enabledWhen = settles;
    kids.whyOff = whenSettling;
    Rule canPlay = [](const Settings& s) { return s.settle && s.kids && s.enabled && !s.lowCpu; };
    const std::wstring playOff = L"Needs children on and snow settling";
    Item& snowman = addButton(list, L"Snowman", L"They come out as soon as they find snow and room", [](App& a) { requestScene(a, PlayScene::Snowman); });
    snowman.enabledWhen = canPlay;
    snowman.whyOff = playOff;
    Item& fight = addButton(list, L"Snowball fight", L"They come out as soon as they find snow and room", [](App& a) { requestScene(a, PlayScene::Fight); });
    fight.enabledWhen = canPlay;
    fight.whyOff = playOff;
    Item& push = addToggle(list, L"Mouse pushes snow", L"Passing over snow throws it aside", &Settings::mousePush);
    push.enabledWhen = settles;
    push.whyOff = whenSettling;
    Item& onCursor = addToggle(list, L"Snow on the mouse pointer", L"It collects snow while it stays still", &Settings::cursorSnow);
    onCursor.enabledWhen = settles;
    onCursor.whyOff = whenSettling;

    addHeading(list, L"SYSTEM");
    addToggle(list, L"Low CPU mode", L"Only soft, steady snowfall: no wind, no children, nothing settles", &Settings::lowCpu);
#ifndef SNOW_NO_AUTOSTART
    Item autostart;
    autostart.kind = Kind::Toggle;
    autostart.label = L"Start with Windows";
    autostart.hint = L"Starts by itself when you sign in";
    autostart.read = [](const App&) { return autostartEnabled(); };
    autostart.write = [](App&, bool on) { setAutostart(on); };
    list.push_back(autostart);
#endif
    Item& saving = addToggle(list, L"Power saving", L"60 frames per second instead of 120", &Settings::saveEnergy);
    saving.enabledWhen = notLowCpu;
    saving.whyOff = L"Low CPU mode already runs at 60 frames per second";
    addButton(list, L"Clear snow", L"Removes all the snow from the screen", [](App& a) { clearAllSnow(a); });
    addButton(list, L"Defaults", L"Puts every setting back as it was", [](App& a) { a.settings = Settings{}; });

    float left = layoutColumn(list, 0, rightColumn, kMargin);
    float right = layoutColumn(list, rightColumn, list.size(), kMargin + kColumn + kGap);
    panel.height = std::max(left, right) + kFooterHeight + 6;

    for (Item& item : list) {
        if (item.kind == Kind::Toggle) item.knob = item.read(*panel.app) ? 1.0f : 0.0f;
    }
}

void changed(const Settings& before) {
    applySettings(*panel.app, before);
    InvalidateRect(panel.hwnd, nullptr, FALSE);
}

void setSliderFromMouse(Item& item, float x) {
    App& app = *panel.app;
    float t = std::clamp((x - item.box.left) / (item.box.right - item.box.left), 0.0f, 1.0f);
    int value = item.low + int(std::lround((item.high - item.low) * t / 5)) * 5;
    value = std::clamp(value, item.low, item.high);
    if (app.settings.*item.number == value) return;
    Settings before = app.settings;
    app.settings.*item.number = value;
    changed(before);
}

void activate(Item& item) {
    App& app = *panel.app;
    Settings before = app.settings;
    if (item.kind == Kind::Toggle) item.write(app, !item.read(app));
    else if (item.kind == Kind::Button) item.press(app);
    changed(before);
    SetTimer(panel.hwnd, kAnimationTimer, 15, nullptr);
}

int itemAt(float x, float y) {
    for (size_t i = 0; i < panel.items.size(); i++) {
        const Item& item = panel.items[i];
        if (item.kind == Kind::Heading) continue;
        const D2D1_RECT_F& b = item.box;
        if (x >= b.left && x < b.right && y >= b.top && y < b.bottom) return int(i);
    }
    return -1;
}

void moveFocus(int direction) {
    int count = int(panel.items.size());
    int at = panel.focus;
    for (int step = 0; step < count; step++) {
        at = (at + direction + count) % count;
        if (usable(panel.items[at], *panel.app)) {
            panel.focus = at;
            break;
        }
    }
    InvalidateRect(panel.hwnd, nullptr, FALSE);
}

void text(ID2D1RenderTarget* rt, const std::wstring& s, IDWriteTextFormat* format, const D2D1_RECT_F& box, const D2D1_COLOR_F& color) {
    panel.brush->SetColor(color);
    rt->DrawTextW(s.c_str(), UINT32(s.size()), format, box, panel.brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

D2D1_COLOR_F mix(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b, float t) {
    return D2D1::ColorF(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t);
}

void drawToggle(ID2D1RenderTarget* rt, const Item& item, float alpha) {
    const D2D1_RECT_F& b = item.box;
    float mid = (b.top + b.bottom) / 2;
    text(rt, item.label, panel.bodyText.Get(), D2D1::RectF(b.left, b.top, b.right - kSwitchWidth - 10, b.bottom), mix(kBack, kText, alpha));

    D2D1_ROUNDED_RECT pill{ D2D1::RectF(b.right - kSwitchWidth, mid - kSwitchHeight / 2, b.right, mid + kSwitchHeight / 2), kSwitchHeight / 2, kSwitchHeight / 2 };
    panel.brush->SetColor(mix(kBack, mix(kOff, kAccent, item.knob), alpha));
    rt->FillRoundedRectangle(pill, panel.brush.Get());
    float knobX = pill.rect.left + kSwitchHeight / 2 + (kSwitchWidth - kSwitchHeight) * item.knob;
    panel.brush->SetColor(mix(kBack, mix(kText, kAccentDark, item.knob), alpha));
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knobX, mid), 7.5f, 7.5f), panel.brush.Get());
}

void drawSlider(ID2D1RenderTarget* rt, const Item& item, float alpha, bool active) {
    const App& app = *panel.app;
    const D2D1_RECT_F& b = item.box;
    int value = app.settings.*item.number;
    float t = float(value - item.low) / float(item.high - item.low);
    text(rt, item.label, panel.bodyText.Get(), D2D1::RectF(b.left, b.top + 2, b.right - 60, b.top + 24), mix(kBack, kText, alpha));
    text(rt, std::to_wstring(value) + L"%", panel.valueText.Get(), D2D1::RectF(b.right - 60, b.top + 2, b.right, b.top + 24), mix(kBack, active ? kAccent : kMuted, alpha));

    float y = b.top + 32, x = b.left + (b.right - b.left) * t;
    panel.brush->SetColor(mix(kBack, kOff, alpha));
    rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(b.left, y - 2, b.right, y + 2), 2, 2), panel.brush.Get());
    panel.brush->SetColor(mix(kBack, kAccent, alpha));
    rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(b.left, y - 2, x, y + 2), 2, 2), panel.brush.Get());
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(std::clamp(x, b.left + 7, b.right - 7), y), active ? 8.0f : 7.0f, active ? 8.0f : 7.0f), panel.brush.Get());
}

void drawButton(ID2D1RenderTarget* rt, const Item& item, float alpha, bool active) {
    panel.brush->SetColor(mix(kBack, active ? kButtonHover : kButton, alpha));
    rt->FillRoundedRectangle(D2D1::RoundedRect(item.box, 7, 7), panel.brush.Get());
    text(rt, item.label, panel.buttonText.Get(), item.box, mix(kBack, kText, alpha));
}

void createTarget() {
    RECT rc;
    GetClientRect(panel.hwnd, &rc);
    float dpi = float(GetDpiForWindow(panel.hwnd));
    auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), dpi, dpi);
    auto hwndProps = D2D1::HwndRenderTargetProperties(panel.hwnd, D2D1::SizeU(rc.right, rc.bottom));
    panel.d2d->CreateHwndRenderTarget(props, hwndProps, &panel.target);
    if (panel.target) panel.target->CreateSolidColorBrush(kText, &panel.brush);
}

void paint() {
    if (!panel.target) createTarget();
    if (!panel.target) return;
    ID2D1RenderTarget* rt = panel.target.Get();
    const App& app = *panel.app;

    rt->BeginDraw();
    rt->Clear(kBack);
    for (size_t i = 0; i < panel.items.size(); i++) {
        const Item& item = panel.items[i];
        bool on = usable(item, app);
        float alpha = item.kind == Kind::Heading || on ? 1.0f : 0.38f;
        bool active = on && (int(i) == panel.hover || int(i) == panel.focus);
        switch (item.kind) {
        case Kind::Heading:
            text(rt, item.label, panel.headingText.Get(), D2D1::RectF(item.box.left, item.box.top + 8, item.box.right, item.box.bottom), kAccent);
            break;
        case Kind::Toggle: drawToggle(rt, item, alpha); break;
        case Kind::Slider: drawSlider(rt, item, alpha, active); break;
        case Kind::Button: drawButton(rt, item, alpha, active); break;
        }
        if (int(i) == panel.focus) {
            panel.brush->SetColor(kAccent);
            D2D1_RECT_F b = item.box;
            rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(b.left - 4, b.top, b.right + 4, b.bottom), 6, 6), panel.brush.Get(), 1.2f);
        }
    }

    // The footer explains what the pointer is on, or why it cannot be used right now.
    float footer = panel.height - kFooterHeight;
    panel.brush->SetColor(kLine);
    rt->DrawLine(D2D1::Point2F(kMargin, footer), D2D1::Point2F(kPanelWidth - kMargin, footer), panel.brush.Get(), 1);
    int shown = panel.hover >= 0 ? panel.hover : panel.focus;
    std::wstring hint = L"Changes apply at once and are saved automatically";
    if (shown >= 0) {
        const Item& item = panel.items[shown];
        hint = usable(item, app) || item.whyOff.empty() ? item.hint : item.whyOff;
    }
    text(rt, hint, panel.hintText.Get(), D2D1::RectF(kMargin, footer + 1, kPanelWidth - kMargin, panel.height - 2), kMuted);

    if (rt->EndDraw() == D2DERR_RECREATE_TARGET) {
        panel.brush.Reset();
        panel.target.Reset();
    }
}

// Moves switch knobs a little closer to where they belong; true while any is still on its way.
bool stepKnobs() {
    bool moving = false;
    for (Item& item : panel.items) {
        if (item.kind != Kind::Toggle) continue;
        float target = item.read(*panel.app) ? 1.0f : 0.0f;
        item.knob += (target - item.knob) * 0.3f;
        if (std::fabs(target - item.knob) < 0.01f) item.knob = target;
        else moving = true;
    }
    return moving;
}

void toDip(LPARAM lp, float& x, float& y) {
    float scale = 96.0f / float(GetDpiForWindow(panel.hwnd));
    x = GET_X_LPARAM(lp) * scale;
    y = GET_Y_LPARAM(lp) * scale;
}

void sizeWindow(UINT dpi, int& w, int& h) {
    RECT rc{ 0, 0, MulDiv(int(std::ceil(kPanelWidth)), dpi, 96), MulDiv(int(std::ceil(panel.height)), dpi, 96) };
    AdjustWindowRectExForDpi(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, 0, dpi);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
}

LRESULT CALLBACK panelProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    float x = 0, y = 0;
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        paint();
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        if (wp == kAnimationTimer) {
            if (!stepKnobs()) KillTimer(h, kAnimationTimer);
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEMOVE: {
        toDip(lp, x, y);
        if (panel.dragging && panel.pressed >= 0) {
            setSliderFromMouse(panel.items[panel.pressed], x);
            return 0;
        }
        int now = itemAt(x, y);
        if (now != panel.hover) {
            panel.hover = now;
            TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, h, 0 };
            TrackMouseEvent(&track);
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        panel.hover = -1;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        toDip(lp, x, y);
        panel.pressed = itemAt(x, y);
        panel.focus = -1;
        if (panel.pressed >= 0 && !usable(panel.items[panel.pressed], *panel.app)) panel.pressed = -1;
        if (panel.pressed >= 0 && panel.items[panel.pressed].kind == Kind::Slider) {
            panel.dragging = true;
            SetCapture(h);
            setSliderFromMouse(panel.items[panel.pressed], x);
        }
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: {
        toDip(lp, x, y);
        if (panel.dragging) {
            panel.dragging = false;
            ReleaseCapture();
        } else if (panel.pressed >= 0 && panel.pressed == itemAt(x, y)) {
            activate(panel.items[panel.pressed]);
        }
        panel.pressed = -1;
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) ShowWindow(h, SW_HIDE);
        else if (wp == VK_SHIFT) panel.shiftDown = true;
        else if (wp == VK_TAB) moveFocus(panel.shiftDown ? -1 : 1);
        else if (panel.focus >= 0) {
            Item& item = panel.items[panel.focus];
            if ((wp == VK_SPACE || wp == VK_RETURN) && item.kind != Kind::Slider) {
                activate(item);
            } else if ((wp == VK_LEFT || wp == VK_RIGHT) && item.kind == Kind::Slider) {
                Settings before = panel.app->settings;
                int& value = panel.app->settings.*item.number;
                value = std::clamp(value + (wp == VK_RIGHT ? 5 : -5), item.low, item.high);
                changed(before);
            }
        }
        return 0;
    case WM_KEYUP:
        if (wp == VK_SHIFT) panel.shiftDown = false;
        return 0;
    case WM_KILLFOCUS:
        panel.shiftDown = false;    // the key may be released while another window has the focus
        return 0;
    case WM_DPICHANGED: {
        const RECT* r = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        panel.brush.Reset();
        panel.target.Reset();
        return 0;
    }
    case WM_SIZE:
        if (panel.target) {
            RECT rc;
            GetClientRect(h, &rc);
            panel.target->Resize(D2D1::SizeU(rc.right, rc.bottom));
        }
        return 0;
    case WM_CLOSE:
        ShowWindow(h, SW_HIDE);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

bool createPanel(App& app) {
    panel.app = &app;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, panel.d2d.GetAddressOf()))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(panel.write.GetAddressOf())))) return false;

    auto format = [](ComPtr<IDWriteTextFormat>& out, float size, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT align, DWRITE_PARAGRAPH_ALIGNMENT vertical) {
        panel.write->CreateTextFormat(L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &out);
        if (!out) return;
        out->SetTextAlignment(align);
        out->SetParagraphAlignment(vertical);
        out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    };
    format(panel.bodyText, 13.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    format(panel.headingText, 11.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    format(panel.valueText, 12.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    format(panel.hintText, 12, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    format(panel.buttonText, 13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    buildItems();

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = panelProc;
    wc.hInstance = app.instance;
    wc.lpszClassName = kPanelClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(app.instance, MAKEINTRESOURCEW(IDI_APP));
    RegisterClassExW(&wc);

    panel.hwnd = CreateWindowExW(0, kPanelClass, L"SnowDesk settings", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, nullptr, nullptr, app.instance, nullptr);
    if (!panel.hwnd) return false;

    BOOL dark = TRUE;
    DwmSetWindowAttribute(panel.hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    COLORREF caption = RGB(0x10, 0x16, 0x1F);
    DwmSetWindowAttribute(panel.hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
    return true;
}

}

// Opens beside the tray, where the click came from.
void showPanel(App& app) {
    if (!panel.hwnd && !createPanel(app)) return;
    if (!IsWindowVisible(panel.hwnd)) {
        POINT cursor;
        GetCursorPos(&cursor);
        MONITORINFO info{ sizeof(info) };
        GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &info);
        UINT dpi = 96, unused = 96;
        GetDpiForMonitor(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), MDT_EFFECTIVE_DPI, &dpi, &unused);
        int w = 0, h = 0;
        sizeWindow(dpi, w, h);
        int x = std::clamp(int(cursor.x) - w / 2, int(info.rcWork.left) + 12, int(info.rcWork.right) - w - 12);
        int y = int(info.rcWork.bottom) - h - 12;
        SetWindowPos(panel.hwnd, HWND_TOP, x, y, w, h, SWP_NOACTIVATE);
        for (Item& item : panel.items) {
            if (item.kind == Kind::Toggle) item.knob = item.read(app) ? 1.0f : 0.0f;
        }
    }
    ShowWindow(panel.hwnd, SW_SHOW);
    SetForegroundWindow(panel.hwnd);
    InvalidateRect(panel.hwnd, nullptr, FALSE);
}

void refreshPanel() {
    if (panel.hwnd) InvalidateRect(panel.hwnd, nullptr, FALSE);
}
