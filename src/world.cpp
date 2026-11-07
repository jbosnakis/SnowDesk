#include "world.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr float GRAVITY = 620.0f;
constexpr int MAX_CELLS_PER_FRAME = 24000;
constexpr int MAX_PLOW_PER_FRAME = 1600;

inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

inline uint32_t xorshift(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

// Height of a pile, in cells, for each kind of surface.
inline float pileCap(uint8_t kind) {
    switch (kind) {
    case S_GROUND: return 20;
    case S_ICON: return 6;
    default: return 11;
    }
}

// Chance that a flake reaching this surface stays there. The rest slip past behind the window and
// carry on below it, so the snow keeps falling on whatever the window leaves uncovered.
// Icons stack in rows: a fixed chance would let the top row take nearly everything, so it depends on
// how many rows are still below, and every row ends up with about the same share.
inline float landChance(uint8_t kind, float rowsBelow) {
    switch (kind) {
    case S_GROUND: return 1.0f;
    case S_ICON: return std::min(1.6f / std::max(rowsBelow, 1.0f), 1.0f);
    default: return 0.65f;
    }
}

constexpr float ICON_ROW_PIXELS = 98.0f;
constexpr int WIND_BIN_PIXELS = 64;
constexpr int TILE_W = 64, TILE_H = 32;     // in cells: the pieces of the grid sent to the graphics card
constexpr float ICON_STICK_SCALE = 1.75f;   // icons are small targets and see few flakes each
constexpr float MELT_ON_CONTACT = 0.7f;     // seconds a flake that did not stick stays visible
constexpr int MAX_PILE_DEPTH = 470;
constexpr int STEADY_MELT_TRIES = 12;
constexpr int SUN_MELT_TRIES = 1500;
constexpr int OVER_LIMIT_TRIES = 700;   // snow left above a limit that was just lowered
constexpr float UNLIMITED_SCREEN_SHARE = 0.62f;
constexpr float UNLIMITED_SURFACE_SCALE = 3.5f;
constexpr int WIND_LIFT_TRIES = 1400;
constexpr float SUN_MAX_THAW = 0.92f;   // share of a pile that full sunshine takes away

inline void setPhase(Particle& p, float phase) {
    p.phase = phase;
    p.swaySin = std::sin(phase);
    p.swayCos = std::cos(phase);
    p.swirlSin = std::sin(phase * 3);
    p.swirlCos = std::cos(phase * 3);
}

inline float smoothRamp(float a, float b, float v) {
    float t = clampf((v - a) / (b - a), 0, 1);
    return t * t * (3 - 2 * t);
}
}

uint32_t World::rnd() { return xorshift(rng); }

void World::init(int pixelW, int pixelH, uint32_t seed) {
    pw = pixelW;
    ph = pixelH;
    gw = pixelW / CELL;
    gh = pixelH / CELL;
    rng = seed ? seed : 0x9E3779B9u;
    size_t n = size_t(gw) * gh;
    snow.assign(n, 0);
    solid.assign(n, 0);
    solidNext.assign(n, 0);
    window.assign(n, 0);
    windowNext.assign(n, 0);
    queued.assign(n, 0);
    vis.assign(n, 0);
    occluder.assign(n, 0);
    occluderDirty = true;
    tilesAcross = (gw + TILE_W - 1) / TILE_W;
    tileDirty.assign(size_t(tilesAcross) * ((gh + TILE_H - 1) / TILE_H), 0);
    dirtyTiles.clear();
    active.clear();
    next.clear();
    parts.clear();
    parts.reserve(MAX_PARTICLES);
    liveFlakes = 0;

    // Long swells decide where drifts form and where the ground stays almost bare, short ones roughen them.
    capX.resize(gw);
    float p1 = rndf(0, 6.28f), p2 = rndf(0, 6.28f), p3 = rndf(0, 6.28f), p4 = rndf(0, 6.28f);
    for (int x = 0; x < gw; x++) {
        float v = 0.46f * std::sin(x * 0.0061f + p1) + 0.3f * std::sin(x * 0.017f + p2)
            + 0.18f * std::sin(x * 0.053f + p3) + 0.06f * std::sin(x * 0.13f + p4);
        capX[x] = clampf(0.85f + 0.95f * v, 0.1f, 1.6f);
    }
    capY.resize(gh);
    float p5 = rndf(0, 6.28f), p6 = rndf(0, 6.28f);
    for (int y = 0; y < gh; y++) {
        capY[y] = clampf(0.85f + 0.3f * std::sin(y * 0.0093f + p5) + 0.25f * std::sin(y * 0.027f + p6), 0.3f, 1.4f);
    }
    sunlit.assign(gw, 0);
    sunTimer = 0;
    sunPhaseA = rndf(0, 6.28f);
    sunPhaseB = rndf(0, 6.28f);
    allDirty = true;
}

void World::markDirty(int x, int y) {
    if (allDirty) return;
    int tile = y / TILE_H * tilesAcross + x / TILE_W;
    if (tileDirty[tile]) return;
    tileDirty[tile] = 1;
    dirtyTiles.push_back(uint32_t(tile));
}

bool World::consumeOccluderDirty() {
    bool was = occluderDirty;
    occluderDirty = false;
    return was;
}

