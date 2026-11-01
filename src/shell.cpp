// The tray icon with its menu, the messages Windows sends the application, and applying settings.
#include "app.h"
#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wtsapi32.h>
#include <algorithm>
#include "resource.h"

namespace {


const UINT WM_TRAY = WM_APP + 1;
constexpr float kBaseFlakesPerMegapixel = 480;
constexpr float kStickRate = 0.2f;          // about half an hour for a full blanket at normal intensity
constexpr float kBareRate = 0.5f;           // bare surfaces hold snow less well than snow does
constexpr float kChillDelay = 75;           // seconds before anything lies at all
constexpr float kChillRamp = 240;           // seconds more until it can start anywhere
constexpr float kUnlimitedStickScale = 20;  // tall drifts would otherwise take many hours
constexpr float kLowCpuDensity = 0.5f;      // soft snow: half the flakes of the chosen intensity

enum MenuId { ID_PANEL = 100, ID_TOGGLE, ID_CLEAR, ID_EXIT };

App* hostApp = nullptr;
NOTIFYICONDATAW tray{};
UINT taskbarCreated = 0;

void showMenu(App& app) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_PANEL, L"Ρυθμίσεις…");
    SetMenuDefaultItem(menu, ID_PANEL, FALSE);
    AppendMenuW(menu, MF_STRING, ID_TOGGLE, app.settings.enabled ? L"Παύση χιονιού" : L"Συνέχεια χιονιού");
    AppendMenuW(menu, MF_STRING, ID_CLEAR, L"Καθαρισμός χιονιού");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_EXIT, L"Έξοδος");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(app.host);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, app.host, nullptr);
    PostMessageW(app.host, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

void onCommand(App& app, int id) {
    switch (id) {
    case ID_PANEL:
        showPanel(app);
        break;
    case ID_TOGGLE: {
        Settings before = app.settings;
        app.settings.enabled = !app.settings.enabled;
        applySettings(app, before);
        refreshPanel();
        break;
    }
    case ID_CLEAR:
        clearAllSnow(app);
        break;
    case ID_EXIT:
        app.running = false;
        break;
    }
}

LRESULT CALLBACK hostProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    App& app = *hostApp;
    if (taskbarCreated && msg == taskbarCreated) {     // Explorer restarted
        addTrayIcon(app);
        app.windowsDirty = true;
        return 0;
    }
    switch (msg) {
    case WM_TRAY:
        if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) showMenu(app);
        else if (LOWORD(lp) == WM_LBUTTONUP) showPanel(app);
        return 0;
    case WM_COMMAND:
        onCommand(app, LOWORD(wp));
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
        app.screensDirty = true;
        return 0;
    case WM_SETTINGCHANGE:
        if (wp == SPI_SETWORKAREA) app.screensDirty = true;
        app.windowsDirty = true;
        return 0;
    case WM_WTSSESSION_CHANGE:
        if (wp == WTS_SESSION_LOCK) app.locked = true;
        if (wp == WTS_SESSION_UNLOCK) app.locked = false;
        return 0;
    case WM_CLOSE:
        app.running = false;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}

#ifndef SNOW_NO_AUTOSTART
// Starting with Windows is a shortcut in the user's Startup folder: visible, and removable by hand.
static std::wstring startupShortcut() {
    PWSTR folder = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Startup, 0, nullptr, &folder))) path = std::wstring(folder) + L"\\SnowDesk.lnk";
    CoTaskMemFree(folder);
    return path;
}

bool autostartEnabled() {
    std::wstring path = startupShortcut();
    return !path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

void setAutostart(bool on) {
    std::wstring path = startupShortcut();
    if (path.empty()) return;
    if (!on) {
        DeleteFileW(path.c_str());
        return;
    }
    wchar_t exe[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return;
    ComPtr<IShellLinkW> link;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return;
    link->SetPath(exe);
    link->SetDescription(L"SnowDesk");
    std::wstring folder(exe);
    folder.resize(folder.find_last_of(L'\\'));
    link->SetWorkingDirectory(folder.c_str());
    ComPtr<IPersistFile> file;
    if (SUCCEEDED(link.As(&file))) file->Save(path.c_str(), TRUE);
}
#endif

void clearAllSnow(App& app) {
    for (auto& screen : app.screens) screen->world.clearSnow();
    app.cursor.clear();
}

SnowParams currentParams(const App& app) {
    const Settings& s = app.settings;
    SnowParams params;
    params.flakesPerMegapixel = kBaseFlakesPerMegapixel * s.density / 100.0f * app.options.snowScale;
    params.fallScale = s.fallSpeed / 100.0f;
    params.flakeScale = s.flakeSize / 100.0f;
    params.settle = s.settle;
    params.unlimited = s.unlimited;
    params.onIcons = s.onIcons;
    params.onWindows = s.onWindows;
    params.onGround = s.onTaskbar;
    float pace = s.settleSpeed / 100.0f;
    params.stickRate = kStickRate * pace * (s.unlimited ? kUnlimitedStickScale : 1);
    params.bareRate = kBareRate;
    params.chillDelay = kChillDelay / pace / app.options.snowScale;
    params.chillRamp = kChillRamp / pace / app.options.snowScale;
    if (s.lowCpu) {
        params.flakesOnly = true;
        params.settle = false;
        params.flakesPerMegapixel *= kLowCpuDensity;
    }
    if (!s.cursorSnow || !s.settle) params.accumulateAfter = 1e9f;
    return params;
}

// Called after any change to the settings, with what they were before.
void applySettings(App& app, const Settings& before) {
    const Settings& now = app.settings;
    bool windBlows = now.wind && !now.lowCpu;
    app.weather.configure(windBlows ? now.windStrength / 100.0f : 0.0f, now.sun && now.settle && !now.lowCpu, now.lowCpu);
    bool settled = before.settle && !before.lowCpu, settles = now.settle && !now.lowCpu;
    if (settled && !settles) clearAllSnow(app);
    if (before.cursorSnow && !now.cursorSnow) app.cursor.clear();
    if (before.flakesInFront != now.flakesInFront) app.windowsDirty = true;
    saveSettings(now, app.options.settingsPath);
}

bool createHostWindow(App& app) {
    hostApp = &app;

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = app.instance;
    wc.lpszClassName = kOverlayClass;
    RegisterClassExW(&wc);
    wc.lpfnWndProc = hostProc;
    wc.lpszClassName = kHostClass;
    wc.hIcon = LoadIconW(app.instance, MAKEINTRESOURCEW(IDI_APP));
    RegisterClassExW(&wc);

    app.host = CreateWindowExW(WS_EX_TOOLWINDOW, kHostClass, L"SnowDesk", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, app.instance, nullptr);
    if (!app.host) return false;
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    WTSRegisterSessionNotification(app.host, NOTIFY_FOR_THIS_SESSION);
    return true;
}

void addTrayIcon(App& app) {
    tray = {};
    tray.cbSize = sizeof(tray);
    tray.hWnd = app.host;
    tray.uID = 1;
    tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    tray.uCallbackMessage = WM_TRAY;
    tray.hIcon = static_cast<HICON>(LoadImageW(app.instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    wcscpy_s(tray.szTip, L"SnowDesk: χιόνι στην επιφάνεια εργασίας");
    Shell_NotifyIconW(NIM_ADD, &tray);
}

void removeTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &tray);
    if (hostApp && hostApp->host) WTSUnRegisterSessionNotification(hostApp->host);
}
