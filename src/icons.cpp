#include "icons.h"
#include <exdisp.h>
#include <shlguid.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <map>

using Microsoft::WRL::ComPtr;

namespace {
constexpr int kLargestImage = 1024;

struct Shape {
    int w = 0, h = 0;
    std::shared_ptr<const std::vector<uint8_t>> mask;
};

// IFolderView gives us the view through the desktop shell window, the same way Explorer scripts do.
bool openDesktop(ComPtr<IFolderView2>& view, HWND& list) {
    ComPtr<IShellWindows> windows;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&windows)))) return false;
    VARIANT loc{}, root{};
    loc.vt = VT_I4;
    loc.lVal = CSIDL_DESKTOP;
    root.vt = VT_EMPTY;
    long hwnd = 0;
    ComPtr<IDispatch> disp;
    if (windows->FindWindowSW(&loc, &root, SWC_DESKTOP, &hwnd, SWFO_NEEDDISPATCH, &disp) != S_OK || !disp) return false;
    ComPtr<IServiceProvider> sp;
    if (FAILED(disp.As(&sp))) return false;
    ComPtr<IShellBrowser> browser;
    if (FAILED(sp->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser)))) return false;
    ComPtr<IShellView> shellView;
    if (FAILED(browser->QueryActiveShellView(&shellView))) return false;
    if (FAILED(shellView.As(&view))) return false;
    HWND def = nullptr;
    if (FAILED(shellView->GetWindow(&def)) || !def) return false;
    list = FindWindowExW(def, nullptr, L"SysListView32", nullptr);
    return list != nullptr;
}

// Keep only pixels that are actually drawn. Fully transparent layers let the snow through.
Shape loadShape(IShellItem* item, int size) {
    Shape out;
    ComPtr<IShellItemImageFactory> factory;
    if (FAILED(item->QueryInterface(IID_PPV_ARGS(&factory)))) return out;
    HBITMAP bmp = nullptr;
    SIZE sz{ size, size };
    if (FAILED(factory->GetImage(sz, SIIGBF_BIGGERSIZEOK, &bmp)) || !bmp) return out;

    BITMAP info{};
    GetObjectW(bmp, sizeof(info), &info);
    int w = info.bmWidth, h = info.bmHeight;
    if (w <= 0 || h <= 0 || w > kLargestImage || h > kLargestImage) {
        DeleteObject(bmp);
        return out;
    }
    std::vector<uint32_t> px(size_t(w) * h);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC dc = GetDC(nullptr);
    int ok = GetDIBits(dc, bmp, 0, h, px.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    DeleteObject(bmp);
    if (!ok) return out;

    bool hasAlpha = false;
    for (uint32_t v : px) if (v >> 24) { hasAlpha = true; break; }

    auto mask = std::make_shared<std::vector<uint8_t>>(size_t(w) * h, 0);
    for (size_t i = 0; i < px.size(); i++) {
        uint32_t v = px[i];
        if (hasAlpha ? (v >> 24) > 90 : (v & 0xFFFFFF) != 0) (*mask)[i] = 1;
    }
    out.w = w;
    out.h = h;
    out.mask = mask;
    return out;
}
}

void IconWatcher::start() {
    quit = false;
    th = std::thread([this] { run(); });
}

// A call into a hung Explorer may never return. Waiting for it would keep the app from closing.
void IconWatcher::stop() {
    quit = true;
    for (int waited = 0; waited < 2000 && !finished; waited += 20) Sleep(20);
    if (!th.joinable()) return;
    if (finished) th.join();
    else th.detach();
}

std::shared_ptr<const std::vector<DesktopIcon>> IconWatcher::latest() {
    std::lock_guard<std::mutex> g(lock);
    return current;
}

// Each read is hundreds of trips into Explorer for something that hardly ever changes.
void IconWatcher::waitForNextRead() {
    constexpr int kTickMs = 50, kBurstGapMs = 250, kQuietReadMs = 2500;
    for (int waited = 0; !quit; waited += kTickMs) {
        if (burst > 0 && waited >= kBurstGapMs) {
            burst--;
            return;
        }
        if (active && waited >= kQuietReadMs) return;
        Sleep(kTickMs);
    }
}

void IconWatcher::run() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    {
        ComPtr<IFolderView2> view;
        HWND list = nullptr;
        std::map<std::wstring, Shape> cache;
        std::vector<DesktopIcon> last;

        while (!quit) {
            waitForNextRead();
            if (quit) break;
            if (!view && !openDesktop(view, list)) { Sleep(1500); continue; }

            std::vector<DesktopIcon> icons;
            int count = 0;
            UINT sizePx = 0;
            FOLDERVIEWMODE mode{};
            bool failed = !IsWindow(list) || FAILED(view->ItemCount(SVGIO_ALLVIEW, &count)) ||
                FAILED(view->GetViewModeAndIconSize(&mode, (int*)&sizePx));
            if (failed) {
                view.Reset();
                list = nullptr;
                continue;
            }
            bool hidden = !IsWindowVisible(list);
            POINT origin{ 0, 0 };
            ClientToScreen(list, &origin);

            // The desktop reports the logical icon size; the view paints it scaled for the monitor DPI.
            UINT dpi = GetDpiForWindow(list);
            int iconPx = MulDiv(int(sizePx), dpi ? dpi : 96, 96);

            for (int i = 0; i < count && !hidden && !quit; i++) {
                ComPtr<IShellItem> item;
                if (FAILED(view->GetItem(i, IID_PPV_ARGS(&item)))) continue;
                PWSTR name = nullptr;
                if (FAILED(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &name))) continue;
                std::wstring key(name);
                CoTaskMemFree(name);

                auto it = cache.find(key);
                if (it == cache.end() || it->second.w != iconPx) {
                    Shape s = loadShape(item.Get(), iconPx);
                    if (!s.mask) continue;
                    it = cache.insert_or_assign(key, s).first;
                }

                PITEMID_CHILD pidl = nullptr;
                if (FAILED(view->Item(i, &pidl))) continue;
                POINT pt{};
                HRESULT hr = view->GetItemPosition(pidl, &pt);
                CoTaskMemFree(pidl);
                if (FAILED(hr)) continue;

                const Shape& s = it->second;
                DesktopIcon ic;
                ic.key = key;
                ic.w = s.w;
                ic.h = s.h;
                ic.mask = s.mask;
                // Offsets measured against a screenshot of the real desktop at 48px icons.
                ic.rc.l = origin.x + pt.x + 1 + (iconPx - s.w) / 2;
                ic.rc.t = origin.y + pt.y + 6 + (iconPx - s.h);     // wide thumbnails rest on the bottom
                ic.rc.r = ic.rc.l + s.w;
                ic.rc.b = ic.rc.t + s.h;
                icons.push_back(std::move(ic));
            }
            if (quit) break;

            bool same = icons.size() == last.size();
            for (size_t i = 0; same && i < icons.size(); i++) {
                same = icons[i].key == last[i].key && icons[i].rc == last[i].rc && icons[i].mask == last[i].mask;
            }
            if (!same) {
                last = icons;
                auto snapshot = std::make_shared<const std::vector<DesktopIcon>>(std::move(icons));
                {
                    std::lock_guard<std::mutex> g(lock);
                    current = snapshot;
                }
                ver++;
            }
        }
    }
    CoUninitialize();
    finished = true;
}
