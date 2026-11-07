// Snow simulation for one monitor. Everything here is plain data and has no Win32 dependency,
// so it can run headless in the self-test.
#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include "cursor.h"
#include "weather.h"

constexpr int CELL = 2;           // simulation cell size in screen pixels
constexpr int MAX_PARTICLES = 140000;

enum SolidKind : uint8_t { S_NONE = 0, S_WINDOW = 1, S_ICON = 2, S_GROUND = 3 };

struct Rect {
    int l = 0, t = 0, r = 0, b = 0;
    int w() const { return r - l; }
    int h() const { return b - t; }
    bool operator==(const Rect& o) const { return l == o.l && t == o.t && r == o.r && b == o.b; }
};

struct SolidRect {
    Rect rc;
    uint8_t kind = S_WINDOW;
    bool rounded = false;
};

// Binary silhouette of a desktop icon, one byte per screen pixel.
struct IconShape {
    Rect rc;
    int w = 0, h = 0;
    std::shared_ptr<const std::vector<uint8_t>> mask;
};

struct Particle {
    float x, y, vx, vy;
    float size, alpha, z, phase;
    float age, noHit;
    float meltLeft;                 // a flake that did not stick: seconds until it has melted away
    float swaySin, swayCos, swirlSin, swirlCos;     // of the phase, so no flake needs a sine per frame
    uint8_t grain;      // 1 = loose snow thrown from a pile, 0 = falling flake
    uint8_t behind;     // 1 = behind a window: hidden, and windows do not stop it
    uint8_t pass;       // 1 = slipping past an obstacle instead of landing on it
    uint8_t aimed;      // 1 = spawned above the idle cursor
};

struct Instance { float x, y, size, alpha; };
struct CellBox { int l, t, r, b; };
struct Carried { int x, y; };

// What stops a fall on the desktop layer: the top of the snow, or of the surface when there is none.
struct Landing {
    float y = 0;
    float depth = 0;        // snow to land on, in cells
    uint8_t kind = S_NONE;  // the surface under that snow
};

struct SnowParams {
    float flakesPerMegapixel = 480;
    float accumulateAfter = 2.5f;   // seconds of stillness before the pointer starts collecting snow
    float stickRate = 3;            // cells of snow a landing flake leaves, on average
    float bareRate = 1;             // the same on a bare surface, relative to landing on snow
    float chillDelay = 0;           // seconds before snow can lie anywhere
    float chillRamp = 0;            // seconds more until it lies everywhere; 0 means no waiting
    float fallScale = 1, flakeScale = 1;
    bool settle = true;             // false: flakes fall and vanish, nothing stays
    bool unlimited = false;
    bool onIcons = true, onWindows = true, onGround = true;
    bool flakesOnly = false;        // low CPU mode: flakes drift down and nothing else is simulated
};

class World {
public:
    void init(int pixelW, int pixelH, uint32_t seed);

    void setSolids(const std::vector<SolidRect>& rects, const std::vector<IconShape>& icons);
    void liftAttached(const Rect& oldRect, std::vector<Carried>& out);
    void placeCarried(const std::vector<Carried>& cells, int dxPx, int dyPx, float vx, float vy, float dt);
    void releaseRegion(const Rect& r, float vx, float vy);
    void plow(float x0, float y0, float x1, float y1, float vx, float vy);
    void clearSnow();
    void spawnGrain(float x, float y, float vx, float vy);

    // The snow surface along the bottom of the screen, where the children walk.
    float groundLevel(float px) const;
    bool groundIsVisible(float px) const;
    void scuff(float px);

    // The same for the other places to stand: the top of a window, and icons.
    const std::vector<Rect>& windowTops() const { return tops; }
    const std::vector<Rect>& iconBoxes() const { return boxes; }
    float ledgeLevel(float px, float baseY) const;
    float ledgeDepth(float px, float baseY) const;
    float groundDepth(float px) const;
    bool isClear(float px, float top, float bottom) const;
    Landing landingBelow(float px, float py) const;
    void scuffFrom(float px, float py);
    void heap(float px, float py, int mass);

