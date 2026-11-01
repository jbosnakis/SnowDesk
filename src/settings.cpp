#include "settings.h"
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>
#include <algorithm>

namespace {

const wchar_t kSection[] = L"snow";

struct FlagKey { const wchar_t* key; bool Settings::* field; };
struct NumberKey { const wchar_t* key; int Settings::* field; int low, high; };

const FlagKey kFlags[] = {
    { L"enabled", &Settings::enabled },
    { L"flakesInFront", &Settings::flakesInFront },
    { L"settle", &Settings::settle },
    { L"unlimited", &Settings::unlimited },
    { L"onIcons", &Settings::onIcons },
    { L"onWindows", &Settings::onWindows },
    { L"onTaskbar", &Settings::onTaskbar },
    { L"wind", &Settings::wind },
    { L"sun", &Settings::sun },
    { L"kids", &Settings::kids },
    { L"mousePush", &Settings::mousePush },
    { L"cursorSnow", &Settings::cursorSnow },
    { L"saveEnergy", &Settings::saveEnergy },
    { L"lowCpu", &Settings::lowCpu },
};

const NumberKey kNumbers[] = {
    { L"density", &Settings::density, 20, 300 },
    { L"flakeSize", &Settings::flakeSize, 50, 250 },
    { L"fallSpeed", &Settings::fallSpeed, 40, 250 },
    { L"opacity", &Settings::opacity, 30, 100 },
    { L"windStrength", &Settings::windStrength, 30, 200 },
    { L"settleSpeed", &Settings::settleSpeed, 25, 400 },
};

}

std::wstring defaultSettingsPath() {
    PWSTR path = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path))) dir = path;
    CoTaskMemFree(path);
    dir += L"\\SnowDesk";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\settings.ini";
}

Settings loadSettings(const std::wstring& path) {
    Settings s;
    for (const FlagKey& f : kFlags) {
        s.*f.field = GetPrivateProfileIntW(kSection, f.key, s.*f.field ? 1 : 0, path.c_str()) != 0;
    }
    for (const NumberKey& n : kNumbers) {
        s.*n.field = std::clamp(int(GetPrivateProfileIntW(kSection, n.key, s.*n.field, path.c_str())), n.low, n.high);
    }
    return s;
}

void saveSettings(const Settings& s, const std::wstring& path) {
    for (const FlagKey& f : kFlags) {
        WritePrivateProfileStringW(kSection, f.key, s.*f.field ? L"1" : L"0", path.c_str());
    }
    for (const NumberKey& n : kNumbers) {
        WritePrivateProfileStringW(kSection, n.key, std::to_wstring(s.*n.field).c_str(), path.c_str());
    }
}