// What changed since the last call. With many pieces changed, one upload of everything is cheaper.
void World::takeDirty(std::vector<CellBox>& boxes) {
    boxes.clear();
    if (allDirty || dirtyTiles.size() * 4 > tileDirty.size()) {
        boxes.push_back({ 0, 0, gw, gh });
    } else {
        for (uint32_t tile : dirtyTiles) {
            int x = int(tile) % tilesAcross * TILE_W, y = int(tile) / tilesAcross * TILE_H;
            boxes.push_back({ x, y, std::min(x + TILE_W, gw), std::min(y + TILE_H, gh) });
        }
    }
    for (uint32_t tile : dirtyTiles) tileDirty[tile] = 0;
    dirtyTiles.clear();
    allDirty = false;
}

void World::setCell(int i, uint8_t v) {
    snow[i] = v;
    vis[i] = solid[i] ? 0 : v;
    int y = i / gw;
    markDirty(i - y * gw, y);
}

void World::wake(int i) {
    if (i < 0 || size_t(i) >= snow.size() || !snow[i] || queued[i]) return;
    queued[i] = 1;
    next.push_back(uint32_t(i));
}

void World::wakeAround(int i) {
    int x = i % gw;
    wake(i - gw);
    if (x > 0) { wake(i - 1); wake(i - gw - 1); }
    if (x + 1 < gw) { wake(i + 1); wake(i - gw + 1); }
}

void World::setSolids(const std::vector<SolidRect>& rects, const std::vector<IconShape>& icons) {
    std::fill(solidNext.begin(), solidNext.end(), uint8_t(0));
    std::fill(windowNext.begin(), windowNext.end(), uint8_t(0));
    tops.clear();
    boxes.clear();

    auto fillRow = [&](std::vector<uint8_t>& layer, int y, int x0, int x1, uint8_t value) {
        if (y < 0 || y >= gh) return;
        x0 = std::max(x0, 0);
        x1 = std::min(x1, gw);
        if (x1 > x0) std::memset(&layer[size_t(y) * gw + x0], value, x1 - x0);
    };

    for (const IconShape& ic : icons) {
        if (!ic.mask) continue;
        boxes.push_back(ic.rc);
        const std::vector<uint8_t>& m = *ic.mask;
        for (int py = 0; py < ic.h; py++) {
            for (int px = 0; px < ic.w; px++) {
                if (!m[size_t(py) * ic.w + px]) continue;
                int cx = (ic.rc.l + px) / CELL, cy = (ic.rc.t + py) / CELL;
                if (cx >= 0 && cy >= 0 && cx < gw && cy < gh) solidNext[size_t(cy) * gw + cx] = S_ICON;
            }
        }
    }
    for (const SolidRect& s : rects) {
        if (s.kind == S_WINDOW) tops.push_back(s.rc);
        int x0 = s.rc.l / CELL, x1 = (s.rc.r + CELL - 1) / CELL;
        int y0 = s.rc.t / CELL, y1 = (s.rc.b + CELL - 1) / CELL;
        for (int y = y0; y < y1; y++) {
            int inset = 0;
            if (s.rounded) inset = (y == y0) ? 3 : (y == y0 + 1 ? 1 : 0);
            if (s.kind == S_WINDOW) fillRow(windowNext, y, x0 + inset, x1 - inset, 1);
            else fillRow(solidNext, y, x0 + inset, x1 - inset, s.kind);
        }
    }
    fillRow(solidNext, gh - 1, 0, gw, S_GROUND);

    solid.swap(solidNext);
    window.swap(windowNext);

    const size_t n = snow.size();
    uint8_t* v = vis.data();
    uint8_t* oc = occluder.data();
    const uint8_t* sn = snow.data();
    const uint8_t* so = solid.data();
    const uint8_t* wi = window.data();
    for (size_t i = 0; i < n; i++) {
        v[i] = so[i] ? 0 : sn[i];
        oc[i] = (wi[i] || so[i] == S_GROUND) ? 255 : 0;
    }
    occluderDirty = true;
    allDirty = true;

    // Anything a moved surface stopped holding up has to be looked at again.
    for (size_t i = 0; i + gw < n; i++) {
        if (sn[i] && !so[i] && isOpen(int(i + gw), wi[i] != 0)) wake(int(i));
    }
}
void World::liftAttached(const Rect& r, std::vector<Carried>& out) {
    int x0 = std::max(r.l / CELL, 0), x1 = std::min((r.r + CELL - 1) / CELL, gw);
    int top = r.t / CELL - 1;
    if (top < 0) return;
    for (int x = x0; x < x1; x++) {
        int y = std::min(top + 2, gh - 1);
        int guard = 0;
        while (y >= 0 && window[size_t(y) * gw + x] && guard++ < 4) y--;
        if (y < 0 || y > top + 2) continue;
        int n = 0;
        while (y >= 0 && n < 64) {
            int i = y * gw + x;
            if (!snow[i] || solid[i] || window[i]) break;
            setCell(i, 0);
            wakeAround(i);
            out.push_back({ x, y });
            y--;
            n++;
        }
    }
}

