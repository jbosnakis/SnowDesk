#include "playground.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float PI = 3.14159265f;
constexpr float MIN_SPAN = 400;         // open ground needed for a scene, in pixels
constexpr float HEADROOM = 135;         // room above it: taller than a snowman, a cheer or a thrown ball
constexpr float MIN_WINDOW_HEIGHT = 110;    // the children come up from behind a window, so it must hide them
constexpr float RISE = 80;              // how far below the edge a child starts
constexpr float MIN_DEPTH = 2;          // cells of snow that count as somewhere to play
constexpr float SOFT_DEPTH = 2.5f;      // cells of snow that are safe to land on
constexpr float JUMP_UP = 115;          // the highest a child can jump
constexpr float LAUNCH_GAP = 62;        // where a child stands to jump onto an icon
constexpr float DROP_SIDE = 46;         // how far out from an icon a child jumps down
constexpr float NEIGHBOUR = 135;        // the longest jump between icons
constexpr int HEAP_MASS = 120;          // cells in an armful thrown down
constexpr int CLIMB_TEAM = 7;           // climbers throw for fun, at nobody
constexpr float ROLL_SPEED = 17;
constexpr float CARRY_SPEED = 80;
constexpr float SNOWBALL_GRAVITY = 520;
constexpr size_t MAX_SNOWMEN = 3;
const float BALL_SIZES[3] = { 21, 15, 11 };
const float TOTAL_DECOR = 7;

constexpr uint32_t rgb(uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255) {
    return r | (g << 8) | (b << 16) | (a << 24);
}

struct Outfit { uint32_t coat, coatDark, hat, hatBand, scarf, mitten, skin, trousers; };

const Outfit OUTFITS[6] = {
    { rgb(214, 62, 66), rgb(170, 42, 52), rgb(247, 201, 72), rgb(214, 160, 40), rgb(70, 130, 200), rgb(70, 130, 200), rgb(250, 214, 184), rgb(58, 66, 92) },
    { rgb(66, 134, 214), rgb(44, 100, 174), rgb(226, 72, 84), rgb(186, 48, 62), rgb(247, 201, 72), rgb(226, 72, 84), rgb(226, 178, 140), rgb(70, 62, 84) },
    { rgb(82, 176, 120), rgb(56, 138, 92), rgb(244, 136, 56), rgb(206, 104, 36), rgb(236, 90, 120), rgb(244, 136, 56), rgb(248, 220, 196), rgb(66, 80, 96) },
    { rgb(236, 112, 160), rgb(196, 80, 126), rgb(120, 96, 214), rgb(92, 72, 176), rgb(96, 206, 214), rgb(120, 96, 214), rgb(140, 98, 70), rgb(70, 66, 100) },
    { rgb(244, 168, 60), rgb(208, 130, 36), rgb(52, 150, 170), rgb(36, 116, 136), rgb(214, 62, 66), rgb(52, 150, 170), rgb(244, 206, 170), rgb(76, 64, 74) },
    { rgb(136, 96, 210), rgb(104, 70, 170), rgb(90, 190, 130), rgb(62, 150, 100), rgb(247, 201, 72), rgb(90, 190, 130), rgb(198, 146, 108), rgb(60, 62, 88) },
};
const uint32_t BOOT = rgb(66, 48, 44);
const uint32_t SNOW_LIGHT = rgb(240, 245, 252);
const uint32_t SNOW_SHADE = rgb(190, 208, 234);
const uint32_t COAL = rgb(36, 36, 44);
const uint32_t STICK = rgb(112, 78, 54);
const uint32_t CARROT = rgb(242, 130, 40);
const uint32_t HAT_BLACK = rgb(38, 38, 48);

float approach(float value, float target, float rate, float dt) {
    return value + (target - value) * std::min(1.0f, rate * dt);
}

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
}

float Playground::rnd(float a, float b) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return a + (b - a) * ((rng & 0xFFFFFF) / 16777216.0f);
}

void Playground::init(uint32_t seed) {
    rng = seed ? seed : 0x7A3D51u;
    nextScene = rnd(25, 50);
}

void Playground::request(PlayScene s) {
    wanted = s;
    nextScene = 0;
}

float Playground::floorAt(const World& world, float x, bool window) const {
    return window ? world.ledgeLevel(x, ledgeY) : world.groundLevel(x);
}

void Playground::scuffFloor(World& world, float x, bool window) {
    if (window) world.scuffFrom(x, world.ledgeLevel(x, ledgeY) - 8);
    else world.scuff(x);
}

float Playground::floorDepth(const World& world, float x) const {
    return onWindow ? world.ledgeDepth(x, ledgeY) : world.groundDepth(x);
}

// Every place along this surface where a scene fits: snow underfoot, and nothing above it that would
// hide the children or that they would hide. Each one is equally likely, so the spot differs every time.
void Playground::addSpots(const World& world, float from, float to, float base, bool window, std::vector<Spot>& out) {
    const float step = 8;
    float start = -1;
    float savedLedge = ledgeY;
    bool savedWindow = onWindow;
    ledgeY = base;
    onWindow = window;
    for (float x = from; x <= to; x += step) {
        float level = floorAt(world, x, window);
        bool open = x + step <= to && world.isClear(x, level - HEADROOM, level - 6) && (window || world.groundIsVisible(x));
        if (open && start < 0) start = x;
        if (!open && start >= 0) {
            for (float left = start; left + MIN_SPAN <= x; left += 40) {
                int samples = 0, snowy = 0;
                for (float c = left; c <= left + MIN_SPAN; c += 12) {
                    samples++;
                    snowy += floorDepth(world, c) >= MIN_DEPTH ? 1 : 0;
                }
                if (snowy >= samples * 0.85f) out.push_back({ left, left + MIN_SPAN, base, window });
            }
            start = -1;
        }
    }
    ledgeY = savedLedge;
    onWindow = savedWindow;
}

// Where a child walking this way stops being seen: behind a window, or past the edge of the screen.
float Playground::hiddenEdge(const World& world, float x, int dir) const {
    while (x > 0 && x < world.width() && world.groundIsVisible(x)) x += dir * 8.0f;
    if (x <= 0) return -50;
    if (x >= world.width()) return world.width() + 50.0f;
    return x + dir * 46.0f;
}

float Playground::nearestExit(const World& world, float x) const {
    float left = hiddenEdge(world, x, -1), right = hiddenEdge(world, x, 1);
    return x - left < right - x ? left : right;
}

Playground::Kid Playground::makeKid(int style, float x, float toX) {
    Kid k;
    k.style = style % 6;
    k.x = x;
    k.targetX = toX;
    k.scale = rnd(0.9f, 1.08f);
    k.speed = rnd(105, 130);
    k.facing = toX >= x ? 1 : -1;
    return k;
}

// A child who plays on a window climbs up from behind it, so nobody appears out of thin air.
void Playground::riseFromWindow(Kid& k, const World& world) {
    k.onWindow = true;
    k.y = world.ledgeLevel(k.x, ledgeY) + 3;
    k.sink = RISE;
    k.sinkTarget = 0;
    k.sinkSpeed = rnd(55, 85);
}