    // Snow inside a guard stays put until the guard is lifted, whatever the mouse or the sun do.
    void clearGuards() { guards.clear(); }
    void guard(const Rect& pixels);

    void step(float dt, const SnowParams& params, const Weather& weather, SnowCursor* cursor);
    int buildInstances(Instance* out, int maxCount) const;

    int width() const { return pw; }
    int height() const { return ph; }
    int gridW() const { return gw; }
    int gridH() const { return gh; }
    const uint8_t* visibleCells() const { return vis.data(); }
    const uint8_t* solidCells() const { return solid.data(); }
    bool windowAt(float x, float y) const;    // is this pixel inside a window
    const uint8_t* occluderCells() const { return occluder.data(); }
    void takeDirty(std::vector<CellBox>& boxes);
    bool consumeOccluderDirty();

    size_t particleCount() const { return parts.size(); }
    size_t activeCount() const { return active.size(); }
    size_t countSnow() const;
    int countSnowIn(const Rect& r) const;
    size_t countHiddenSnow() const;
    int solidAt(int px, int py) const;

private:
    int pw = 0, ph = 0, gw = 0, gh = 0;
    // Two layers share the grid. The desktop (icons, taskbar) stops everything. Windows float above it:
    // they stop what comes from outside, but behind them the desktop layer carries on, out of sight.
    std::vector<uint8_t> snow, solid, solidNext, window, windowNext, queued, vis;
    std::vector<uint8_t> occluder;      // 255 where a window or taskbar hides everything we draw
    bool occluderDirty = true;
    float cellClock = 0;
    std::vector<uint32_t> active, next;
    std::vector<Particle> parts;
    std::vector<float> capX, capY;      // how deep snow may lie here: some places collect far more
    std::vector<float> sunlit;          // per column: how much the sun reaches right now
    std::vector<float> windBins;        // this frame's wind along the screen
    std::vector<Rect> tops, boxes;      // windows and icons, in pixels
    std::vector<Rect> guards;           // in cells
    float sunTimer = 0, sunPhaseA = 0, sunPhaseB = 0;
    int liveFlakes = 0;
    bool started = false;
    float time = 0, aimTimer = 0;
    float settleClock = 0;          // how long it has been snowing: surfaces cool down with it
    SnowParams current;
    std::vector<uint8_t> tileDirty;
    std::vector<uint32_t> dirtyTiles;
    int tilesAcross = 0;
    bool allDirty = true;
    uint32_t rng = 1;

    uint32_t rnd();
    float rndf() { return (rnd() & 0xFFFFFF) / 16777216.0f; }
    float rndf(float a, float b) { return a + (b - a) * rndf(); }

    bool isOpen(int i, bool insideWindow) const { return !snow[i] && !solid[i] && (insideWindow || !window[i]); }
    bool guarded(int x, int y) const;
    float pileLimit(uint8_t kind, int x, int y) const;
    float stickRate(uint8_t base, bool onSnow, int x) const;
    int columnAt(float px) const;
    int groundRow(int x) const;
    int surfaceRow(int x) const;
    void setCell(int i, uint8_t v);
    void wake(int i);
    void wakeAround(int i);
    void markDirty(int x, int y);
    void takeCell(int i);
    bool canPile(int x, int y, bool insideWindow) const;
    int surfaceNear(int x, int y, bool insideWindow) const;
    int deposit(int x, int y, int mass, bool insideWindow);
    bool depositOne(int x, int y, bool insideWindow);
    void spawnFlake(bool prewarm);
    void stepCells();
    void stepParticles(float dt, const SnowParams& params, const Weather& weather, SnowCursor* cursor);
    void updateSunlight(float dt, const Weather& weather);
    void driftFlakes(float dt, float fallScale);
    void melt(const Weather& weather);
    void blowSnow(const Weather& weather);
};
