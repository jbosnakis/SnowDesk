// SnowDesk: snow that falls over the Windows desktop and settles on what is really there.
#include <windows.h>
#include <shlobj.h>
#include <share.h>
#include <algorithm>
#include "app.h"

namespace {

constexpr float kFrameSeconds = 1 / 120.0f;
constexpr float kSavingFrameSeconds = 1 / 60.0f;
constexpr float kIdleFrameSeconds = 0.25f;
constexpr float kNobodyHereSeconds = 10 * 60;   // let the OLED panel rest when nobody is at the PC
constexpr float kFadeInPerSecond = 1.6f;
constexpr float kFadeOutPerSecond = 6.0f;
constexpr double kRescanSeconds = 0.4;
constexpr double kTopmostSeconds = 2.0;
constexpr double kShellCheckSeconds = 0.25;

App app;

double seconds() {
    static LARGE_INTEGER freq{};
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart) / double(freq.QuadPart);
}

float hourOfDay() {
    if (app.options.fixedHour >= 0) return app.options.fixedHour;
    SYSTEMTIME st;
    GetLocalTime(&st);
    return st.wHour + st.wMinute / 60.0f;
}

BOOL CALLBACK onMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM param) {
    MONITORINFO info{ sizeof(info) };
    if (GetMonitorInfoW(monitor, &info)) reinterpret_cast<std::vector<MONITORINFO>*>(param)->push_back(info);
    return TRUE;
}

void destroyScreens() {
    for (auto& screen : app.screens) app.renderer.destroySurface(screen->surface);
    app.screens.clear();
}

bool createScreens() {
    std::vector<MONITORINFO> monitors;
    EnumDisplayMonitors(nullptr, nullptr, onMonitor, reinterpret_cast<LPARAM>(&monitors));
    uint32_t seed = GetTickCount() | 1;
    for (const MONITORINFO& m : monitors) {
        auto screen = std::make_unique<Screen>();
        screen->bounds = { m.rcMonitor.left, m.rcMonitor.top, m.rcMonitor.right, m.rcMonitor.bottom };
        screen->work = { m.rcWork.left, m.rcWork.top, m.rcWork.right, m.rcWork.bottom };
        Surface& s = screen->surface;
        s.x = screen->bounds.l;
        s.y = screen->bounds.t;
        s.w = screen->bounds.w();
        s.h = screen->bounds.h();
        if (!app.renderer.createSurface(s, app.instance, kOverlayClass)) return false;
        screen->world.init(s.w, s.h, seed);
        screen->playground.init(seed ^ 0x5BD1E995u);
        if (app.options.firstScene != PlayScene::None) screen->playground.request(app.options.firstScene);
        seed = seed * 2654435761u + 1;
        app.screens.push_back(std::move(screen));
    }
    app.lastRects.clear();
    app.windowsDirty = true;
    return !app.screens.empty();
}

bool recreateIfNeeded() {
    if (app.deviceLost) {   // a driver update or GPU reset
        app.deviceLost = false;
        destroyScreens();
        app.renderer.shutdown();
        Sleep(1000);
        return app.renderer.init() && createScreens();
    }
    if (app.screensDirty) {
        app.screensDirty = false;
        destroyScreens();
        return createScreens();
    }
    return true;
}

// The shell takes a fraction of a millisecond to answer, far too long to ask on every frame.
bool shellReportsFullscreen(double now) {
    static double nextCheck = 0;
    static bool fullscreen = false;
    if (now >= nextCheck) {
        nextCheck = now + kShellCheckSeconds;
        QUERY_USER_NOTIFICATION_STATE state{};
        fullscreen = SUCCEEDED(SHQueryUserNotificationState(&state)) &&
            (state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE);
    }
    return fullscreen;
}

bool shouldHideEverywhere(double now) {
    if (!app.settings.enabled || app.locked) return true;
    LASTINPUTINFO input{ sizeof(input) };
    if (GetLastInputInfo(&input) && (GetTickCount() - input.dwTime) / 1000.0f > kNobodyHereSeconds) return true;
    return !app.options.ignoreFullscreen && shellReportsFullscreen(now);
}