bool Playground::startScene(PlayScene which, World& world) {
    if (which == PlayScene::Climb) return startClimb(world);

    std::vector<Spot> ground, raised;
    addSpots(world, 0, float(world.width()), 0, false, ground);
    for (const Rect& top : world.windowTops()) {
        if (top.h() < MIN_WINDOW_HEIGHT) continue;
        float from = std::max(top.l + 12.0f, 0.0f), to = std::min(top.r - 12.0f, float(world.width()));
        if (to - from >= MIN_SPAN) addSpots(world, from, to, float(top.t), true, raised);
    }
    if (ground.empty() && raised.empty()) return false;     // no snow or no room for now; look again soon

    const std::vector<Spot>& pool = !raised.empty() && (ground.empty() || chance(0.45f)) ? raised : ground;
    Spot place = pool[size_t(rnd(0, 0.999f) * pool.size())];

    kids.clear();
    scene = which;
    wanted = PlayScene::None;
    sceneTime = 0;
    stageTime = 0;
    lowSnowTime = 0;
    onWindow = place.window;
    ledgeY = place.base;

    // Children appear from behind whatever hides the stretch, or from the edge of the screen.
    if (!onWindow) {
        exitLeft = hiddenEdge(world, place.left, -1);
        exitRight = hiddenEdge(world, place.right, 1);
    }
    int firstStyle = int(rnd(0, 5.99f));
    spot = rnd(place.left + 190, place.right - 190);

    if (which == PlayScene::Snowman) {
        side = chance(0.5f) ? 1 : -1;
        int count = chance(0.55f) ? 2 : 1;
        float from = side > 0 ? exitLeft : exitRight;
        for (int i = 0; i < count; i++) {
            float toX = spot - side * (112 + i * 70.0f);
            Kid k = makeKid(firstStyle + i, onWindow ? toX : from - side * i * 46.0f, toX);
            if (onWindow) riseFromWindow(k, world);
            kids.push_back(k);
        }
        ballIndex = 0;
        ballRolling = false;
        stage = Stage::Arrive;
    } else {
        int perTeam = chance(0.5f) ? 2 : 1;
        float reach = std::min(spot - place.left, place.right - spot) - 30;
        for (int team = 0; team < 2; team++) {
            int sign = team ? 1 : -1;
            for (int i = 0; i < perTeam; i++) {
                float distance = std::min(95 + i * 55.0f + rnd(0, 20), reach);
                float from = team ? exitRight : exitLeft;
                float toX = spot + sign * distance;
                Kid k = makeKid(firstStyle + team * 2 + i, onWindow ? toX : from + sign * i * 40.0f, toX);
                if (onWindow) riseFromWindow(k, world);
                k.team = team;
                k.timer = rnd(0.2f, 1.6f);
                kids.push_back(k);
            }
        }
        fightLength = rnd(24, 42);
        stage = Stage::Arrive;
    }
    return true;
}

bool Playground::iconReady(const World& world, const Rect& icon, Landing& top) const {
    float cx = (icon.l + icon.r) * 0.5f;
    if (world.solidAt(int(cx), icon.t + 4) == S_WINDOW) return false;
    top = world.landingBelow(cx, icon.t - 40.0f);
    return top.kind == S_ICON && top.depth >= SOFT_DEPTH;
}

// Icons close enough above the ground or a window to jump onto, with snow on them and where the child starts.
bool Playground::startClimb(World& world) {
    std::vector<Leap> leaps;
    for (const Rect& icon : world.iconBoxes()) {
        Landing top;
        if (!iconReady(world, icon, top)) continue;
        float cx = (icon.l + icon.r) * 0.5f;
        for (int s = -1; s <= 1; s += 2) {
            float x = cx + s * LAUNCH_GAP;
            if (x < 20 || x > world.width() - 20) continue;
            float rise = world.groundLevel(x) - icon.t;
            if (rise > 40 && rise <= JUMP_UP && world.groundIsVisible(x) && world.groundDepth(x) >= MIN_DEPTH) {
                leaps.push_back({ x, 0, false, icon });
            }
            for (const Rect& t : world.windowTops()) {
                if (t.h() < MIN_WINDOW_HEIGHT || x < t.l + 16 || x > t.r - 16) continue;
                float level = world.ledgeLevel(x, float(t.t));
                rise = level - icon.t;
                if (rise > 40 && rise <= JUMP_UP && world.ledgeDepth(x, float(t.t)) >= MIN_DEPTH && world.isClear(x, level - 70, level - 6)) {
                    leaps.push_back({ x, float(t.t), true, icon });
                }
            }
        }
    }
    if (leaps.empty()) return false;

    kids.clear();
    scene = PlayScene::Climb;
    wanted = PlayScene::None;
    sceneTime = 0;
    stage = Stage::Climb;
    Leap first = leaps[size_t(rnd(0, 0.999f) * leaps.size())];
    onWindow = first.window;
    ledgeY = first.base;
    spot = first.launchX;

    std::vector<Leap> party{ first };
    if (chance(0.5f)) {
        for (const Leap& other : leaps) {
            if (other.window == first.window && other.base == first.base && !(other.icon == first.icon)) {
                party.push_back(other);
                break;
            }
        }
    }
    int firstStyle = int(rnd(0, 5.99f));
    for (size_t i = 0; i < party.size(); i++) {
        const Leap& l = party[i];
        float from = l.launchX;
        if (!onWindow) {
            float exit = nearestExit(world, l.launchX);
            from = exit + (exit < l.launchX ? -46.0f : 46.0f) * float(i);
        }
        Kid k = makeKid(firstStyle + int(i), from, l.launchX);
        if (onWindow) riseFromWindow(k, world);
        k.team = CLIMB_TEAM;
        k.job = Job::Approach;
        k.goalX = (l.icon.l + l.icon.r) * 0.5f;
        k.goalTop = float(l.icon.t);
        k.timer = rnd(0.2f, 1.4f);
        k.playLeft = rnd(9, 22);
        kids.push_back(k);
    }
    return true;
}

bool Playground::allArrived() const {
    for (const Kid& k : kids) {
        if (std::fabs(k.x - k.targetX) > 2 || k.sink > 1) return false;
    }
    return true;
}