void World::placeCarried(const std::vector<Carried>& cells, int dxPx, int dyPx, float vx, float vy, float dt) {
    float speed = std::sqrt(vx * vx + vy * vy);
    float frames = dt * 60;     // the chances below are per 1/60 s of movement
    float throwChance = clampf(speed / 5200.0f, 0.012f, 0.42f) * frames;
    float slipChance = clampf(speed / 420.0f, 0.0f, 0.9f) * frames;
    int dx = dxPx / CELL, dy = dyPx / CELL;
    int slipDir = vx > 0 ? -1 : 1;
    if (std::fabs(vx) < 20) slipChance = 0;

    for (const Carried& c : cells) {
        if (rndf() < throwChance) {
            spawnGrain(c.x * float(CELL) + 1, c.y * float(CELL) + 1,
                -vx * rndf(0.2f, 0.5f) + rndf(-45, 45),
                -std::fabs(vy) * rndf(0.1f, 0.35f) + rndf(-90, -10));
            continue;
        }
        // The heap drags behind the window, so it creeps towards the trailing edge and falls off.
        int nx = c.x + dx + (rndf() < slipChance ? slipDir * (1 + int(rnd() % 2)) : 0);
        int ny = c.y + dy;
        bool placed = false;
        if (nx >= 0 && nx < gw) {
            for (int up = 0; up < 3 && ny - up >= 0; up++) {
                int y = ny - up;
                if (y >= gh) break;
                int i = y * gw + nx;
                if (isOpen(i, false)) {
                    setCell(i, 255);
                    wake(i);
                    placed = true;
                    break;
                }
            }
        }
        if (!placed) spawnGrain(c.x * float(CELL) + 1, c.y * float(CELL) + 1, -vx * 0.3f + rndf(-30, 30), rndf(-70, 0));
    }
}

void World::takeCell(int i) {
    setCell(i, 0);
    wakeAround(i);
}

bool World::windowAt(float x, float y) const {
    if (x < 0 || y < 0 || x >= pw || y >= ph) return false;
    int cx = std::min(int(x) / CELL, gw - 1), cy = std::min(int(y) / CELL, gh - 1);
    return window[size_t(cy) * gw + cx] != 0;
}

void World::spawnGrain(float x, float y, float vx, float vy) {
    if (parts.size() >= MAX_PARTICLES) return;
    Particle p{};
    p.x = clampf(x, 0, float(pw) - 0.5f);    // the pointer and the children can throw from past the edge
    p.y = y;
    p.vx = vx;
    p.vy = vy;
    p.size = rndf(2.2f, 3.6f);
    p.alpha = 0.95f;
    p.z = 1;
    p.phase = rndf(0, 6.28f);
    p.age = 1;
    p.grain = 1;
    p.behind = windowAt(x, y) ? 1 : 0;
    parts.push_back(p);
}

void World::releaseRegion(const Rect& r, float vx, float vy) {
    int x0 = std::max(r.l / CELL, 0), x1 = std::min((r.r + CELL - 1) / CELL, gw);
    int y0 = std::max(r.t / CELL, 0), y1 = std::min((r.b + CELL - 1) / CELL, gh);
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            int i = y * gw + x;
            if (!snow[i] || solid[i]) continue;
            takeCell(i);
            spawnGrain(x * float(CELL) + 1, y * float(CELL) + 1, vx + rndf(-60, 60), vy + rndf(-130, -10));
        }
    }
}

void World::plow(float x0, float y0, float x1, float y1, float vx, float vy) {
    float speed = std::sqrt(vx * vx + vy * vy);
    if (speed < 25) return;
    float radius = 13.0f + clampf(speed / 90.0f, 0, 15);
    float dx = x1 - x0, dy = y1 - y0;
    float len2 = dx * dx + dy * dy;
    float dirX = vx / speed, dirY = vy / speed;
    float push = clampf(speed, 0, 1500);

    int cx0 = std::max(int(std::min(x0, x1) - radius) / CELL, 0);
    int cx1 = std::min(int(std::max(x0, x1) + radius) / CELL + 1, gw);
    int cy0 = std::max(int(std::min(y0, y1) - radius) / CELL, 0);
    int cy1 = std::min(int(std::max(y0, y1) + radius) / CELL + 1, gh);
    int taken = 0;
    for (int y = cy0; y < cy1 && taken < MAX_PLOW_PER_FRAME; y++) {
        for (int x = cx0; x < cx1 && taken < MAX_PLOW_PER_FRAME; x++) {
            int i = y * gw + x;
            if (!snow[i] || solid[i] || window[i]) continue;     // the mouse cannot reach behind a window
            if (guarded(x, y)) continue;
            float px = x * float(CELL) + 1, py = y * float(CELL) + 1;
            float t = len2 > 0 ? clampf(((px - x0) * dx + (py - y0) * dy) / len2, 0, 1) : 0;
            float qx = x0 + dx * t, qy = y0 + dy * t;
            float nx = px - qx, ny = py - qy;
            float d = std::sqrt(nx * nx + ny * ny);
            if (d > radius) continue;
            if (d > 0.01f) { nx /= d; ny /= d; }
            float away = rndf(50, 220);
            takeCell(i);
            spawnGrain(px, py,
                dirX * push * rndf(0.25f, 0.6f) + nx * away,
                dirY * push * rndf(0.25f, 0.6f) + ny * away - rndf(30, 170));
            taken++;
        }
    }
}