void updateScreen(Screen& screen, bool hideEverywhere, SnowCursor* cursor, float dt, double now, double start) {
    bool wanted = !hideEverywhere && !screen.covered;
    float rate = wanted ? kFadeInPerSecond : -kFadeOutPerSecond;
    screen.fade = std::clamp(screen.fade + dt * rate, 0.0f, 1.0f);

    Surface& s = screen.surface;
    if (screen.fade <= 0) {
        if (s.shown) {
            ShowWindow(s.hwnd, SW_HIDE);    // fully hidden, so games keep their direct path to the display
            s.shown = false;
        }
        return;
    }
    if (!s.shown) {
        SetWindowPos(s.hwnd, HWND_TOPMOST, s.x, s.y, s.w, s.h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        s.shown = true;
    }
    screen.world.step(dt, currentParams(app), app.weather, cursor);
    bool kids = app.settings.kids && app.settings.settle && !app.settings.lowCpu;    // they play in snow that stays
    if (kids) screen.playground.step(dt, screen.world, app.weather, cursor);
    else screen.world.clearGuards();

    DrawParams params;
    if (kids) params.shapes = &screen.playground.shapes();
    params.flakesInFront = app.settings.flakesInFront;
    params.flakesOnly = app.settings.lowCpu;
    params.fade = screen.fade * app.settings.opacity / 100.0f;
    params.time = float(now - start);
    params.dt = dt;
    params.quickEase = now < app.draggingUntil;
    if (!app.renderer.draw(s, screen.world, cursor, params)) app.deviceLost = true;
}

void keepOnTop() {
    for (auto& screen : app.screens) {
        if (screen->surface.shown) {
            SetWindowPos(screen->surface.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        }
    }
}

void pumpMessagesUntil(HANDLE timer) {
    for (;;) {
        DWORD result = MsgWaitForMultipleObjectsEx(1, &timer, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) app.running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (result == WAIT_OBJECT_0 || !app.running || app.screensDirty) return;
    }
}

void runLoop() {
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);

    double start = seconds(), previous = start, statsAt = start, lastTopmost = start, workTotal = 0;
    int frames = 0;
    bool maskDumped = false;

    while (app.running) {
        double now = seconds();
        float dt = std::min(float(now - previous), 0.05f);
        previous = now;

        bool animating = false;
        for (auto& screen : app.screens) animating = animating || screen->fade > 0;
        app.iconWatcher.setActive(animating && !app.settings.lowCpu);
        LARGE_INTEGER due;
        float frame = app.settings.saveEnergy || app.settings.lowCpu ? kSavingFrameSeconds : kFrameSeconds;
        due.QuadPart = -LONGLONG((animating ? frame : kIdleFrameSeconds) * 10000000.0f);
        SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);

        if (!recreateIfNeeded()) break;

        applyIconChanges(app);
        if (app.windowsDirty || now - app.lastScan > kRescanSeconds) {
            scanDesktop(app, now);
        } else {
            for (auto& screen : app.screens) {
                if (screen->solidsDirty) rebuildSolids(app, *screen);
            }
        }

        if (app.options.testWindow) driveTestWindow(app, now - start);
        if (!app.options.dumpMaskPath.empty() && !maskDumped && now - start > 2.5) {
            dumpSolidMask(app);
            maskDumped = true;
        }

        app.weather.step(dt, hourOfDay());
        PointerStep pointer;
        if (!app.settings.lowCpu) pointer = trackPointer(app, dt, now - start);
        bool hideEverywhere = shouldHideEverywhere(now);
        for (auto& screen : app.screens) {
            SnowCursor* cursor = screen.get() == pointer.screen ? &app.cursor : nullptr;
            updateScreen(*screen, hideEverywhere, cursor, dt, now, start);
        }

        if (now - lastTopmost > kTopmostSeconds) {
            lastTopmost = now;
            keepOnTop();
        }

        workTotal += seconds() - now;
        frames++;
        if (now - statsAt >= 1.0) {
            logStats(app, now - start, frames, workTotal);
            statsAt = now;
            frames = 0;
            workTotal = 0;
        }
        if (app.options.quitAfter > 0 && now - start > app.options.quitAfter) app.running = false;

        pumpMessagesUntil(timer);
    }
    CloseHandle(timer);
}

}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\SnowDesk.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    app.instance = instance;
    app.options = parseOptions();
    if (app.options.settingsPath.empty()) app.options.settingsPath = defaultSettingsPath();
    app.settings = loadSettings(app.options.settingsPath);
    if (!app.options.logPath.empty()) app.log = _wfsopen(app.options.logPath.c_str(), L"w", _SH_DENYNO);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    if (!createHostWindow(app) || !app.renderer.init() || !createScreens()) {
        MessageBoxW(nullptr, L"Δεν ξεκίνησε το Direct3D 11.", L"SnowDesk", MB_ICONERROR);
        return 1;
    }
    addTrayIcon(app);
    app.weather.init(GetTickCount() | 1);
    applySettings(app, app.settings);
    app.iconWatcher.start();
    installWindowHooks(app);
    if (app.options.openPanel) showPanel(app);

    runLoop();

    removeWindowHooks();
    app.iconWatcher.stop();
    removeTrayIcon();
    closeTestWindow();
    destroyScreens();
    app.renderer.shutdown();
    if (app.log) fclose(app.log);
    CoUninitialize();
    ReleaseMutex(single);
    CloseHandle(single);
    return 0;
}