void Playground::moveKid(Kid& k, float dt, World& world, const Weather&) {
    k.walking = false;
    if (k.feet == Feet::Jump) {
        k.jumpT = std::min(k.jumpT + dt / k.jumpTime, 1.0f);
        float t = k.jumpT;
        k.x = k.fromX + (k.toX - k.fromX) * t;
        k.y = k.fromY + (k.toY - k.fromY) * t - k.arc * 4 * t * (1 - t);
        if (t >= 1) land(k, world);
    } else if (k.feet == Feet::Fall) {
        Landing below = world.landingBelow(k.x, k.y);
        k.fallSpeed = std::min(k.fallSpeed + 1100 * dt, 900.0f);
        k.y += k.fallSpeed * dt;
        if (k.y >= below.y + 3) {
            k.toX = k.x;
            k.toY = below.y + 3;
            k.toKind = below.kind;
            k.padX = k.x;
            land(k, world);
        }
    } else if (k.feet == Feet::Icon) {
        // An icon dragged away from under a child, or snow sliding off it, leaves nothing to stand on.
        Landing under = world.landingBelow(k.x, k.y - 14);
        if (under.kind != S_ICON || under.y > k.y + 10) {
            k.feet = Feet::Fall;
            k.fallSpeed = 0;
            k.surprised = 0.7f;
            k.scoop = 0;
            k.holdingBall = false;
            k.armFrontTarget = k.armBackTarget = 2.6f;
            if (k.job != Job::CrouchJump) k.job = Job::None;
        } else {
            k.y = approach(k.y, under.y + 3, 12, dt);
        }
    } else {
        float gap = k.targetX - k.x;
        k.walking = std::fabs(gap) > 1.5f;
        if (k.walking) {
            k.facing = gap > 0 ? 1 : -1;
            float move = std::min(k.speed * dt, std::fabs(gap));
            k.x += k.facing * move;

            float before = k.phase;
            k.phase += move * 0.115f;
            if (std::floor(before / PI) != std::floor(k.phase / PI)) {
                int foot = int(std::floor(k.phase / PI)) & 1;
                scuffFloor(world, k.x + k.facing * (foot ? 8.0f : -8.0f) * k.scale, k.onWindow);     // a footprint
            }
        }
        float ground = floorAt(world, k.x, k.onWindow) + 3;
        k.y = k.y == 0 ? ground : approach(k.y, ground, 12, dt);
    }

    if (k.sink != k.sinkTarget) {
        float was = k.sink, reach = k.sinkSpeed * dt;
        k.sink += std::clamp(k.sinkTarget - k.sink, -reach, reach);
        if (k.sinkTarget == 0) {
            k.armFrontTarget = k.armBackTarget = k.sink > 0 ? 2.6f : 0.2f;
            if (was > 0 && k.sink == 0) {
                k.armBackTarget = -0.2f;
                k.hopSpeed = 130;       // over the edge
            }
        }
    }

    k.hopSpeed -= 900 * dt;
    k.hop = std::max(k.hop + k.hopSpeed * dt, 0.0f);
    if (k.hop <= 0) k.hopSpeed = 0;

    k.crouch = approach(k.crouch, k.crouchTarget, 11, dt);
    k.armFront = approach(k.armFront, k.armFrontTarget, 13, dt);
    k.armBack = approach(k.armBack, k.armBackTarget, 13, dt);
    k.surprised = std::max(k.surprised - dt, 0.0f);

    if (k.walking && !k.holdingBall && k.crouchTarget == 0) {
        float swing = std::sin(k.phase) * 0.75f;
        k.armFrontTarget = swing;
        k.armBackTarget = -swing;
    }
}

void Playground::leave() {
    stage = Stage::Leave;
    for (Kid& k : kids) {
        k.crouchTarget = 0;
        k.holdingBall = false;
        if (k.onWindow) k.sinkTarget = RISE;
        else k.targetX = std::fabs(k.x - exitLeft) < std::fabs(k.x - exitRight) ? exitLeft : exitRight;
    }
}

// The snow they came for is gone: whatever was half built is left to melt.
void Playground::abandon() {
    if (scene == PlayScene::Snowman && stage != Stage::Arrive && !snowmen.empty() && snowmen.back().decor < TOTAL_DECOR) {
        snowmen.back().life = 0;
    }
    ballRolling = false;
    for (Kid& k : kids) {
        k.armFrontTarget = 0.2f;
        k.armBackTarget = -0.2f;
    }
    leave();
}

void Playground::checkSnowUnderfoot(float dt, const World& world) {
    snowCheck -= dt;
    if (snowCheck > 0) return;
    snowCheck = 0.5f;
    int samples = 0, snowy = 0;
    for (float x = spot - 170; x <= spot + 170; x += 12) {
        samples++;
        snowy += floorDepth(world, x) >= 1 ? 1 : 0;
    }
    lowSnowTime = snowy < samples * 0.35f ? lowSnowTime + 0.5f : 0;
    if (lowSnowTime >= 2) abandon();
}

// The window under the scene was closed, minimized or dragged off this screen: everyone on it drops.
void Playground::checkLedge(const World& world) {
    if (!onWindow || world.solidAt(int(spot), int(ledgeY) + 2) == S_WINDOW) return;
    onWindow = false;
    ballRolling = false;
    scene = PlayScene::Climb;
    stage = Stage::Climb;
    for (Kid& k : kids) {
        k.sink = k.sinkTarget = 0;
        k.playLeft = 0;
        if (k.gone) continue;
        k.holdingBall = false;
        if (k.onWindow) {
            k.onWindow = false;
            k.feet = Feet::Fall;
            k.job = Job::None;
            k.fallSpeed = 0;
            k.surprised = 0.7f;
            k.crouchTarget = 0;
            k.armFrontTarget = k.armBackTarget = 2.6f;
        }
    }
}

void Playground::windowMoved(const Rect& before, int dx, int dy) {
    auto on = [&](float x, float base) { return std::fabs(base - before.t) < 4 && x >= before.l - 4 && x <= before.r + 4; };
    for (Snowman& s : snowmen) {
        if (s.onWindow && on(s.x, s.baseY)) {
            s.x += dx;
            s.y += dy;
            s.baseY += dy;
        }
    }
    if (!onWindow || !on(spot, ledgeY)) return;
    ledgeY += dy;
    spot += dx;
    ballX += dx;
    ballY += dy;
    placeFromX += dx;
    placeFromY += dy;
    for (Kid& k : kids) {
        if (!k.onWindow) continue;
        k.x += dx;
        k.y += dy;
        k.targetX += dx;
    }
}