void World::clearSnow() {
    std::vector<int> kept;
    for (const Rect& g : guards) {
        for (int y = g.t; y < g.b; y++) {
            for (int x = g.l; x < g.r; x++) {
                int i = y * gw + x;
                if (snow[i]) kept.push_back(i);
            }
        }
    }
    std::fill(snow.begin(), snow.end(), uint8_t(0));
    std::fill(vis.begin(), vis.end(), uint8_t(0));
    std::fill(queued.begin(), queued.end(), uint8_t(0));
    active.clear();
    next.clear();
    parts.erase(std::remove_if(parts.begin(), parts.end(), [](const Particle& p) { return p.grain != 0; }), parts.end());
    for (int i : kept) setCell(i, 255);
    allDirty = true;
}

void World::guard(const Rect& r) {
    Rect g{ std::max(r.l / CELL, 0), std::max(r.t / CELL, 0), std::min(r.r / CELL + 1, gw), std::min(r.b / CELL + 1, gh) };
    if (g.r > g.l && g.b > g.t) guards.push_back(g);
}

bool World::guarded(int x, int y) const {
    for (const Rect& g : guards) {
        if (x >= g.l && x < g.r && y >= g.t && y < g.b) return true;
    }
    return false;
}

int World::countSnowIn(const Rect& r) const {
    int x0 = std::max(r.l / CELL, 0), x1 = std::min((r.r + CELL - 1) / CELL, gw);
    int y0 = std::max(r.t / CELL, 0), y1 = std::min((r.b + CELL - 1) / CELL, gh);
    int n = 0;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) n += vis[size_t(y) * gw + x] ? 1 : 0;
    }
    return n;
}

size_t World::countHiddenSnow() const {
    size_t n = 0;
    for (size_t i = 0; i < snow.size(); i++) n += snow[i] && window[i] ? 1 : 0;
    return n;
}

size_t World::countSnow() const {
    size_t n = 0;
    for (uint8_t v : snow) n += v ? 1 : 0;
    return n;
}

int World::solidAt(int px, int py) const {
    if (px < 0 || py < 0 || px >= pw || py >= ph) return S_NONE;
    int cx = std::min(px / CELL, gw - 1), cy = std::min(py / CELL, gh - 1);
    size_t i = size_t(cy) * gw + cx;
    return window[i] ? uint8_t(S_WINDOW) : solid[i];
}

// The grid column under a pixel. The last one can be past the grid on a screen of odd width.
int World::columnAt(float px) const {
    return std::clamp(int(clampf(px, 0, float(pw - 1))) / CELL, 0, gw - 1);
}

int World::groundRow(int x) const {
    int y = gh - 1;
    while (y > 0 && solid[size_t(y) * gw + x] == S_GROUND) y--;
    return y;
}

int World::surfaceRow(int x) const {
    int y = groundRow(x), depth = 0;
    while (y > 0 && snow[size_t(y) * gw + x] && depth++ < MAX_PILE_DEPTH) y--;
    return y;
}

// Averaged over a few columns, so feet do not trip over every grain.
float World::groundLevel(float px) const {
    int center = columnAt(px);
    int sum = 0, count = 0;
    for (int x = center - 8; x <= center + 8; x += 2) {
        if (x < 0 || x >= gw) continue;
        sum += surfaceRow(x);
        count++;
    }
    return count ? float(sum) / count * CELL + CELL : float(ph);
}

bool World::groundIsVisible(float px) const {
    int x = columnAt(px);
    int low = std::max(groundRow(x) - 4, 0), high = std::max(low - 40, 0);
    return !occluder[size_t(low) * gw + x] && !occluder[size_t(high) * gw + x];
}

void World::scuff(float px) {
    int center = int(px) / CELL;
    for (int x = center - 1; x <= center + 1; x++) {
        if (x < 0 || x >= gw) continue;
        int y = surfaceRow(x) + 1;
        int i = y * gw + x;
        if (y < gh && snow[i] && !solid[i]) takeCell(i);
    }
}

// The top of the snow on a window whose edge is at baseY, averaged like groundLevel.
float World::ledgeLevel(float px, float baseY) const {
    int center = columnAt(px);
    int base = int(baseY) / CELL - 1;
    if (base < 0 || base >= gh) return baseY;
    int sum = 0, count = 0;
    for (int x = center - 8; x <= center + 8; x += 2) {
        if (x < 0 || x >= gw) continue;
        int y = base, depth = 0;
        while (y > 0 && snow[size_t(y) * gw + x] && depth++ < MAX_PILE_DEPTH) y--;
        sum += y;
        count++;
    }
    return count ? float(sum) / count * CELL + CELL : baseY;
}

float World::ledgeDepth(float px, float baseY) const {
    int x = columnAt(px);
    int y = int(baseY) / CELL - 1, depth = 0;
    while (y >= 0 && y < gh && snow[size_t(y) * gw + x] && depth < MAX_PILE_DEPTH) { y--; depth++; }
    return float(depth);
}

float World::groundDepth(float px) const {
    int x = columnAt(px);
    return float(groundRow(x) - surfaceRow(x));
}

// True when no window, icon or taskbar touches this column between the two heights.
bool World::isClear(float px, float top, float bottom) const {
    if (top < 0 || px < 0 || px >= pw) return false;
    int x = std::min(int(px) / CELL, gw - 1);
    int y1 = std::min(int(bottom) / CELL, gh - 1);
    for (int y = int(top) / CELL; y <= y1; y++) {
        size_t i = size_t(y) * gw + x;
        if (window[i] || solid[i]) return false;
    }
    return true;
}

