// The weather over the desktop, driven by the clock: a prevailing wind that changes sides, gusts that
// grow stronger at night, snowfall that comes and goes, and the sun during the day. No Win32.
#pragma once
#include <cstdint>

class Weather {
public:
    void init(uint32_t seed);
    void configure(float windScale, bool sunShines, bool steadySnow);
    void startStorm(float seconds);
    void step(float dt, float hourOfDay);

    float windAt(float x) const;            // pixels per second, positive towards the right
    float wind() const { return (prevailing + gustNow) * windScale; }
    float gustStrength() const { return strength; }     // 0 calm .. 1 storm
    float snowfall() const { return fall; }             // multiplies the flake count
    float sun() const { return sunlight; }              // 0 at night .. 1 at noon
    float hour() const { return hourNow; }

private:
    float rnd(float a, float b);

    uint32_t rng = 1;
    float time = 0, hourNow = 12;
    float night = 0, sunlight = 0;
    float prevailing = 0, prevailingTarget = 12, prevailingTimer = 0;
    float gustPeak = 0, gustLength = 0, gustElapsed = 0, gustWait = 8, gustNow = 0;
    float strength = 0, fall = 1;
    float windScale = 1;
    bool sunOn = true;
    bool steady = false;
    float stormLeft = 0;
};