void Playground::stepSnowmanScene(float dt, World& world) {
    stageTime += dt;
    Kid& worker = kids[size_t(ballIndex) % kids.size()];
    float top = floorAt(world, spot, onWindow) + 3;

    switch (stage) {
    case Stage::Arrive:
        if (allArrived()) {
            snowmen.push_back({});
            Snowman& s = snowmen.back();
            s.x = spot;
            s.y = top;
            s.life = rnd(300, 720);
            s.onWindow = onWindow;
            s.baseY = ledgeY;
            stage = Stage::FetchBall;
            stageTime = 0;
        }
        break;

    case Stage::FetchBall: {
        // The first ball is rolled up to the spot; the others from the far side, to be lifted on.
        int from = ballIndex == 0 ? side : -side;
        worker.targetX = spot - from * (ballIndex == 0 ? 112.0f : 118.0f);
        for (Kid& k : kids) {
            if (&k != &worker) {
                k.facing = spot > k.x ? 1 : -1;
                k.armFrontTarget = 0.3f + 0.25f * std::sin(time * 3 + k.phase);
            }
        }
        if (std::fabs(worker.x - worker.targetX) < 3) {
            worker.facing = from;
            ballRolling = true;
            ballX = worker.x + from * 26.0f;
            ballR = 5;
            ballSpin = 0;
            worker.speed = ROLL_SPEED;
            worker.crouchTarget = 0.55f;
            worker.armFrontTarget = worker.armBackTarget = 1.45f;
            worker.targetX = spot - from * (ballIndex == 0 ? 26.0f : 60.0f) - from * BALL_SIZES[ballIndex];
            stage = Stage::RollBall;
            stageTime = 0;
        }
        break;
    }

    case Stage::RollBall: {
        int from = worker.facing;
        float target = BALL_SIZES[ballIndex];
        ballX = worker.x + from * (ballR + 13);
        float rolled = ROLL_SPEED * dt;
        ballSpin += from * rolled / std::max(ballR, 1.0f);
        ballR = std::min(ballR + (target - 5) * rolled / 74.0f, target);
        ballY = floorAt(world, ballX, onWindow) + 3 - ballR;
        scuffFloor(world, ballX + from * rnd(-ballR, ballR) * 0.5f, onWindow);      // the ball picks up the snow it rolls over
        if (std::fabs(worker.x - worker.targetX) < 1.5f) {
            worker.speed = 115;
            ballR = target;
            if (ballIndex == 0) {
                Snowman& s = snowmen.back();
                s.x = ballX;
                s.y = floorAt(world, ballX, onWindow) + 3;
                s.radius[0] = target;
                spot = ballX;
                ballRolling = false;
                worker.crouchTarget = 0;
                worker.armFrontTarget = 0.2f;
                worker.armBackTarget = -0.2f;
                ballIndex = 1;
                stage = Stage::FetchBall;
                stageTime = 0;
            } else {
                // Up onto the hands and over to the snowman.
                worker.crouchTarget = 0;
                worker.holdingBall = true;
                worker.armFrontTarget = worker.armBackTarget = 2.9f;
                worker.speed = CARRY_SPEED;
                Snowman& s = snowmen.back();
                worker.targetX = s.x - from * (s.radius[0] + 15);
                stage = Stage::CarryBall;
                stageTime = 0;
            }
        }
        break;
    }

    case Stage::CarryBall: {
        float hx, hy;
        handOf(worker, hx, hy);
        float lift = clamp01(stageTime / 0.45f);
        float wantedY = hy - ballR * 0.7f;
        ballX = approach(ballX, hx, 14, dt);
        ballY = ballY + (wantedY - ballY) * lift;
        if (stageTime > 0.45f && std::fabs(worker.x - worker.targetX) < 2) {
            worker.holdingBall = false;
            worker.hopSpeed = 170;      // on tiptoes for the last bit
            placeFromX = ballX;
            placeFromY = ballY;
            stage = Stage::PlaceBall;
            stageTime = 0;
        }
        break;
    }

    case Stage::PlaceBall: {
        Snowman& s = snowmen.back();
        float seat = s.y - s.radius[0] * 1.72f;
        if (ballIndex == 2) seat -= s.radius[1] * 1.62f;
        seat -= ballR * 0.2f;
        float t = clamp01(stageTime / 0.5f);
        t = t * t * (3 - 2 * t);
        ballX = placeFromX + (s.x - placeFromX) * t;
        ballY = placeFromY + (seat - placeFromY) * t - 12 * std::sin(t * PI);
        if (stageTime >= 0.5f) {
            s.radius[ballIndex] = ballR;
            ballRolling = false;
            worker.armFrontTarget = 0.2f;
            worker.armBackTarget = -0.2f;
            for (int i = 0; i < 10; i++) world.spawnGrain(s.x + rnd(-ballR, ballR), seat + ballR, rnd(-50, 50), rnd(-110, -20));
            ballIndex++;
            if (ballIndex < 3) {
                stage = Stage::FetchBall;
            } else {
                stage = Stage::Decorate;
                Kid& k = kids[0];
                k.targetX = s.x - k.facing * (s.radius[0] + 14);
            }
            stageTime = 0;
        }
        break;
    }

    case Stage::Decorate: {
        Snowman& s = snowmen.back();
        for (Kid& k : kids) {
            if (std::fabs(k.x - k.targetX) < 3) k.facing = s.x > k.x ? 1 : -1;
        }
        Kid& k = kids[0];
        if (std::fabs(k.x - k.targetX) < 3) {
            float before = s.decor;
            s.decor = std::min(s.decor + dt * 1.7f, TOTAL_DECOR);
            k.armFrontTarget = 2.3f + 0.5f * std::sin(time * 9);
            if (std::floor(before) != std::floor(s.decor)) k.hopSpeed = 90;
        }
        if (s.decor >= TOTAL_DECOR) {
            stage = Stage::Cheer;
            stageTime = 0;
        }
        break;
    }

    case Stage::Cheer:
        for (Kid& k : kids) {
            k.armFrontTarget = 2.8f + 0.25f * std::sin(time * 13);
            k.armBackTarget = 2.8f - 0.25f * std::sin(time * 13);
            if (k.hop <= 0 && chance(dt * 3.5f)) k.hopSpeed = rnd(170, 230);
        }
        if (stageTime > 3.2f) {
            for (Kid& k : kids) {
                k.armFrontTarget = 0.2f;
                k.armBackTarget = -0.2f;
            }
            leave();
        }
        break;

    default:
        break;
    }
}

void Playground::throwSnowball(Kid& k, SnowCursor* cursor) {
    float hx, hy;
    handOf(k, hx, hy);

    float toX = 0, toY = 0;
    bool atCursor = cursor && cursor->visible && std::fabs(cursor->x - k.x) < 420 && cursor->y > k.y - 320 && chance(0.3f);
    if (atCursor) {
        toX = cursor->x;
        toY = cursor->y;
        k.facing = toX > k.x ? 1 : -1;
    } else {
        std::vector<const Kid*> enemies;
        for (const Kid& other : kids) {
            if (other.team != k.team) enemies.push_back(&other);
        }
        if (enemies.empty()) {      // up on an icon there is nobody to aim at: just out into the air
            toX = k.x + k.facing * rnd(120, 320);
            toY = k.y + 40;
        } else {
            const Kid& target = *enemies[size_t(rnd(0, 0.999f) * enemies.size())];
            toX = target.x + rnd(-26, 26);
            toY = target.y - 30;
            if (chance(0.35f)) toY -= rnd(38, 75);      // not everyone is a good shot: this one sails over
        }
    }
    float flight = std::clamp(std::fabs(toX - hx) / 430.0f, 0.42f, 1.05f);
    launchBall(hx, hy, toX, toY, flight, 4.4f, k.team, atCursor);
}

void Playground::launchBall(float x, float y, float toX, float toY, float flight, float radius, int team, bool atCursor) {
    Snowball ball{};
    ball.x = x;
    ball.y = y;
    ball.vx = (toX - x) / flight;
    ball.vy = (toY - y - 0.5f * SNOWBALL_GRAVITY * flight * flight) / flight;
    ball.team = team;
    ball.atCursor = atCursor;
    ball.radius = radius;
    ball.owner = -1;
    balls.push_back(ball);
    thrown++;
}

// Ready to crouch and jump to this spot.
void Playground::jumpTo(Kid& k, float x, float y, uint8_t kind) {
    k.toX = x;
    k.toY = y;
    k.toKind = kind;
    k.job = Job::CrouchJump;
    k.timer = 0.3f;
    k.facing = x >= k.x ? 1 : -1;
    k.crouchTarget = 0.75f;
    k.armFrontTarget = k.armBackTarget = -0.7f;
}

void Playground::leap(Kid& k) {
    k.fromX = k.x;
    k.fromY = k.y;
    k.jumpT = 0;
    k.arc = 26 + std::max(k.fromY - k.toY, 0.0f) * 0.35f;
    float dx = k.toX - k.x, dy = k.toY - k.y;
    k.jumpTime = std::clamp(std::sqrt(dx * dx + dy * dy) / 300.0f, 0.42f, 1.0f);
    k.feet = Feet::Jump;
    k.onWindow = false;
    k.job = Job::None;
    k.crouchTarget = 0.15f;
    k.armFrontTarget = k.armBackTarget = 2.6f;
}