Landing World::landingBelow(float px, float py) const {
    int x = columnAt(px);
    int y = std::clamp(int(py) / CELL, 0, gh - 1);
    auto empty = [&](int cx, int cy) { size_t i = size_t(cy) * gw + cx; return !solid[i] && !snow[i]; };
    while (y < gh - 1 && empty(x, y)) y++;

    Landing landing;
    landing.y = float(y * CELL);
    int base = y;
    while (base < gh - 1 && !solid[size_t(base) * gw + x]) base++;
    landing.kind = solid[size_t(base) * gw + x];

    // Measured across a body's width: one tall grain is nothing to land on.
    int total = 0, count = 0;
    for (int cx = x - 6; cx <= x + 6; cx += 3) {
        if (cx < 0 || cx >= gw) continue;
        int cy = std::max(y - 10, 0), end = std::min(y + 20, gh - 1);
        while (cy < end && empty(cx, cy)) cy++;
        int depth = 0;
        while (cy < gh - 1 && snow[size_t(cy) * gw + cx] && !solid[size_t(cy) * gw + cx]) { cy++; depth++; }
        total += depth;
        count++;
    }
    landing.depth = count ? float(total) / count : 0;
    return landing;
}

// Takes the first snow found below py, which is how a child scoops some up from a ledge or an icon.
void World::scuffFrom(float px, float py) {
    int center = int(px) / CELL, top = int(py) / CELL;
    for (int x = center - 1; x <= center + 1; x++) {
        if (x < 0 || x >= gw) continue;
        for (int y = std::max(top, 0); y < std::min(top + 40, gh); y++) {
            int i = y * gw + x;
            if (solid[i]) break;
            if (snow[i]) { takeCell(i); break; }
        }
    }
}

// An armful of snow dropped here, heaped up in the middle.
void World::heap(float px, float py, int mass) {
    int center = int(px) / CELL, row = std::max(int(py) / CELL - 30, 0);
    for (int k = 0; k < mass; k++) {
        int x = center + int(rnd() % 9) + int(rnd() % 9) - 8;
        depositOne(x, row, true);
    }
}

// Cells a flake leaves where it lands. Bare surfaces start out too warm: nothing lies at first, then it
// takes hold in the coldest patches. Snow already lying keeps the next flakes, so patches spread from there.
float World::stickRate(uint8_t base, bool onSnow, int x) const {
    float rate = current.stickRate * (base == S_ICON ? ICON_STICK_SCALE : 1.0f);
    if (onSnow) return rate;
    rate *= current.bareRate;
    if (current.chillRamp <= 0) return rate;
    float chill = clampf((settleClock - current.chillDelay) / current.chillRamp, 0, 1);
    float patch = clampf((capX[x] - 0.1f) / 1.5f, 0, 1);    // where drifts run deep, snow also lies first
    return rate * smoothRamp(0, 0.35f, chill - (1 - patch) * 0.65f);
}

float World::pileLimit(uint8_t kind, int x, int y) const {
    if (kind == S_ICON ? !current.onIcons : kind == S_GROUND ? !current.onGround : !current.onWindows) return 0;
    if (current.unlimited) {
        if (kind == S_GROUND) return gh * UNLIMITED_SCREEN_SHARE * (0.6f + 0.25f * capX[x]);
        return pileCap(kind) * UNLIMITED_SURFACE_SCALE * (0.6f + 0.25f * capX[x]);
    }
    return pileCap(kind) * capX[x] * capY[y] * (1 - SUN_MAX_THAW * sunlit[x]);
}

bool World::canPile(int x, int y, bool insideWindow) const {
    int depth = 0;
    for (int yy = y + 1; yy < gh && depth < MAX_PILE_DEPTH; yy++) {
        int i = yy * gw + x;
        if (solid[i]) return depth < pileLimit(solid[i], x, yy);
        if (window[i] && !insideWindow) return depth < pileLimit(S_WINDOW, x, yy);
        if (!snow[i]) return true;
        depth++;
    }
    return false;
}

bool World::depositOne(int x, int y, bool insideWindow) {
    if (x < 0 || x >= gw || y < 0 || y >= gh) return false;
    int i = y * gw + x;
    if (!isOpen(i, insideWindow)) return false;
    while (y + 1 < gh && isOpen((y + 1) * gw + x, insideWindow)) y++;
    i = y * gw + x;
    setCell(i, 255);
    wake(i);
    return true;
}

// Row where snow would rest in this column near height y, or -1 past a wall or over an edge.
int World::surfaceNear(int x, int y, bool insideWindow) const {
    if (x < 0 || x >= gw) return -1;
    int wall = std::min(y, gh - 1);
    y = std::max(y - 2, 0);
    while (y < wall && !isOpen(y * gw + x, insideWindow)) y++;
    if (!isOpen(y * gw + x, insideWindow)) return -1;
    for (int n = 0; n < 7 && y + 1 < gh; n++, y++) {
        if (!isOpen((y + 1) * gw + x, insideWindow)) return y;
    }
    return -1;
}
// Fresh snow fills the hollows nearby first, which gives a blanket instead of a row of spikes.
int World::deposit(int x, int y, int mass, bool insideWindow) {
    int placed = 0;
    for (int k = 0; k < mass; k++) {
        int startX = x + (k ? int(rnd() % 5) - 2 : 0);
        int startY = surfaceNear(startX, y + 1, insideWindow);
        if (startY < 0) continue;
        int bestX = startX, bestY = startY;
        int dir = (rnd() & 1) ? 1 : -1;
        for (int side = 0; side < 2; side++, dir = -dir) {
            int cur = startY;
            for (int d = 1; d <= 12; d++) {
                int sy = surfaceNear(startX + dir * d, cur, insideWindow);
                // A steeper drop is the flank of a round icon, not a hollow: snow placed there slides off.
                if (sy < 0 || sy - cur > 1) break;
                if (sy > bestY) { bestY = sy; bestX = startX + dir * d; }
                cur = sy;
            }
        }
        if (canPile(bestX, bestY, insideWindow) && depositOne(bestX, bestY, insideWindow)) placed++;
    }
    return placed;
}

