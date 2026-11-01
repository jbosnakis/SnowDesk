#include "weather.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float PI = 3.14159265f;
constexpr float SUNRISE = 7.5f, SUNSET = 17.5f;

float smooth(float a, float b, float v) {
    float t = std::clamp((v - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

// 1 from late evening to early morning, 0 during the day.
float nightAt(float hour) {
    return std::max(smooth(18.0f, 21.0f, hour), 1 - smooth(5.0f, 7.5f, hour));
}

float sunAt(float hour) {
    if (hour <= SUNRISE || hour >= SUNSET) return 0;
    return std::sin((hour - SUNRISE) / (SUNSET - SUNRISE) * PI);
}
}

float Weather::rnd(float a, float b) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return a + (b - a) * ((rng & 0xFFFFFF) / 16777216.0f);
}

void Weather::init(uint32_t seed) {
    rng = seed ? seed : 0x2F6E2B1u;
    prevailingTarget = rnd(0, 1) < 0.5f ? -14.0f : 14.0f;
}

void Weather::configure(float scale, bool sunShines, bool steadySnow) {
    windScale = scale;
    sunOn = sunShines;
    steady = steadySnow;
}

void Weather::startStorm(float seconds) {
    stormLeft = seconds;
    gustWait = 0;
}

void Weather::step(float dt, float hourOfDay) {
    time += dt;
    hourNow = hourOfDay;
    night = nightAt(hourOfDay);
    sunlight = sunOn ? sunAt(hourOfDay) : 0;
    stormLeft = std::max(stormLeft - dt, 0.0f);
    bool storm = stormLeft > 0;

    // The prevailing wind wanders and now and then crosses over to the other side.
    prevailingTimer -= dt;
    if (prevailingTimer <= 0) {
        prevailingTimer = rnd(35, 130);
        float side = rnd(0, 1) < 0.5f ? -1.0f : 1.0f;
        prevailingTarget = side * rnd(6, 38) * (1 + 0.7f * night);
    }
    prevailing += (prevailingTarget - prevailing) * std::min(1.0f, dt / 10);

    if (steady) {
        gustNow = 0;
        gustLength = 0;
        strength = 0;
        fall = 1;
        return;
    }

    if (gustLength > 0) {
        gustElapsed += dt;
        if (gustElapsed >= gustLength) {
            gustLength = 0;
            gustNow = 0;
            gustWait = storm ? rnd(0.5f, 3) : rnd(10, 50) * (1 - 0.55f * night);
        } else {
            float envelope = std::sin(PI * gustElapsed / gustLength);
            float flutter = 1 + 0.22f * std::sin(time * 3.1f) + 0.13f * std::sin(time * 7.3f);
            gustNow = gustPeak * envelope * envelope * flutter;
        }
    } else {
        gustWait -= dt;
        if (gustWait <= 0) {
            // Gusts usually follow the prevailing wind, but not always.
            float side = (prevailingTarget >= 0) == (rnd(0, 1) < 0.68f) ? 1.0f : -1.0f;
            gustPeak = side * rnd(90, 230) * (1 + 1.1f * night) * (storm ? 1.5f : 1);
            gustLength = rnd(4, 13) * (1 + 0.4f * night);
            gustElapsed = 0;
        }
    }
    strength = std::clamp(std::fabs(gustNow) * windScale / 340.0f, 0.0f, 1.0f);

    float swell = 0.5f * std::sin(time * 0.013f + 1.7f) + 0.3f * std::sin(time * 0.041f);
    fall = std::clamp(0.85f + 0.6f * swell + 0.5f * strength, 0.25f, 1.7f) * (1 - 0.35f * sunlight) * (storm ? 2.4f : 1);
}

// A gust is a front moving across the screen, so it does not hit everywhere at the same moment.
float Weather::windAt(float x) const {
    float side = gustNow >= 0 ? 1.0f : -1.0f;
    return windScale * (prevailing + gustNow * (0.72f + 0.28f * std::sin(x * 0.0021f - side * time * 1.1f)));
}