void Playground::land(Kid& k, World& world) {
    float depth = world.landingBelow(k.toX, k.toY - 14).depth;
    k.x = k.toX;
    k.y = k.toY;
    k.fallSpeed = 0;
    k.crouch = 0.75f;
    k.crouchTarget = 0;
    k.armFrontTarget = 0.2f;
    k.armBackTarget = -0.2f;
    for (int i = 0; i < 14; i++) world.spawnGrain(k.x + rnd(-13, 13), k.y - 5, rnd(-110, 110), rnd(-180, -20));
    (depth >= SOFT_DEPTH ? softLanded : k.toKind == S_ICON ? hardOnIcon : hardLanded)++;

    if (k.toKind == S_ICON) {
        k.feet = Feet::Icon;
        k.targetX = k.x;
        k.job = Job::Pause;
        k.timer = rnd(0.5f, 1.1f);
    } else {
        k.feet = Feet::Ground;
        k.padHeld = false;
        exitKid(k, world);
    }
}

void Playground::exitKid(Kid& k, const World& world) {
    k.job = Job::Exit;
    k.crouchTarget = 0;
    k.holdingBall = false;
    if (k.onWindow) k.sinkTarget = RISE;
    else k.targetX = nearestExit(world, k.x);
}

// The closest icon within a short jump that has snow on it and nobody standing there.
bool Playground::findNeighbour(const World& world, const Kid& k, bool freshOnly, float& x, float& y) {
    float best = NEIGHBOUR * NEIGHBOUR;
    bool found = false;
    for (const Rect& icon : world.iconBoxes()) {
        float cx = (icon.l + icon.r) * 0.5f;
        float dx = cx - k.x, dy = icon.t - k.y;
        float d2 = dx * dx + dy * dy;
        if (d2 >= best || (std::fabs(dx) < 12 && std::fabs(dy) < 40)) continue;
        // Upwards it has to be within a jump as well.
        if (dy < -JUMP_UP) continue;
        bool skip = false;
        if (freshOnly) {
            for (float t : k.tried) skip = skip || std::fabs(t - cx) < 12;
        }
        for (const Kid& other : kids) {
            if (&other != &k && !other.gone && std::fabs(other.x - cx) < 16 && std::fabs(other.y - icon.t) < 40) skip = true;
        }
        Landing top;
        if (skip || !iconReady(world, icon, top)) continue;
        best = d2;
        x = cx;
        y = top.y + 3;
        found = true;
    }
    return found;
}

// Down only onto snow. With none below, on to the next icon; with no icon left in reach, the child
// gathers an armful, throws it down where it will land, and jumps once it is there.
void Playground::decideDescent(Kid& k, World& world) {
    int dir = chance(0.5f) ? 1 : -1;
    bool hasPad = false;
    for (int t = 0; t < 2; t++, dir = -dir) {
        float x = k.x + dir * DROP_SIDE;
        if (x < 24 || x > world.width() - 24) continue;
        Landing below = world.landingBelow(x, k.y - 6);
        if (below.depth >= SOFT_DEPTH) {
            k.padX = x;
            k.padY = below.y;
            k.padHeld = true;
            if (chance(0.3f)) {     // the foot slips, and it is a good thing the snow is there
                k.toX = x;
                k.toY = below.y + 3;
                k.toKind = below.kind;
                leap(k);
                k.arc = 6;
                k.crouchTarget = 0;
                k.surprised = k.jumpTime;
            } else {
                jumpTo(k, x, below.y + 3, below.kind);
            }
            return;
        }
        if (!hasPad) {
            hasPad = true;
            k.padX = x;
            k.padY = below.y;
        }
    }

    float nx, ny;
    if (findNeighbour(world, k, true, nx, ny)) {
        k.tried.push_back(k.x);
        hops++;
        jumpTo(k, nx, ny, S_ICON);
        return;
    }
    k.padHeld = true;
    k.job = Job::Spread;
    k.timer = rnd(2.6f, 3.4f);
    k.scoop = 2.5f;
    k.facing = k.padX > k.x ? 1 : -1;
}

void Playground::stepClimbScene(float dt, World& world, SnowCursor* cursor) {
    bool anyone = false;
    for (size_t n = 0; n < kids.size(); n++) {
        Kid& k = kids[n];
        if (k.gone) continue;
        anyone = true;
        k.timer -= dt;
        if (k.feet == Feet::Icon) k.playLeft -= dt;

        switch (k.job) {
        case Job::Approach: {
            if (k.feet != Feet::Ground || k.sink > 1 || std::fabs(k.x - k.targetX) > 3 || k.timer > 0) break;
            Landing top = world.landingBelow(k.goalX, k.goalTop - 40);
            if (top.kind == S_ICON && top.depth >= SOFT_DEPTH) jumpTo(k, k.goalX, top.y + 3, S_ICON);
            else exitKid(k, world);
            break;
        }
        case Job::CrouchJump:
            if (k.timer <= 0) leap(k);
            break;

        case Job::Pause: {
            if (k.timer > 0) break;
            if (k.playLeft <= 0) {
                k.job = Job::Decide;
                break;
            }
            float roll = rnd(0, 1), nx, ny;
            if (roll < 0.6f) {
                k.job = Job::Gather;
                k.timer = 0.8f;
                k.crouchTarget = 0.8f;
                k.armFrontTarget = k.armBackTarget = 1.0f;
            } else if (roll < 0.82f && findNeighbour(world, k, false, nx, ny)) {
                hops++;
                jumpTo(k, nx, ny, S_ICON);
            } else {
                k.armFrontTarget = 2.7f;
                k.armBackTarget = -0.2f;
                k.hopSpeed = 150;
                k.timer = 1.1f;
            }
            break;
        }
        case Job::Gather:
            if (k.timer > 0) {
                if (chance(dt * 12)) world.scuffFrom(k.x + rnd(-14, 14), k.y - 14);
                break;
            }
            k.crouchTarget = 0;
            k.holdingBall = true;
            k.armFrontTarget = -2.3f;
            k.armBackTarget = 0.7f;
            k.job = Job::Aim;
            k.timer = 0.4f;
            break;

        case Job::Aim:
            if (k.timer > 0) break;
            k.armFrontTarget = 1.5f;
            k.armFront = 0.6f;
            k.holdingBall = false;
            k.facing = chance(0.5f) ? 1 : -1;
            throwSnowball(k, cursor);
            k.job = Job::Pause;
            k.timer = rnd(0.8f, 1.8f);
            break;

        case Job::Decide:
            k.crouchTarget = 0;
            k.armFrontTarget = 0.2f;
            k.armBackTarget = -0.2f;
            decideDescent(k, world);
            break;

        case Job::Spread:
            // Arms wide open, catching the snow, then the armful is hugged together.
            k.scoop = std::min(k.scoop + 3.6f * dt, 14.0f);
            k.armFrontTarget = k.scoop < 8 ? 1.7f : 1.25f;
            k.armBackTarget = k.scoop < 8 ? -1.7f : 1.25f;
            k.crouchTarget = 0;
            if (k.timer <= 0) {
                k.job = Job::Toss;
                k.timer = 0.4f;
                k.armFrontTarget = k.armBackTarget = 2.7f;
            }
            break;

        case Job::Toss: {
            if (k.timer > 0) break;
            float hx, hy;
            handOf(k, hx, hy);
            float fall = std::max(k.padY - hy, 10.0f);
            launchBall(hx, hy, k.padX, k.padY, std::clamp(std::sqrt(2 * fall / SNOWBALL_GRAVITY) * 0.9f, 0.3f, 1.4f), k.scoop, k.team, false);
            balls.back().mass = HEAP_MASS;
            balls.back().stopY = k.padY;
            balls.back().owner = int(n);
            k.scoop = 0;
            k.armFrontTarget = 1.5f;
            k.armBackTarget = 1.5f;
            k.padDone = false;
            k.job = Job::WaitPad;
            heaps++;
            break;
        }
        case Job::WaitPad: {
            if (!k.padDone) break;
            Landing below = world.landingBelow(k.padX, k.y - 6);
            k.padY = below.y;
            if (below.depth >= SOFT_DEPTH) {
                k.armFrontTarget = 0.2f;
                k.armBackTarget = -0.2f;
                jumpTo(k, k.padX, below.y + 3, below.kind);
            } else {
                k.job = Job::Spread;       // not enough yet: one more
                k.timer = rnd(1.8f, 2.4f);
                k.scoop = 2.5f;
            }
            break;
        }
        case Job::Exit:
            if (k.onWindow) k.gone = k.sink >= RISE - 1;
            else if (k.feet == Feet::Ground) k.gone = std::fabs(k.x - k.targetX) < 3;
            break;

        default:
            break;
        }
    }
    if (!anyone) stage = Stage::Leave;
}