void World::stepCells() {
    active.swap(next);
    next.clear();
    int budget = MAX_CELLS_PER_FRAME;
    for (size_t k = 0; k < active.size(); k++) {
        int c = int(active[k]);
        if (budget-- <= 0) {
            // Out of budget: keep the rest for the next frame.
            next.push_back(uint32_t(c));
            continue;
        }
        queued[c] = 0;
        if (!snow[c] || solid[c]) continue;
        int y = c / gw, x = c - y * gw;
        if (y + 1 >= gh) continue;
        int below = c + gw;
        bool inside = window[c] != 0;
        if (isOpen(below, inside)) {
            takeCell(c);
            spawnGrain(x * float(CELL) + 1, y * float(CELL) + 1, rndf(-14, 14), 0);
            continue;
        }
        int s = (rnd() & 1) ? 1 : -1;
        for (int t = 0; t < 2; t++, s = -s) {
            int nx = x + s;
            if (nx < 0 || nx >= gw) continue;
            int side = c + s;
            if (!isOpen(side, inside) || !isOpen(side + gw, inside)) continue;
            // Snow touching a surface clings to it and only lets go on a really steep slope.
            if ((solid[below] || (window[below] && !inside)) && y + 2 < gh && !isOpen(side + 2 * gw, inside)) continue;
            takeCell(c);
            int to = side + gw;
            setCell(to, 255);
            wake(to);
            break;
        }
    }
}

void World::spawnFlake(bool prewarm) {
    if (parts.size() >= MAX_PARTICLES) return;
    Particle p{};
    float z = rndf();
    z = 0.25f + 0.75f * z * z;
    p.z = z;
    p.x = rndf(0, float(pw));
    p.y = prewarm ? -rndf(10, float(ph)) : -rndf(6, 50);
    p.vy = 34 + 74 * z;
    p.size = 1.7f + 3.1f * z;
    p.alpha = 0.34f + 0.5f * z;
    setPhase(p, rndf(0, 6.28f));
    p.age = 0;
    parts.push_back(p);
    liveFlakes++;
}

