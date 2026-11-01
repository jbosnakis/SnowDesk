#pragma once
#include <windows.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "world.h"

struct DesktopIcon {
    std::wstring key;
    Rect rc;            // screen coordinates of the icon image
    int w = 0, h = 0;
    std::shared_ptr<const std::vector<uint8_t>> mask;
};

// Reads desktop icon positions and shapes from Explorer on its own thread: every call is cross-process
// COM, which must never stall the animation.
class IconWatcher {
public:
    void start();
    void stop();
    std::shared_ptr<const std::vector<DesktopIcon>> latest();
    unsigned version() const { return ver.load(); }
    void setActive(bool on) { active = on; }    // off while no snow shows: nothing needs the icons
    void poke() { burst = kBurstReads; }        // something may have moved: read now, and again shortly

private:
    static constexpr int kBurstReads = 3;

    void run();
    void waitForNextRead();
    std::thread th;
    std::atomic<bool> quit{ false };
    std::atomic<bool> finished{ false };
    std::atomic<unsigned> ver{ 0 };
    std::atomic<bool> active{ true };
    std::atomic<int> burst{ kBurstReads };
    std::mutex lock;
    std::shared_ptr<const std::vector<DesktopIcon>> current;
};
