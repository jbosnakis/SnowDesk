// Developer switches used to test the application without touching the real mouse or desktop.
#include "app.h"
#include <shellapi.h>
#include <cmath>
#include <map>

namespace {
HWND testWindow = nullptr;
}

Options parseOptions() {
    Options o;
#ifdef SNOW_DEV
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; i++) {
        std::wstring arg = argv[i];
        auto value = [&]() { return i + 1 < argc ? std::wstring(argv[++i]) : std::wstring(); };
        if (arg == L"--log") o.logPath = value();
        else if (arg == L"--dump-mask") o.dumpMaskPath = value();
        else if (arg == L"--ignore-fullscreen") o.ignoreFullscreen = true;
        else if (arg == L"--sim-cursor") o.scriptedPointer = true;
        else if (arg == L"--test-window") o.testWindow = true;
        else if (arg == L"--fast") o.snowScale = float(_wtof(value().c_str()));
        else if (arg == L"--quit-after") o.quitAfter = float(_wtof(value().c_str()));
        else if (arg == L"--hour") o.fixedHour = float(_wtof(value().c_str()));
        else if (arg == L"--settings") o.settingsPath = value();
        else if (arg == L"--panel") o.openPanel = true;
        else if (arg == L"--scene") {
            std::wstring name = value();
            o.firstScene = name == L"fight" ? PlayScene::Fight : name == L"climb" ? PlayScene::Climb : PlayScene::Snowman;
        }
    }
    LocalFree(argv);
#endif
    return o;
}

// Sits still, gets dragged, then covers the screen for a moment like a game or a fullscreen video.
void driveTestWindow(App& app, double t) {
    constexpr int homeX = 60, homeY = 500, width = 340, height = 200;
    if (!testWindow) {
        testWindow = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, L"STATIC", L"SnowDesk test", WS_POPUP | WS_CAPTION | WS_VISIBLE,
            homeX, homeY, width, height, nullptr, nullptr, app.instance, nullptr);
    }
    static bool covering = false;
    bool cover = t > 19 && t < 20.5;
    if (cover != covering) {
        covering = cover;
        if (cover) SetWindowPos(testWindow, nullptr, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), SWP_NOZORDER | SWP_NOACTIVATE);
        else SetWindowPos(testWindow, nullptr, homeX, homeY, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (cover) return;

    double phase = std::fmod(t, 24.0);
    int x = homeX;
    if (phase > 14 && phase < 18) x += int(140 * (1 - std::cos((phase - 14) * 3.14159 * 1.5)) / 2);
    RECT r;
    GetWindowRect(testWindow, &r);
    if (r.left != x) SetWindowPos(testWindow, nullptr, x, homeY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void closeTestWindow() {
    if (testWindow) DestroyWindow(testWindow);
    testWindow = nullptr;
}

// Stands still over the screen, presses and drags a desktop icon, then sweeps along the taskbar.
void scriptedPointerPosition(double t, float& x, float& y, bool& pressed) {
    pressed = t > 10 && t < 11;
    if (pressed) {
        x = 36 + float(t - 10) * 120;
        y = 146;
    } else if (std::fmod(t, 24.0) < 14) {
        x = 210;
        y = 700;
    } else {
        x = 210 + 180 * float(std::sin(t * 1.3));
        y = 1375 + 8 * float(std::sin(t * 2.1));
    }
}

void dumpSolidMask(const App& app) {
    if (app.screens.empty()) return;
    const World& w = app.screens[0]->world;
    FILE* f = nullptr;
    if (_wfopen_s(&f, app.options.dumpMaskPath.c_str(), L"wb") || !f) return;
    fprintf(f, "P5\n%d %d\n255\n", w.gridW(), w.gridH());
    std::vector<uint8_t> row(w.gridW());
    static const uint8_t shades[4] = { 0, 90, 255, 170 };
    for (int y = 0; y < w.gridH(); y++) {
        const uint8_t* s = w.solidCells() + size_t(y) * w.gridW();
        for (int x = 0; x < w.gridW(); x++) row[x] = shades[s[x]];
        fwrite(row.data(), 1, row.size(), f);
    }
    fclose(f);
}

// Per row of icons, top to bottom: how many of the uncovered icons have snow on them.
static void logIconCoverage(App& app, const Screen& s) {
    if (!app.icons) return;
    constexpr int kVisibleCells = 8;
    std::map<int, std::pair<int, int>> rows;
    for (const DesktopIcon& ic : *app.icons) {
        Rect r = s.toLocal(ic.rc);
        if (s.world.solidAt((r.l + r.r) / 2, r.t + 2) == S_WINDOW) continue;
        Rect above{ r.l, r.t - 16, r.r, r.b };
        auto& row = rows[r.t];
        row.second++;
        if (s.world.countSnowIn(above) >= kVisibleCells) row.first++;
    }
    fprintf(app.log, "  icons with snow by row:");
    for (const auto& [top, row] : rows) fprintf(app.log, " y%d=%d/%d", top, row.first, row.second);
    fprintf(app.log, "\n");
}

void logStats(App& app, double t, int frames, double workSeconds) {
    if (!app.log || app.screens.empty()) return;
    const Screen& s = *app.screens[0];
    const World& w = s.world;
    logIconCoverage(app, s);
    fprintf(app.log, "t=%.1f fps=%d work_ms=%.2f visible=%d covered=%d particles=%zu active=%zu snow=%zu windows=%zu icons=%zu cursor(%.0f,%.0f idle=%.1f cap=%d over=%d)\n",
        t, frames, workSeconds / frames * 1000, int(s.surface.shown), int(s.covered),
        w.particleCount(), w.activeCount(), w.countSnow(), app.windows.size(), app.icons ? app.icons->size() : 0,
        app.cursor.x, app.cursor.y, app.cursor.idle, app.cursor.snowCount, int(app.cursor.overWindow));
    fprintf(app.log, "  kids=%zu snowmen=%zu spot=%.0f thrown=%d hits=%d hiddenSnow=%zu\n", s.playground.kidCount(), s.playground.snowmanCount(),
        s.playground.sceneSpot(), s.playground.ballsThrown(), s.playground.ballsLanded(), w.countHiddenSnow());
    fprintf(app.log, "  scene=%d onWindow=%d onIcons=%d hops=%d heaps=%d soft=%d hard=%d\n", int(s.playground.currentScene()), int(s.playground.playingOnWindow()),
        s.playground.kidsOnIcons(), s.playground.iconHops(), s.playground.heapsThrown(), s.playground.softLandings(), s.playground.hardLandings());
    const Weather& sky = app.weather;
    fprintf(app.log, "  weather: hour=%.1f wind=%.0f gust=%.2f snowfall=%.2f sun=%.2f\n", sky.hour(), sky.wind(), sky.gustStrength(), sky.snowfall(), sky.sun());
    fflush(app.log);
}