void World::stepParticles(float dt, const SnowParams& params, const Weather& weather, SnowCursor* cursor) {
    float cursorSpeed = cursor && cursor->visible ? cursor->speed() : 0;
    bool cursorCollects = cursor && cursor->visible && !cursor->overWindow && cursor->idle > params.accumulateAfter;
    uint32_t cursorRng = rnd() | 1;
    float gust = weather.gustStrength();

    float swayS = std::sin(time * 1.15f), swayC = std::cos(time * 1.15f);
    float swirlS = std::sin(time * 2.3f), swirlC = std::cos(time * 2.3f);
    windBins.resize(size_t(pw / WIND_BIN_PIXELS) + 2);
    for (size_t b = 0; b < windBins.size(); b++) windBins[b] = weather.windAt(float(b * WIND_BIN_PIXELS));

    for (size_t k = 0; k < parts.size();) {
        Particle& p = parts[k];
        p.age += dt;
        p.noHit = std::max(p.noHit - dt, 0.0f);
        if (p.meltLeft > 0) {
            p.meltLeft -= dt;
            if (p.meltLeft > 0) {
                k++;
            } else {
                liveFlakes--;
                parts[k] = parts.back();
                parts.pop_back();
            }
            continue;
        }
        bool dead = false;

        float along = clampf(p.x, 0, float(pw - 1)) / WIND_BIN_PIXELS;
        int bin = int(along);
        float wind = windBins[bin] + (windBins[size_t(bin) + 1] - windBins[bin]) * (along - bin);
        if (p.grain) {
            p.vy = std::min(p.vy + GRAVITY * dt, 560.0f);
            p.vx += (wind * 0.5f - p.vx) * std::min(1.0f, 1.9f * dt);
        } else {
            // In a gust flakes swirl, slow their fall and sometimes get lifted for a moment.
            float targetX = wind * (0.55f + p.z) + (12.0f + 42.0f * gust) * p.z * (swayS * p.swayCos + swayC * p.swaySin);
            float targetY = (34 + 74 * p.z) * params.fallScale * (1 - 0.25f * gust) + 42.0f * gust * (swirlS * p.swirlCos + swirlC * p.swirlSin);
            if (p.aimed) targetX = 2.0f * std::sin(time * 1.7f + p.phase);
            float response = std::min(1.0f, (2.2f + 2.4f * gust) * dt);
            p.vx += (targetX - p.vx) * response;
            p.vy += (targetY - p.vy) * response;

            if (cursorSpeed > 90) {
                float dx = p.x - cursor->x, dy = p.y - cursor->y;
                float d2 = dx * dx + dy * dy;
                if (d2 < 90 * 90) {
                    float f = (1 - d2 / (90 * 90)) * 3.4f * dt;
                    p.vx += cursor->vx * f;
                    p.vy += cursor->vy * f;
                }
            }
        }

        float mx = p.vx * dt, my = p.vy * dt;
        int steps = int(std::max(std::fabs(mx), std::fabs(my)) / CELL) + 1;
        float sx = mx / steps, sy = my / steps;
        for (int s = 0; s < steps && !dead; s++) {
            float nx = p.x + sx, ny = p.y + sy;

            if (nx < 0 || nx >= pw) {
                if (p.grain) { sx = -sx * 0.4f; p.vx = -p.vx * 0.4f; nx = p.x; }
                else nx = nx < 0 ? nx + pw : nx - pw;
                nx = clampf(nx, 0, float(pw) - 0.5f);
            }
            if (ny >= ph) { dead = true; break; }

            bool canTouchCursor = !p.grain && p.noHit <= 0 && (!p.behind || p.aimed) && !(cursor && cursor->overWindow);
            if (canTouchCursor && cursor && cursor->visible && cursor->hit(nx, ny)) {
                if (cursorCollects) {
                    cursor->drop(int(nx - cursor->originX()), cursorRng);
                    cursor->drop(int(nx - cursor->originX()) + 1, cursorRng);
                    dead = true;
                    break;
                }
                p.vx = (p.vx + cursor->vx * 0.35f) + rndf(-55, 55);
                p.vy = -std::fabs(p.vy) * 0.55f + cursor->vy * 0.25f - rndf(10, 45);
                p.noHit = 0.5f;
                p.aimed = 0;
            }

            if (ny >= 0) {
                int cx = int(nx) / CELL, cy = int(ny) / CELL;
                if (cx >= gw) cx = gw - 1;
                if (cy >= gh) cy = gh - 1;
                int i = cy * gw + cx;
                uint8_t sk = solid[i];
                bool inWindow = window[i] != 0;
                if (!inWindow) p.behind = 0;
                bool hitsWindow = inWindow && !p.behind;
                bool blocked = sk || snow[i] || hitsWindow;

                if (p.pass) {
                    if (sk == S_GROUND) { dead = true; break; }
                    if (!blocked) p.pass = 0;
                } else if (blocked) {
                    int lx = std::clamp(int(p.x) / CELL, 0, gw - 1), ly = std::min(int(p.y) / CELL, gh - 1);
                    if (p.y < 0 || ly < 0) {
                        (hitsWindow ? p.behind : p.pass) = 1;
                    } else if (p.grain) {
                        bool inside = p.behind != 0;
                        int li = ly * gw + lx;
                        if (ly < gh && isOpen(li, inside)) { setCell(li, 255); wake(li); }
                        else if (ly > 0 && isOpen(li - gw, inside)) { setCell(li - gw, 255); wake(li - gw); }
                        dead = true;
                        break;
                    } else {
                        uint8_t base = hitsWindow ? uint8_t(S_WINDOW) : sk;
                        for (int yy = cy; yy < gh && !base; yy++) {
                            int ii = yy * gw + cx;
                            if (solid[ii]) base = solid[ii];
                            else if (window[ii] && !p.behind) base = S_WINDOW;
                            else if (!snow[ii]) break;
                        }
                        float rowsBelow = (ph - ny) / ICON_ROW_PIXELS;
                        float chance = landChance(base, rowsBelow) * (base == S_GROUND ? 1.0f : std::min(capX[lx], 1.0f));
                        if (!params.settle && base == S_GROUND) {
                            dead = true;
                            break;
                        }
                        if (params.settle && rndf() < chance) {
                            float cells = stickRate(base, snow[i] != 0, lx);
                            int mass = int(cells) + (rndf() < cells - int(cells) ? 1 : 0);
                            // A window may stop just short of the taskbar; the snow below it still belongs behind it.
                            bool inside = p.behind || (ly >= 4 && window[size_t(ly - 4) * gw + lx]);
                            // A full surface passes the flake on to whatever is below it.
                            if (mass > 0 && (deposit(lx, ly, mass, inside) > 0 || base == S_GROUND)) {
                                dead = true;
                                break;
                            }
                            if (mass == 0 && (base == S_GROUND || canPile(lx, ly, inside))) {
                                p.meltLeft = MELT_ON_CONTACT;
                                break;
                            }
                        }
                        (hitsWindow ? p.behind : p.pass) = 1;
                    }
                }
            }
            p.x = nx;
            p.y = ny;
        }

        if (dead) {
            if (!p.grain) liveFlakes--;
            parts[k] = parts.back();
            parts.pop_back();
        } else {
            k++;
        }
    }
}

// Low CPU mode: no wind, no surfaces, no pointer. The windows only hide the flakes, in the shader.
void World::driftFlakes(float dt, float fallScale) {
    float swayS = std::sin(time * 1.15f), swayC = std::cos(time * 1.15f);
    for (size_t k = 0; k < parts.size();) {
        Particle& p = parts[k];
        p.age += dt;
        p.x += 12.0f * p.z * (swayS * p.swayCos + swayC * p.swaySin) * dt;
        if (p.x < 0) p.x += pw;
        else if (p.x >= pw) p.x -= pw;
        p.y += (34 + 74 * p.z) * fallScale * dt;
        if (p.y >= ph) {
            liveFlakes--;
            parts[k] = parts.back();
            parts.pop_back();
        } else {
            k++;
        }
    }
}