void Playground::stepFightScene(float dt, World& world, SnowCursor* cursor) {
    stageTime += dt;
    if (stage == Stage::Arrive) {
        if (allArrived()) {
            stage = Stage::Fight;
            stageTime = 0;
            for (Kid& k : kids) k.throwStep = 0;
        }
        return;
    }
    if (stage == Stage::Cheer) {
        for (Kid& k : kids) {
            k.crouchTarget = 0;
            k.armFrontTarget = 2.7f + 0.3f * std::sin(time * 12 + k.style);
            if (k.hop <= 0 && chance(dt * 2.5f)) k.hopSpeed = rnd(150, 210);
        }
        if (stageTime > 2.4f) {
            for (Kid& k : kids) {
                k.armFrontTarget = 0.2f;
                k.armBackTarget = -0.2f;
            }
            leave();
        }
        return;
    }
    if (stage != Stage::Fight) return;

    if (stageTime > fightLength && balls.empty()) {
        stage = Stage::Cheer;
        stageTime = 0;
        return;
    }

    for (Kid& k : kids) {
        k.facing = k.team == 0 ? 1 : -1;
        k.timer -= dt;
        if (k.surprised > 0) {
            k.crouchTarget = 0;
            k.holdingBall = false;
            k.armFrontTarget = 2.5f;
            k.armBackTarget = 2.5f;
            k.throwStep = 3;
            k.timer = std::max(k.timer, 0.2f);
            continue;
        }
        if (k.timer > 0) continue;
        switch (k.throwStep) {
        case 0:     // scoop up some snow
            if (stageTime > fightLength) break;
            k.crouchTarget = 0.8f;
            k.armFrontTarget = k.armBackTarget = 1.0f;
            k.timer = rnd(0.5f, 0.85f);
            k.throwStep = 1;
            scuffFloor(world, k.x + k.facing * 12.0f, k.onWindow);
            break;
        case 1:     // stand up, ball in hand, arm back
            k.crouchTarget = 0;
            k.holdingBall = true;
            k.armFrontTarget = -2.3f;
            k.armBackTarget = 0.7f;
            k.timer = rnd(0.28f, 0.5f);
            k.throwStep = 2;
            break;
        case 2:     // throw
            k.armFrontTarget = 1.5f;
            k.armFront = 0.6f;
            k.holdingBall = false;
            throwSnowball(k, cursor);
            k.timer = 0.3f;
            k.throwStep = 3;
            break;
        default:    // catch a breath
            k.armFrontTarget = 0.2f;
            k.armBackTarget = -0.2f;
            k.timer = rnd(0.3f, 1.5f);
            k.throwStep = 0;
            break;
        }
    }
}

void Playground::stepSnowballs(float dt, World& world, const Weather& weather, SnowCursor* cursor) {
    auto burst = [&](float x, float y, int count) {
        for (int i = 0; i < count; i++) world.spawnGrain(x + rnd(-4, 4), y + rnd(-4, 4), rnd(-120, 120), rnd(-170, 10));
    };
    for (size_t i = 0; i < balls.size();) {
        Snowball& b = balls[i];
        b.vy += SNOWBALL_GRAVITY * dt;
        if (!b.mass) b.vx += weather.windAt(b.x) * 0.6f * dt;
        b.x += b.vx * dt;
        b.y += b.vy * dt;

        bool done = false;
        if (b.mass && b.y >= b.stopY) {
            world.heap(b.x, b.stopY, b.mass);
            burst(b.x, b.stopY - 3, 18);
            if (b.owner >= 0 && size_t(b.owner) < kids.size()) kids[b.owner].padDone = true;
            done = true;
        }
        if (b.atCursor && cursor && cursor->visible) {
            float dx = b.x - cursor->x, dy = b.y - cursor->y;
            if (dx * dx + dy * dy < 15 * 15) {
                uint32_t seed = uint32_t(time * 1000) | 1;
                for (int d = 0; d < 26; d++) cursor->drop(SnowCursor::M + cursor->hotX + int(rnd(-3, 13)), seed);
                burst(b.x, b.y, 12);
                done = true;
            }
        }
        for (Kid& k : kids) {
            if (done || k.team == b.team) continue;
            float dx = b.x - k.x, dy = b.y - (k.y - 30 * k.scale - k.hop);
            if (dx * dx + dy * dy < 15 * 15) {
                hits++;
                k.surprised = 0.75f;
                k.hopSpeed = 120;
                k.x += b.vx > 0 ? 5.0f : -5.0f;
                k.targetX = k.x;
                burst(b.x, b.y, 16);
                done = true;
            }
        }
        bool overLedge = onWindow && world.solidAt(int(b.x), int(ledgeY) + 2) == S_WINDOW;
        if (!done && !b.mass && b.y >= floorAt(world, b.x, overLedge)) {
            burst(b.x, b.y - 2, 10);
            done = true;
        }
        if (!done && (b.x < -40 || b.x > world.width() + 40)) done = true;

        if (done) {
            balls[i] = balls.back();
            balls.pop_back();
        } else {
            i++;
        }
    }
}

void Playground::crumble(const Snowman& s, World& world, float pushX, float pushY) {
    float y = s.y;
    for (int b = 0; b < 3; b++) {
        float r = s.radius[b];
        int count = int(r * r * 0.55f);
        for (int n = 0; n < count; n++) {
            world.spawnGrain(s.x + rnd(-r, r), y - rnd(0, 2 * r),
                pushX * rnd(0.1f, 0.35f) + rnd(-90, 90), pushY * rnd(0.1f, 0.3f) + rnd(-190, -20));
        }
        y -= r * 1.72f;
    }
}

