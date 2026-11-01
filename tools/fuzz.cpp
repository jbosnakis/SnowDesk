// Throws extreme and random input at the simulation under AddressSanitizer and checked containers.
// Any out-of-bounds access stops the run with a report. fuzz.exe [seconds] [first seed]
#include <windows.h>
#include <dbghelp.h>
#include <crtdbg.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <random>
#include <vector>
#include "../src/cursor.h"
#include "../src/playground.h"
#include "../src/weather.h"
#include "../src/world.h"
#pragma comment(lib, "dbghelp.lib")

namespace {

// Prints where a checked container or assertion failed, then stops the run.
int reportWithStack(int, char* message, int*) {
    fprintf(stderr, "%s\n", message);
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    SymInitialize(process, nullptr, TRUE);
    void* frames[32];
    USHORT count = CaptureStackBackTrace(0, 32, frames, nullptr);
    for (USHORT i = 0; i < count; i++) {
        char buffer[sizeof(SYMBOL_INFO) + 256]{};
        SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 where = DWORD64(frames[i]);
        IMAGEHLP_LINE64 line{ sizeof(line) };
        DWORD offset = 0;
        if (!SymFromAddr(process, where, nullptr, symbol)) continue;
        if (SymGetLineFromAddr64(process, where, &offset, &line) && strstr(line.FileName, "SnowDesk")) {
            fprintf(stderr, "  %s  %s:%lu\n", symbol->Name, strrchr(line.FileName, '\\') + 1, line.LineNumber);
        }
    }
    fflush(stderr);
    ExitProcess(3);
}

std::mt19937 rng;
int pick(int a, int b) { return a + int(rng() % uint32_t(b - a + 1)); }
float pickf(float a, float b) { return a + (b - a) * float(rng() % 100000) / 100000.0f; }
bool coin(int percent) { return pick(0, 99) < percent; }

// Mostly ordinary values, with the edges and absurd ones mixed in.
int coord(int size) {
    switch (pick(0, 7)) {
    case 0: return 0;
    case 1: return size - 1;
    case 2: return size;
    case 3: return -1;
    case 4: return pick(-70000, 70000);
    default: return pick(-40, size + 40);
    }
}

float fcoord(float size) {
    switch (pick(0, 7)) {
    case 0: return 0;
    case 1: return size - 0.5f;
    case 2: return size;
    case 3: return -0.5f;
    case 4: return pickf(-1000000, 1000000);
    default: return pickf(-40, size + 40);
    }
}

Rect anyRect(int w, int h) {
    Rect r{ coord(w), coord(h), 0, 0 };
    r.r = coin(10) ? r.l - pick(0, 50) : r.l + pick(0, w);
    r.b = coin(10) ? r.t - pick(0, 50) : r.t + pick(0, h);
    return r;
}

void randomSolids(World& world, int w, int h) {
    std::vector<SolidRect> rects;
    for (int i = pick(0, 12); i > 0; i--) {
        SolidRect s;
        s.rc = anyRect(w, h);
        s.kind = coin(80) ? S_WINDOW : S_GROUND;
        s.rounded = coin(50);
        rects.push_back(s);
    }
    if (coin(70)) rects.push_back({ Rect{ 0, h - 48, w, h }, S_GROUND, false });

    std::vector<IconShape> icons;
    for (int i = pick(0, 16); i > 0; i--) {
        IconShape ic;
        ic.w = pick(1, 96);
        ic.h = pick(1, 96);
        ic.rc.l = coord(w);
        ic.rc.t = coord(h);
        ic.rc.r = ic.rc.l + ic.w;
        ic.rc.b = ic.rc.t + ic.h;
        auto mask = std::make_shared<std::vector<uint8_t>>(size_t(ic.w) * ic.h);
        for (uint8_t& m : *mask) m = coin(70) ? 1 : 0;
        ic.mask = mask;
        icons.push_back(ic);
    }
    world.setSolids(rects, icons);
}

SnowParams randomParams() {
    SnowParams p;
    p.flakesPerMegapixel = coin(20) ? pickf(0, 60000) : pickf(0, 1500);
    p.accumulateAfter = coin(30) ? 1e9f : pickf(0, 4);
    p.stickRate = coin(30) ? pickf(0, 30) : pickf(0, 1);
    p.bareRate = pickf(0, 1);
    p.chillDelay = coin(50) ? 0 : pickf(0, 5);
    p.chillRamp = coin(50) ? 0 : pickf(0, 5);
    p.fallScale = pickf(0.1f, 6);
    p.flakeScale = pickf(0.1f, 6);
    p.settle = coin(85);
    p.unlimited = coin(25);
    p.onIcons = coin(80);
    p.onWindows = coin(80);
    p.onGround = coin(80);
    p.flakesOnly = coin(10);
    return p;
}

void randomCursor(SnowCursor& c, int w, int h) {
    c.visible = coin(85);
    c.x = fcoord(float(w));
    c.y = fcoord(float(h));
    c.vx = pickf(-9000, 9000);
    c.vy = pickf(-9000, 9000);
    c.idle = coin(60) ? pickf(3, 60) : 0;
    if (coin(10)) {
        int cw = pick(1, 200), ch = pick(1, 200);
        std::vector<uint8_t> shape(size_t(cw) * ch);
        for (uint8_t& m : shape) m = coin(50) ? 1 : 0;
        c.setShape(shape.data(), cw, ch, pick(-20, 220), pick(-20, 220));
    }
}

void checkBoxes(World& world) {
    std::vector<CellBox> boxes;
    world.takeDirty(boxes);
    for (const CellBox& b : boxes) {
        if (b.l < 0 || b.t < 0 || b.r > world.gridW() || b.b > world.gridH() || b.l >= b.r || b.t >= b.b) {
            printf("bad dirty box %d,%d,%d,%d\n", b.l, b.t, b.r, b.b);
            abort();
        }
    }
}

void poke(World& world, Playground& play, int w, int h) {
    float W = float(w), H = float(h);
    switch (pick(0, 13)) {
    case 0: world.plow(fcoord(W), fcoord(H), fcoord(W), fcoord(H), pickf(-9000, 9000), pickf(-9000, 9000)); break;
    case 1: world.releaseRegion(anyRect(w, h), pickf(-3000, 3000), pickf(-3000, 3000)); break;
    case 2: {
        Rect r = anyRect(w, h);
        std::vector<Carried> cells;
        world.liftAttached(r, cells);
        world.placeCarried(cells, coord(w) - w / 2, coord(h) - h / 2, pickf(-6000, 6000), pickf(-6000, 6000), pickf(0, 0.5f));
        play.windowMoved(r, pick(-500, 500), pick(-500, 500));
        break;
    }
    case 3: world.spawnGrain(fcoord(W), fcoord(H), pickf(-5000, 5000), pickf(-5000, 5000)); break;
    case 4: world.scuff(fcoord(W)); break;
    case 5: world.scuffFrom(fcoord(W), fcoord(H)); break;
    case 6: world.heap(fcoord(W), fcoord(H), pick(0, 300)); break;
    case 7: world.guard(anyRect(w, h)); break;
    case 8: world.clearGuards(); break;
    case 9: (void)world.landingBelow(fcoord(W), fcoord(H)); break;
    case 10: (void)world.ledgeDepth(fcoord(W), fcoord(H)); (void)world.ledgeLevel(fcoord(W), fcoord(H)); break;
    case 11: (void)world.groundLevel(fcoord(W)); (void)world.groundDepth(fcoord(W)); (void)world.groundIsVisible(fcoord(W)); break;
    case 12: (void)world.isClear(fcoord(W), fcoord(H), fcoord(H)); (void)world.solidAt(coord(w), coord(h)); (void)world.countSnowIn(anyRect(w, h)); break;
    case 13: play.request(PlayScene(pick(0, 3))); break;
    }
}

void runOne(uint32_t seed, int frames) {
    rng.seed(seed);
    static const int sizes[][2] = { { 5120, 1440 }, { 1920, 1080 }, { 641, 481 }, { 1366, 769 }, { 200, 150 }, { 33, 17 } };
    const int* size = sizes[pick(0, 5)];
    int w = size[0], h = size[1];

    auto world = std::make_unique<World>();
    Playground play;
    Weather weather;
    SnowCursor cursor;
    std::vector<uint8_t> shape(32 * 32, 1);
    cursor.setShape(shape.data(), 32, 32, 2, 2);
    world->init(w, h, seed | 1);
    play.init(seed ^ 0x5BD1E995u);
    weather.init(seed | 1);
    randomSolids(*world, w, h);

    SnowParams params = randomParams();
    std::vector<Instance> instances(MAX_PARTICLES);
    for (int f = 0; f < frames; f++) {
        if (coin(4)) randomSolids(*world, w, h);
        if (coin(2)) params = randomParams();
        if (coin(2)) weather.configure(pickf(0, 2), coin(50), coin(20));
        if (coin(1)) weather.startStorm(pickf(0, 30));
        if (coin(1)) world->clearSnow();
        if (coin(30)) randomCursor(cursor, w, h);
        for (int k = pick(0, 3); k > 0; k--) poke(*world, play, w, h);

        float dt = coin(10) ? 0.05f : (coin(10) ? 0.0f : 1 / 120.0f);
        weather.step(dt, pickf(0, 24));
        SnowCursor* c = coin(90) ? &cursor : nullptr;
        world->step(dt, params, weather, c);
        play.step(dt, *world, weather, c);
        cursor.growFrost(dt);
        cursor.ease(dt);
        (void)world->buildInstances(instances.data(), int(instances.size()));
        checkBoxes(*world);
    }
}

}

int main(int argc, char** argv) {
    _CrtSetReportHook(reportWithStack);
    int seconds = argc > 1 ? atoi(argv[1]) : 60;
    uint32_t seed = argc > 2 ? uint32_t(atoi(argv[2])) : 1;
    time_t end = time(nullptr) + seconds;
    int rounds = 0;
    while (time(nullptr) < end) {
        printf("seed %u\n", seed);
        fflush(stdout);
        runOne(seed++, 1500);
        rounds++;
    }
    printf("clean: %d rounds, seeds up to %u\n", rounds, seed - 1);
    return 0;
}