// The sun reaches some stretches and not others, and those stretches wander with the hour.
void World::updateSunlight(float dt, const Weather& weather) {
    sunTimer -= dt;
    if (sunTimer > 0) return;
    sunTimer = 5;
    float h = weather.hour();
    for (int x = 0; x < gw; x++) {
        float v = 0.6f * std::sin(x * 0.0047f + sunPhaseA + h * 0.4f) + 0.4f * std::sin(x * 0.015f + sunPhaseB - h * 0.25f);
        sunlit[x] = weather.sun() * smoothRamp(-0.1f, 0.5f, v);
    }
}

// A little snow always evaporates from the surface, so piles never stay perfectly static, which
// matters on an OLED panel. The sun thaws whatever lies deeper than its stretch of ground now allows.
void World::melt(const Weather& weather) {
    if (snow.empty()) return;
    int tries = STEADY_MELT_TRIES + std::max(int(weather.sun() * SUN_MELT_TRIES), OVER_LIMIT_TRIES);
    for (int t = 0; t < tries; t++) {
        int i = int((uint64_t(rnd()) * snow.size()) >> 32);
        if (i < gw || !snow[i] || solid[i]) continue;
        bool inside = window[i] != 0;
        if (!isOpen(i - gw, inside) || guarded(i % gw, i / gw)) continue;
        if (t < STEADY_MELT_TRIES || !canPile(i % gw, i / gw - 1, inside)) takeCell(i);
    }
}

// A strong gust lifts loose snow off the surfaces and drops it further along.
void World::blowSnow(const Weather& weather) {
    float gust = weather.gustStrength();
    if (gust < 0.25f || snow.empty()) return;
    int tries = int(gust * gust * WIND_LIFT_TRIES);
    for (int t = 0; t < tries; t++) {
        int i = int((uint64_t(rnd()) * snow.size()) >> 32);
        if (i < gw || !snow[i] || solid[i] || !isOpen(i - gw, window[i] != 0)) continue;
        int x = i % gw, y = i / gw;
        if (guarded(x, y)) continue;
        float wind = weather.windAt(x * float(CELL));
        takeCell(i);
        spawnGrain(x * float(CELL) + 1, (y - 1) * float(CELL) + 1, wind * rndf(0.6f, 1.1f), rndf(-75, -15));
    }
}

void World::step(float dt, const SnowParams& params, const Weather& weather, SnowCursor* cursor) {
    current = params;
    time += dt;
    if (params.settle) settleClock += dt;
    updateSunlight(dt, weather);
    float mp = float(pw) * ph / 1000000.0f;
    int target = int(params.flakesPerMegapixel * mp * weather.snowfall());
    bool prewarm = !started;
    started = true;
    int spawn = prewarm ? target : std::min(target - liveFlakes, 1 + target / 240);
    for (int i = 0; i < spawn; i++) spawnFlake(prewarm);

    if (params.flakesOnly) {
        driftFlakes(dt, params.fallScale);
        return;
    }

    if (cursor && cursor->visible && !cursor->overWindow && cursor->idle > params.accumulateAfter * 0.7f) {
        aimTimer -= dt;
        if (aimTimer <= 0) {
            aimTimer = rndf(0.35f, 0.8f);
            std::vector<int> cols;
            for (int x = 0; x < SnowCursor::S; x++) {
                for (int y = 0; y < SnowCursor::S; y++) {
                    if (cursor->mask[size_t(y) * SnowCursor::S + x]) { cols.push_back(x); break; }
                }
            }
            if (!cols.empty() && parts.size() < MAX_PARTICLES) {
                Particle p{};
                p.z = rndf(0.5f, 0.9f);
                p.x = clampf(cursor->originX() + cols[rnd() % cols.size()], 0, float(pw) - 0.5f);
                p.y = cursor->y - rndf(70, 190);
                if (p.y >= 0) {
                    p.vy = 34 + 74 * p.z;
                    p.size = 1.7f + 3.1f * p.z;
                    p.alpha = 0.34f + 0.5f * p.z;
                    setPhase(p, rndf(0, 6.28f));
                    p.aimed = 1;
                    p.behind = 1;
                    parts.push_back(p);
                    liveFlakes++;
                }
            }
        }
    }

    // Piles settle cell by cell, so their pace must not depend on the display frame rate.
    cellClock += dt;
    if (cellClock >= 1 / 60.0f) {
        cellClock = std::min(cellClock - 1 / 60.0f, 1 / 60.0f);
        stepCells();
        melt(weather);
        blowSnow(weather);
    }
    stepParticles(dt, params, weather, cursor);
}

int World::buildInstances(Instance* out, int maxCount) const {
    int n = 0;
    for (const Particle& p : parts) {
        if (n >= maxCount) break;
        if (p.y < -8 || (p.grain && p.behind)) continue;
        float fade = p.grain ? 1.0f : std::min(p.age / 0.45f, 1.0f);
        if (p.meltLeft > 0) fade *= p.meltLeft / MELT_ON_CONTACT;
        out[n++] = { p.x, p.y, p.grain ? p.size : p.size * current.flakeScale, p.alpha * fade };
    }
    return n;
}
