// Snow that collects on the mouse pointer. The pointer itself is drawn by the system above every
// window, so we can only paint around it: a cap on its upper edges and a frost halo along its outline.
#pragma once
#include <cstdint>
#include <vector>

struct SnowCursor {
    static constexpr int S = 96, M = 16;    // canvas size and the margin around the pointer image

    bool visible = false;
    float x = 0, y = 0, vx = 0, vy = 0;
    float idle = 0;
    int hotX = 0, hotY = 0;
    int snowCount = 0;
    bool dirty = true;

    std::vector<uint8_t> mask = std::vector<uint8_t>(S * S, 0);
    std::vector<uint8_t> snow = std::vector<uint8_t>(S * S, 0);
    std::vector<uint8_t> shown = std::vector<uint8_t>(S * S, 0);    // snow, eased over time for drawing

    float originX() const { return x - hotX - M; }
    float originY() const { return y - hotY - M; }
    float speed() const;
    bool anythingShown() const { return shownAny; }

    void setShape(const uint8_t* src, int w, int h, int hx, int hy);
    bool hit(float wx, float wy) const;
    void drop(int lx, uint32_t& rng);
    void growFrost(float dt);
    void resetFrost() { frost = 0; }
    void clear();
    void ease(float dt);

private:
    std::vector<uint32_t> halo;
    float frost = 0;
    bool shownAny = false;
};