void Playground::stepSnowmen(float dt, World& world, const Weather& weather, const SnowCursor* cursor) {
    bool building = scene == PlayScene::Snowman && stage != Stage::Leave;
    for (size_t i = 0; i < snowmen.size();) {
        Snowman& s = snowmen[i];
        bool underConstruction = building && i + 1 == snowmen.size();
        bool removed = false;
        if (!underConstruction) {
            float floor = s.onWindow ? world.ledgeLevel(s.x, s.baseY) : world.groundLevel(s.x);
            s.y = approach(s.y, floor + 3, 0.6f, dt);
            s.life -= dt * (1 + 5 * weather.sun());
            if (s.life <= 0) s.melt += dt / 40.0f;

            if (s.onWindow && world.solidAt(int(s.x), int(s.baseY) + 2) != S_WINDOW) {
                crumble(s, world, 0, 0);    // the window it stood on is gone
                removed = true;
            }

            // A pointer swung through the snowman knocks it over.
            if (!removed && cursor && cursor->visible && cursor->speed() > 260) {
                float height = (s.radius[0] + s.radius[1] + s.radius[2]) * 1.75f;
                float dx = cursor->x - s.x, dy = cursor->y - (s.y - height * 0.5f);
                if (std::fabs(dx) < s.radius[0] && std::fabs(dy) < height * 0.5f) {
                    crumble(s, world, cursor->vx, cursor->vy);
                    removed = true;
                }
            }
        }
        if (s.melt >= 1) removed = true;
        if (removed) {
            snowmen.erase(snowmen.begin() + i);
        } else {
            i++;
        }
    }
}

void Playground::step(float dt, World& world, const Weather& weather, SnowCursor* cursor) {
    time += dt;
    sceneTime += dt;

    if (scene == PlayScene::None) {
        nextScene -= dt;
        if (nextScene <= 0) {
            float roll = rnd(0, 1);
            PlayScene pick = wanted != PlayScene::None ? wanted
                : roll < 0.35f ? PlayScene::Snowman : roll < 0.7f ? PlayScene::Fight : PlayScene::Climb;
            if (pick == PlayScene::Snowman && snowmen.size() >= MAX_SNOWMEN) pick = PlayScene::Fight;
            bool started = startScene(pick, world);
            // When this one does not fit, the other kind of play may.
            if (!started && wanted == PlayScene::None) started = startScene(pick == PlayScene::Climb ? PlayScene::Fight : PlayScene::Climb, world);
            if (!started) nextScene = 15;
        }
    } else {
        checkLedge(world);
        if (stage == Stage::Leave) {
            bool anyone = false;
            for (Kid& k : kids) {
                if (k.onWindow ? k.sink >= RISE - 1 : k.feet == Feet::Ground && std::fabs(k.x - k.targetX) < 3) k.gone = true;
                anyone = anyone || !k.gone;
            }
            if (!anyone) {
                kids.clear();
                scene = PlayScene::None;
                stage = Stage::Idle;
                onWindow = false;
                nextScene = wanted != PlayScene::None ? 0 : rnd(70, 200);
            }
        } else if (scene == PlayScene::Snowman) {
            stepSnowmanScene(dt, world);
            if (stage != Stage::Cheer && stage != Stage::Decorate && stage != Stage::Leave) checkSnowUnderfoot(dt, world);
        } else if (scene == PlayScene::Fight) {
            stepFightScene(dt, world, cursor);
            if (stage == Stage::Arrive || stage == Stage::Fight) checkSnowUnderfoot(dt, world);
        } else {
            stepClimbScene(dt, world, cursor);
        }
        for (Kid& k : kids) {
            if (!k.gone) moveKid(k, dt, world, weather);
        }
    }

    stepSnowballs(dt, world, weather, cursor);
    stepSnowmen(dt, world, weather, cursor);

    world.clearGuards();
    for (const Kid& k : kids) {
        if (k.padHeld && !k.gone) {
            // padY follows the top of the snow, so the guard reaches well below it, down to the surface.
            world.guard({ int(k.padX) - 44, int(k.padY) - 34, int(k.padX) + 44, int(k.padY) + 64 });
        }
    }

    drawn.clear();
    for (const Snowman& s : snowmen) drawSnowman(s);
    if (ballRolling && scene == PlayScene::Snowman) drawSnowBall(ballX, ballY, ballR, ballSpin);
    for (const Kid& k : kids) {
        if (!k.gone) drawKid(k, weather);
    }
    if (ballRolling && stage == Stage::CarryBall) drawSnowBall(ballX, ballY, ballR, ballSpin);
    for (const Snowball& b : balls) {
        drawSnowBall(b.x, b.y, b.radius, 0);
    }
}

int Playground::kidsOnIcons() const {
    int n = 0;
    for (const Kid& k : kids) n += k.feet == Feet::Icon && !k.gone ? 1 : 0;
    return n;
}

int Playground::kidsSpreading() const {
    int n = 0;
    for (const Kid& k : kids) n += k.job == Job::Spread && !k.gone ? 1 : 0;
    return n;
}

void Playground::add(float ax, float ay, float bx, float by, float ra, float rb, uint32_t color) {
    drawn.push_back({ ax, ay, bx, by, ra, rb, color, 0 });
}

void Playground::drawSnowBall(float x, float y, float r, float spin) {
    circle(x, y, r, SNOW_SHADE);
    circle(x - r * 0.1f, y - r * 0.13f, r * 0.88f, SNOW_LIGHT);
    if (spin != 0 && r > 7) circle(x + std::cos(spin) * r * 0.55f, y + std::sin(spin) * r * 0.55f, r * 0.13f, SNOW_SHADE);
}

// Shoulders and hands follow from the same pose in drawKid and here.
void Playground::handOf(const Kid& k, float& x, float& y) const {
    float s = k.scale, f = float(k.facing);
    float lean = 8 * k.crouch;
    float shoulderX = k.x + f * lean * s;
    float shoulderY = k.y + k.sink - k.hop + (-33 + 9 * k.crouch) * s;
    x = shoulderX + f * std::sin(k.armFront) * 15 * s;
    y = shoulderY + std::cos(k.armFront) * 15 * s;
}

