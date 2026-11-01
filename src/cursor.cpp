#include "cursor.h"
#include <algorithm>
#include <cmath>

namespace {
inline uint32_t xorshift(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}
}

float SnowCursor::speed() const { return std::sqrt(vx * vx + vy * vy); }

void SnowCursor::setShape(const uint8_t* src, int w, int h, int hx, int hy) {
    std::fill(mask.begin(), mask.end(), uint8_t(0));
    int stride = w;
    w = std::min(w, S - 2 * M);
    h = std::min(h, S - 2 * M);
    hotX = hx;
    hotY = hy;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) mask[size_t(y + M) * S + x + M] = src[size_t(y) * stride + x];
    }

    // Two rings around the outline, nearest pixels first, shuffled so the frost grows unevenly.
    std::vector<uint8_t> ring(size_t(S) * S, 0);
    std::vector<uint32_t> first, second;
    uint32_t r = 0x51ED270Bu;
    for (int pass = 0; pass < 2; pass++) {
        std::vector<uint32_t>& list = pass ? second : first;
        for (int y = 1; y < S - 1; y++) {
            for (int x = 1; x < S - 1; x++) {
                size_t i = size_t(y) * S + x;
                if (mask[i] || ring[i]) continue;
                bool near = false;
                for (int k = 0; k < 4 && !near; k++) {
                    static const int ox[4] = { 1, -1, 0, 0 }, oy[4] = { 0, 0, 1, -1 };
                    size_t j = size_t(y + oy[k]) * S + x + ox[k];
                    near = pass == 0 ? mask[j] != 0 : ring[j] == 1;
                }
                if (near) list.push_back(uint32_t(i));
            }
        }
        for (uint32_t i : list) ring[i] = uint8_t(pass + 1);
        for (size_t i = list.size(); i > 1; i--) std::swap(list[i - 1], list[xorshift(r) % i]);
    }
    halo = first;
    halo.insert(halo.end(), second.begin(), second.end());
    clear();
}

bool SnowCursor::hit(float wx, float wy) const {
    int lx = int(wx - originX()), ly = int(wy - originY());
    if (lx < 0 || ly < 0 || lx >= S || ly >= S) return false;
    return mask[size_t(ly) * S + lx] != 0;
}

void SnowCursor::drop(int lx, uint32_t& rng) {
    if (lx < 1 || lx >= S - 1) return;
    auto freeAt = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= S || y >= S) return false;
        size_t i = size_t(y) * S + x;
        return !mask[i] && !snow[i];
    };
    int x = lx, y = 0;
    while (y + 1 < S && freeAt(x, y + 1)) y++;
    if (y + 1 >= S) return;
    for (int t = 0; t < 8; t++) {
        int s = (xorshift(rng) & 1) ? 1 : -1;
        int nx = freeAt(x + s, y + 1) && freeAt(x + s, y + 2) ? x + s : (freeAt(x - s, y + 1) && freeAt(x - s, y + 2) ? x - s : x);
        if (nx == x) break;
        x = nx;
        y++;
        while (y + 1 < S && freeAt(x, y + 1)) y++;
        if (y + 1 >= S) return;
    }
    int depth = 0;
    for (int yy = y + 1; yy < S && snow[size_t(yy) * S + x]; yy++) depth++;
    if (depth >= 7) return;
    snow[size_t(y) * S + x] = 255;
    snowCount++;
    dirty = true;
}

// Frost creeps along the outline, faster the longer the pointer stays put.
void SnowCursor::growFrost(float dt) {
    if (idle <= 10 || halo.empty()) return;
    float before = frost;
    frost += dt * (idle > 30 ? 7.0f : 3.0f);
    int to = std::min(int(frost), int(halo.size()));
    for (int i = int(before); i < to; i++) {
        if (!snow[halo[i]]) {
            snow[halo[i]] = 235;
            snowCount++;
        }
    }
}

void SnowCursor::clear() {
    std::fill(snow.begin(), snow.end(), uint8_t(0));
    snowCount = 0;
    frost = 0;
}

void SnowCursor::ease(float dt) {
    float rise = 1 - std::exp(-dt / 0.16f), fall = 1 - std::exp(-dt / 0.05f);
    bool changed = false, any = false;
    for (size_t i = 0; i < shown.size(); i++) {
        int from = shown[i], to = snow[i];
        if (from != to) {
            int step = int((to - from) * (to > from ? rise : fall));
            if (step == 0) step = to > from ? 1 : -1;
            shown[i] = uint8_t(from + step);
            changed = true;
        }
        any = any || shown[i];
    }
    dirty = dirty || changed;
    shownAny = any;
}
