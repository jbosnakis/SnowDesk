// What the user can choose. Percentages are relative to the normal look.
#pragma once
#include <string>

struct Settings {
    bool enabled = true;

    int density = 100;
    int flakeSize = 100;
    int fallSpeed = 100;
    int opacity = 100;
    bool flakesInFront = false;     // flakes also fall over windows, and snow stays on with one maximized

    bool settle = true;             // false: it only snows, nothing stays
    int settleSpeed = 100;
    bool unlimited = false;
    bool onIcons = true;
    bool onWindows = true;
    bool onTaskbar = true;

    bool wind = true;
    int windStrength = 100;
    bool sun = true;

    bool kids = true;
    bool mousePush = true;
    bool cursorSnow = true;

    bool saveEnergy = false;        // 60 frames per second instead of 120
    bool lowCpu = false;            // only soft, steady flakes: none of the rest even runs
};

std::wstring defaultSettingsPath();
Settings loadSettings(const std::wstring& path);
void saveSettings(const Settings& settings, const std::wstring& path);