void Playground::drawKid(const Kid& k, const Weather& weather) {
    const Outfit& o = OUTFITS[k.style];
    float s = k.scale, f = float(k.facing);
    float ox = k.x, oy = k.y + k.sink - k.hop;
    float c = k.crouch;
    float bob = k.walking ? std::fabs(std::sin(k.phase)) * 1.8f : 0;
    float breathe = k.walking ? 0 : std::sin(time * 2.2f + k.style) * 0.5f;
    auto px = [&](float x) { return ox + f * x * s; };
    auto py = [&](float y) { return oy + y * s; };

    float hipY = -20 + 9 * c - bob;
    float lean = 8 * c;
    float chestY = -33 + 9 * c - bob + breathe;
    float headX = lean * 1.2f, headY = -44 + 9 * c - bob + breathe;
    if (k.surprised > 0) headY -= 2;

    // Legs: a walk swings them, a crouch bends them outwards.
    float stride = k.walking ? std::sin(k.phase) * 9 : 3.5f;
    float liftA = k.walking ? std::max(std::cos(k.phase), 0.0f) * 4.5f : 0;
    float liftB = k.walking ? std::max(-std::cos(k.phase), 0.0f) * 4.5f : 0;
    float kneeOut = 6 * c;

    auto leg = [&](float footX, float lift, float side) {
        float kneeX = footX * 0.5f + side * kneeOut + kneeOut;
        float kneeY = (hipY - lift) * 0.5f - 2 * c;
        add(px(0), py(hipY), px(kneeX), py(kneeY), 4.6f * s, 4.1f * s, o.trousers);
        add(px(kneeX), py(kneeY), px(footX), py(-3 - lift), 4.1f * s, 3.8f * s, o.trousers);
        add(px(footX - 1), py(-2.6f - lift), px(footX + 3.5f), py(-2.4f - lift), 3.5f * s, 3.2f * s, BOOT);
    };

    auto arm = [&](float angle, uint32_t sleeve) {
        float sx = lean, sy = chestY + 1;
        float hx = sx + std::sin(angle) * 15, hy = sy + std::cos(angle) * 15;
        add(px(sx), py(sy), px(hx), py(hy), 3.9f * s, 3.2f * s, sleeve);
        circle(px(hx), py(hy), 3.8f * s, o.mitten);
    };

    arm(k.armBack, o.coatDark);
    leg(-stride, liftB, -1);
    leg(stride, liftA, 1);

    // Puffy coat with a darker hem.
    add(px(0), py(hipY - 2), px(lean), py(chestY), 10 * s, 9 * s, o.coat);
    add(px(-7.5f), py(hipY + 2.5f), px(7.5f), py(hipY + 2.5f), 2.8f * s, 2.8f * s, o.coatDark);

    // Hat first, the face over it, then the band: the hat ends up wrapped around the head.
    circle(px(headX), py(headY - 2), 10.2f * s, o.hat);
    circle(px(headX + 0.8f), py(headY + 2.2f), 8.3f * s, o.skin);
    add(px(headX - 8.6f), py(headY - 3.6f), px(headX + 8.6f), py(headY - 3.6f), 2.5f * s, 2.5f * s, o.hatBand);
    circle(px(headX - 1.5f), py(headY - 13), 3.5f * s, SNOW_LIGHT);

    // Eyes look the way the child faces; a fright makes them round.
    float eyeR = (k.surprised > 0 ? 1.9f : 1.35f) * s;
    circle(px(headX + 1.2f), py(headY + 1.6f), eyeR, COAL);
    circle(px(headX + 6.4f), py(headY + 1.6f), eyeR, COAL);
    circle(px(headX + 0), py(headY + 5.2f), 1.9f * s, rgb(240, 130, 120, 120));
    circle(px(headX + 7.4f), py(headY + 5.2f), 1.7f * s, rgb(240, 130, 120, 120));
    bool happy = stage == Stage::Cheer || k.hop > 4;
    if (k.surprised > 0 || happy) circle(px(headX + 3.8f), py(headY + 6.2f), (happy ? 2 : 1.6f) * s, rgb(150, 56, 56));
    else add(px(headX + 2.4f), py(headY + 6), px(headX + 5.2f), py(headY + 6), 0.75f * s, 0.75f * s, rgb(150, 70, 66));

    // The scarf tail streams with the wind.
    float neckY = headY + 9.5f;
    add(px(headX - 6.5f), py(neckY), px(headX + 6.5f), py(neckY), 3.3f * s, 3.3f * s, o.scarf);
    float wind = std::clamp(weather.windAt(k.x) / 170.0f, -1.0f, 1.0f);
    float flutter = std::sin(time * (5 + 5 * std::fabs(wind)) + k.style) * 2;
    float tailX = px(headX - 4), tailY = py(neckY + 1);
    float endX = tailX + (wind * 14 - f * 3) * s, endY = tailY + (9 - 6 * std::fabs(wind) + flutter) * s;
    add(tailX, tailY, endX, endY, 2.5f * s, 2 * s, o.scarf);

    arm(k.armFront, o.coat);

    if (k.holdingBall) {
        float hx, hy;
        handOf(k, hx, hy);
        if (scene != PlayScene::Snowman) drawSnowBall(hx, hy - 2, 4.4f, 0);
    }
    if (k.scoop > 0) {
        float hx, hy;
        handOf(k, hx, hy);
        if (k.armBack < 0) {
            // Still catching: a little snow in each open hand.
            float backX = ox + f * (lean + std::sin(k.armBack) * 15) * s;
            float backY = py(chestY + 1 + std::cos(k.armBack) * 15);
            drawSnowBall(hx, hy - 2, k.scoop * 0.6f, 0);
            drawSnowBall(backX, backY - 2, k.scoop * 0.6f, 0);
        } else {
            drawSnowBall(hx, hy - k.scoop * 0.5f, k.scoop, 0);
        }
    }
}

void Playground::drawSnowman(const Snowman& s) {
    float shrink = 1 - s.melt * 0.8f;
    float y = s.y;
    float headX = s.x, headY = s.y;
    float bodyY = s.y, bodyR = 0, headR = 0;
    float sway = s.melt * 4 * std::sin(s.x);

    for (int i = 0; i < 3 && s.radius[i] > 0; i++) {
        float r = s.radius[i] * shrink;
        float cy = y - r * 0.92f;
        drawSnowBall(s.x + sway * i, cy, r, 0);
        if (i == 1) { bodyY = cy; bodyR = r; }
        if (i == 2) { headX = s.x + sway * i; headY = cy; headR = r; }
        y = cy - r * 0.8f;
    }
    if (headR <= 0) return;

    float d = s.decor;
    float r = headR;
    auto shown = [&](float step) { return d >= step; };
    if (shown(1)) {         // coal eyes
        circle(headX - r * 0.38f, headY - r * 0.2f, r * 0.13f, COAL);
        circle(headX + r * 0.38f, headY - r * 0.2f, r * 0.13f, COAL);
    }
    if (shown(2)) add(headX + r * 0.2f, headY + r * 0.1f, headX + r * 1.25f, headY + r * 0.2f, r * 0.2f, 0.4f, CARROT);
    if (shown(3)) {         // a smile of coal
        for (int i = -2; i <= 2; i++) {
            circle(headX + i * r * 0.22f, headY + r * (0.58f - 0.03f * i * i * -1.6f), r * 0.07f, COAL);
        }
    }
    if (shown(4)) {         // stick arms
        float ax = bodyR * 0.8f;
        add(s.x - ax, bodyY - 2, s.x - ax - 19, bodyY - 13, 1.5f, 1, STICK);
        add(s.x - ax - 13, bodyY - 9, s.x - ax - 18, bodyY - 4, 1, 0.7f, STICK);
        add(s.x + ax, bodyY - 2, s.x + ax + 19, bodyY - 12, 1.5f, 1, STICK);
        add(s.x + ax + 13, bodyY - 9, s.x + ax + 17, bodyY - 16, 1, 0.7f, STICK);
    }
    if (shown(5)) {         // buttons
        circle(s.x, bodyY - bodyR * 0.35f, 1.7f, COAL);
        circle(s.x, bodyY + bodyR * 0.2f, 1.7f, COAL);
    }
    if (shown(6)) {         // scarf
        uint32_t red = rgb(212, 58, 62);
        float neck = headY + r * 0.92f;
        add(headX - r * 0.8f, neck, headX + r * 0.8f, neck, 2.8f, 2.8f, red);
        add(headX + r * 0.35f, neck + 1, headX + r * 0.7f, neck + 12, 2.5f, 2, red);
    }
    if (shown(7)) {         // top hat
        float top = headY - r * 0.82f;
        add(headX - r * 0.95f, top, headX + r * 0.95f, top, 1.7f, 1.7f, HAT_BLACK);
        add(headX, top - 2, headX, top - r * 0.95f, r * 0.58f, r * 0.58f, HAT_BLACK);
        add(headX - r * 0.56f, top - 3.5f, headX + r * 0.56f, top - 3.5f, 1.3f, 1.3f, rgb(200, 52, 58));
    }
}
