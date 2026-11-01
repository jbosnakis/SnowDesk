// Headless test of the children's rules on a made-up desktop: no window appears and the real mouse is not used.
// Build and run: tools\playtest.bat
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include "../src/playground.h"
#include "../src/world.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    failures += ok ? 0 : 1;
}

constexpr int W = 1800, H = 1000, BAR = 48;
constexpr float DT = 1 / 60.0f;

IconShape icon(int x, int y) {
    auto mask = std::make_shared<std::vector<uint8_t>>(48 * 48, uint8_t(1));
    return { { x, y, x + 48, y + 48 }, 48, 48, mask };
}

struct Desk {
    World world;
    Playground play;
    Weather weather;
    SnowCursor cursor;
    std::vector<SolidRect> rects;
    std::vector<IconShape> icons;

    void init(uint32_t seed) {
        world.init(W, H, seed);
        play.init(seed ^ 0x5BD1E995u);
        weather.init(seed | 1);
        cursor.visible = false;
        rects.push_back({ { 0, H - BAR, W, H }, S_GROUND, false });
    }
    void apply() { world.setSolids(rects, icons); }
    void run(float seconds, bool kids = true) {
        SnowParams params;
        params.flakesPerMegapixel = 2400;
        for (float t = 0; t < seconds; t += DT) {
            weather.step(DT, 21);       // night, so the sun does not thaw what the test needs
            world.step(DT, params, weather, &cursor);
            if (kids) play.step(DT, world, weather, &cursor);
        }
    }
    // Wipes the ground in a band with fast mouse strokes.
    void sweep(float from, float to) {
        float y = H - BAR - 8.0f;
        for (float x = from; x < to; x += 20) world.plow(x, y, x + 20, y, 3000, 0);
    }
};

template <class Pred> bool runUntil(Desk& d, float limit, Pred done) {
    for (float t = 0; t < limit; t += 0.25f) {
        d.run(0.25f);
        if (done()) return true;
    }
    return false;
}

void noSnowNoPlay() {
    puts("no snow on the ground: nobody comes out to play");
    Desk d;
    d.init(11);
    d.apply();
    d.play.request(PlayScene::Snowman);
    SnowParams params;
    params.flakesPerMegapixel = 0;
    for (float t = 0; t < 40; t += DT) {
        d.weather.step(DT, 12);
        d.world.step(DT, params, d.weather, &d.cursor);
        d.play.step(DT, d.world, d.weather, &d.cursor);
    }
    check(d.play.kidCount() == 0, "no children without snow");
}

void scenesVaryAndAvoidCover() {
    puts("scenes: on the ground and on windows, different spots every time");
    int onWindow = 0, onGround = 0, cramped = 0;
    std::vector<float> spots;
    for (uint32_t seed = 1; seed <= 12; seed++) {
        Desk d;
        d.init(seed * 37);
        d.rects.push_back({ { 300, 500, 1000, 800 }, S_WINDOW, true });      // room above: fine
        d.rects.push_back({ { 1100, 60, 1700, 700 }, S_WINDOW, true });      // too close to the top of the screen
        d.apply();
        d.run(50, false);
        d.play.request(seed & 1 ? PlayScene::Fight : PlayScene::Snowman);
        if (!runUntil(d, 40, [&] { return d.play.kidCount() > 0; })) continue;
        spots.push_back(d.play.sceneSpot());
        if (d.play.playingOnWindow()) {
            onWindow++;
            cramped += d.play.ledgeHeight() < 135 ? 1 : 0;
        } else {
            onGround++;
        }
    }
    printf("  on windows %d, on the ground %d\n", onWindow, onGround);
    std::sort(spots.begin(), spots.end());
    float spread = spots.empty() ? 0 : spots.back() - spots.front();
    check(onWindow > 0, "some scenes play on a window");
    check(onGround > 0, "some scenes play on the ground");
    check(cramped == 0, "never on a window without headroom");
    check(spread > 300, "the spot changes from one time to the next");
}

