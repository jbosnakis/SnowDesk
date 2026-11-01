// State shared by the parts of the application, and the functions each part exposes.
#pragma once
#include <windows.h>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "icons.h"
#include "playground.h"
#include "render.h"
#include "settings.h"
#include "world.h"

inline const wchar_t kOverlayClass[] = L"SnowDeskOverlay";
inline const wchar_t kHostClass[] = L"SnowDeskHost";

// Developer switches; a normal launch uses none of them.
struct Options {
    std::wstring logPath;
    std::wstring dumpMaskPath;
    bool ignoreFullscreen = false;
    bool scriptedPointer = false;
    bool testWindow = false;
    float snowScale = 1;
    float quitAfter = 0;
    float fixedHour = -1;       // pretend it is this time of day
    PlayScene firstScene = PlayScene::None;
    std::wstring settingsPath;
    bool openPanel = false;
};

struct Screen {
    Rect bounds;            // virtual desktop coordinates
    Rect work;
    Surface surface;
    World world;
    Playground playground;
    float fade = 0;
    bool covered = false;   // something maximized or fullscreen is here: no snow on this monitor
    bool solidsDirty = true;

    Rect toLocal(const Rect& r) const { return { r.l - bounds.l, r.t - bounds.t, r.r - bounds.l, r.b - bounds.t }; }
};

struct ScannedWindow {
    HWND hwnd;
    Rect rc;
    uint8_t kind;
    bool rounded;
};

// The icon the mouse pressed on, followed until it counts as a drag.
struct IconPress {
    bool down = false;
    float x = 0, y = 0;
    int icon = -1;
    bool thrown = false;
};

struct App {
    HINSTANCE instance = nullptr;
    HWND host = nullptr;
    Options options;
    Settings settings;
    FILE* log = nullptr;

    Renderer renderer;
    std::vector<std::unique_ptr<Screen>> screens;
    Weather weather;

    IconWatcher iconWatcher;
    std::shared_ptr<const std::vector<DesktopIcon>> icons;
    unsigned iconVersion = 0;

    std::vector<ScannedWindow> windows;
    std::unordered_map<HWND, Rect> lastRects;
    bool windowsDirty = true;
    double lastScan = 0;
    double draggingUntil = 0;

    SnowCursor cursor;
    HCURSOR cursorHandle = nullptr;
    float lastX = 0, lastY = 0;
    bool hasLastPosition = false;
    IconPress press;
    bool desktopPressed = false;    // the desktop's icon list holds the mouse

    bool locked = false;
    bool running = true;
    bool screensDirty = false;
    bool deviceLost = false;
};

// scene.cpp: what is on the desktop right now
void installWindowHooks(App& app);
void removeWindowHooks();
void scanDesktop(App& app, double now);
void applyIconChanges(App& app);
void rebuildSolids(App& app, Screen& screen);

// pointer.cpp: the mouse, its snow, and icon drags
struct PointerStep {
    Screen* screen = nullptr;
};
PointerStep trackPointer(App& app, float dt, double now);

// shell.cpp: tray icon, system messages and applying settings
bool createHostWindow(App& app);
void addTrayIcon(App& app);
void removeTrayIcon();
SnowParams currentParams(const App& app);
void applySettings(App& app, const Settings& before);
void clearAllSnow(App& app);
#ifndef SNOW_NO_AUTOSTART
bool autostartEnabled();
void setAutostart(bool on);
#endif

// panel.cpp: the settings window
void showPanel(App& app);
void refreshPanel();

// diagnostics.cpp: developer switches
Options parseOptions();
void driveTestWindow(App& app, double t);
void closeTestWindow();
void scriptedPointerPosition(double t, float& x, float& y, bool& pressed);
void dumpSolidMask(const App& app);
void logStats(App& app, double t, int frames, double workSeconds);