void windowCarriesAndDrops() {
    puts("a window with children on it moves, then closes");
    for (uint32_t seed = 1; seed < 40; seed++) {
        Desk d;
        d.init(seed * 101);
        Rect win{ 400, 500, 1200, 850 };
        d.rects.push_back({ win, S_WINDOW, true });
        d.apply();
        d.run(50, false);
        d.play.request(PlayScene::Fight);
        if (!runUntil(d, 20, [&] { return d.play.kidCount() > 0; }) || !d.play.playingOnWindow()) continue;
        d.run(4);
        float spot = d.play.sceneSpot(), ledge = d.play.ledgeHeight();
        d.play.windowMoved(win, 60, -30);
        d.rects[1].rc = { win.l + 60, win.t - 30, win.r + 60, win.b - 30 };
        d.apply();
        d.run(1);
        check(std::fabs(d.play.sceneSpot() - spot - 60) < 1 && std::fabs(d.play.ledgeHeight() - ledge + 30) < 1, "the scene rides along with the window");
        check(d.play.playingOnWindow(), "still on the window after the move");
        d.rects.pop_back();
        d.apply();
        d.run(1);
        check(!d.play.playingOnWindow(), "the children drop when the window closes");
        bool cleared = runUntil(d, 60, [&] { return d.play.kidCount() == 0; });
        check(cleared, "and walk off the screen");
        return;
    }
    check(false, "no scene started on a window");
}

// A 2x2 block of icons with the bottom row within a jump of the ground.
Desk* climbDesk(uint32_t seed) {
    Desk* d = new Desk;
    d->init(seed);
    for (int cx : { 700, 790 }) {
        d->icons.push_back(icon(cx, H - BAR - 100));
        d->icons.push_back(icon(cx, H - BAR - 198));
    }
    d->apply();
    d->run(70, false);
    return d;
}

void climbWithSnow() {
    puts("climbing: up onto the icons and back down onto snow");
    int climbed = 0, finished = 0, soft = 0, hard = 0, runs = 0;
    for (uint32_t seed = 5; seed < 45; seed += 8) {
        std::unique_ptr<Desk> d(climbDesk(seed));
        d->play.request(PlayScene::Climb);
        runs++;
        if (!runUntil(*d, 40, [&] { return d->play.kidsOnIcons() > 0; })) continue;
        climbed++;
        finished += runUntil(*d, 90, [&] { return d->play.kidCount() == 0; }) ? 1 : 0;
        soft += d->play.softLandings();
        hard += d->play.hardLandings();
    }
    printf("  runs %d, climbed %d, finished %d, landings soft %d hard %d\n", runs, climbed, finished, soft, hard);
    check(climbed > 0, "a child gets onto an icon");
    check(finished == climbed, "and has left again at the end");
    check(hard == 0, "never lands without snow");
}

void climbSnowTakenAway() {
    puts("the snow under the icons is taken away: hop along, or throw snow down first");
    int heaps = 0, hard = 0, onIcons = 0, soft = 0, hops = 0, ended = 0, runs = 0;
    for (uint32_t seed = 3; seed < 15; seed += 2) {
        std::unique_ptr<Desk> d(climbDesk(seed * 13));
        d->play.request(PlayScene::Climb);
        if (!runUntil(*d, 40, [&] { return d->play.kidsOnIcons() > 0; })) continue;
        runs++;
        // A hard-working mouse: the ground under the whole group is kept bare, in sweeps all through the scene.
        for (float t = 0; t < 120 && d->play.kidCount() > 0; t += 0.25f) {
            d->sweep(500, 1100);
            d->run(0.25f);
        }
        ended += d->play.kidCount() == 0 ? 1 : 0;
        heaps += d->play.heapsThrown();
        hard += d->play.hardLandings();
        onIcons += d->play.hardOnIcons();
        soft += d->play.softLandings();
        hops += d->play.iconHops();
    }
    printf("  runs %d: hops %d, heaps %d, soft %d, hard %d (of them on icons %d), ended %d\n", runs, hops, heaps, soft, hard, onIcons, ended);
    check(runs > 0, "the scene starts");
    check(heaps > 0, "with nothing below, armfuls are thrown down");
    check(hard == 0, "the mouse cannot spoil the heap: no hard landing");
    check(ended == runs, "every child gets down in the end");
}

void guardHoldsAgainstClearing() {
    puts("a guarded heap survives the mouse and the Clear command");
    Desk d;
    d.init(77);
    d.apply();
    d.world.heap(600, H - BAR - 2.0f, 200);
    Rect zone{ 540, H - BAR - 60, 660, H - BAR };
    d.world.guard(zone);
    int before = d.world.countSnowIn(zone);
    d.sweep(500, 700);
    d.world.clearSnow();
    check(before > 100 && d.world.countSnowIn(zone) >= before * 9 / 10, "the heap is still there");
    d.world.clearGuards();
    d.sweep(500, 700);
    check(d.world.countSnowIn(zone) < before, "and can be swept away once the guard is lifted");
}

}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    noSnowNoPlay();
    guardHoldsAgainstClearing();
    scenesVaryAndAvoidCover();
    windowCarriesAndDrops();
    climbWithSnow();
    climbSnowTakenAway();
    printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
