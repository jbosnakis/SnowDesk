// Children who come out to play in the snow: building a snowman or having a snowball fight.
// They are drawn from soft capsules, so they stay crisp at any size and animate freely. No Win32.
#pragma once
#include <cstdint>
#include <vector>
#include "world.h"

// A capsule from a to b with its radius going from ra to rb. A circle has a == b.
struct Shape {
    float ax, ay, bx, by;
    float ra, rb;
    uint32_t color;     // 0xAABBGGRR
    float padding;
};

enum class PlayScene { None, Snowman, Fight, Climb };

class Playground {
public:
    void init(uint32_t seed);
    void request(PlayScene scene);      // start this next, as soon as there is room
    void step(float dt, World& world, const Weather& weather, SnowCursor* cursor);
    void windowMoved(const Rect& before, int dx, int dy);   // children on it ride along
    const std::vector<Shape>& shapes() const { return drawn; }
    size_t kidCount() const { return kids.size(); }
    size_t snowmanCount() const { return snowmen.size(); }
    float sceneSpot() const { return spot; }
    int ballsThrown() const { return thrown; }
    int ballsLanded() const { return hits; }
    PlayScene currentScene() const { return scene; }
    bool playingOnWindow() const { return onWindow; }
    float ledgeHeight() const { return ledgeY; }
    int kidsOnIcons() const;
    int kidsSpreading() const;
    int softLandings() const { return softLanded; }
    int hardLandings() const { return hardLanded + hardOnIcon; }
    int hardOnIcons() const { return hardOnIcon; }
    int heapsThrown() const { return heaps; }
    int iconHops() const { return hops; }

private:
    enum class Stage { Idle, Arrive, FetchBall, RollBall, CarryBall, PlaceBall, Decorate, Fight, Cheer, Climb, Leave };
    enum class Feet { Ground, Jump, Fall, Icon };
    enum class Job { None, Approach, CrouchJump, Pause, Gather, Aim, Decide, Spread, Toss, WaitPad, Exit };

    struct Kid {
        float x = 0, y = 0;
        float targetX = 0, speed = 115;
        int facing = 1;
        float phase = 0, crouch = 0, crouchTarget = 0;
        float armFront = 0.2f, armBack = -0.2f, armFrontTarget = 0.2f, armBackTarget = -0.2f;
        float hop = 0, hopSpeed = 0;
        float surprised = 0;
        float scale = 1;
        int style = 0;
        int team = 0;
        float timer = 0;
        int throwStep = 0;
        bool walking = false;
        bool holdingBall = false;
        bool gone = false;

        // On a window, or on the way up from behind one.
        bool onWindow = false;
        float sink = 0, sinkTarget = 0, sinkSpeed = 75;

        // Off the ground.
        Feet feet = Feet::Ground;
        Job job = Job::None;
        float fromX = 0, fromY = 0, toX = 0, toY = 0, arc = 0, jumpT = 0, jumpTime = 1, fallSpeed = 0;
        uint8_t toKind = S_GROUND;
        float goalX = 0, goalTop = 0;   // the icon a climber is heading for
        float playLeft = 0;
        float scoop = 0;                // the armful being gathered, by radius
        float padX = 0, padY = 0;       // where this child will come down
        bool padHeld = false, padDone = false;
        std::vector<float> tried;       // icons already left behind
    };

    struct Snowman {
        float x = 0, y = 0;
        float radius[3] = { 0, 0, 0 };
        float decor = 0;        // how many of the decorations are on
        float life = 0;
        float melt = 0;         // 0 whole .. 1 gone
        bool onWindow = false;
        float baseY = 0;
    };

    struct Snowball {
        float x, y, vx, vy;
        int team;
        bool atCursor;
        float radius;
        int mass;       // snow it leaves where it lands, when it is an armful for a child to jump on
        float stopY;
        int owner;
    };

    struct Spot { float left, right, base; bool window; };
    struct Leap { float launchX, base; bool window; Rect icon; };

    float rnd(float a, float b);
    bool chance(float p) { return rnd(0, 1) < p; }

    float floorAt(const World& world, float x, bool window) const;
    void scuffFloor(World& world, float x, bool window);
    float floorDepth(const World& world, float x) const;
    void addSpots(const World& world, float from, float to, float base, bool window, std::vector<Spot>& out);
    float hiddenEdge(const World& world, float x, int dir) const;
    float nearestExit(const World& world, float x) const;
    bool startScene(PlayScene scene, World& world);
    bool startClimb(World& world);
    bool iconReady(const World& world, const Rect& icon, Landing& top) const;
    Kid makeKid(int style, float x, float toX);
    void riseFromWindow(Kid& k, const World& world);

    void moveKid(Kid& k, float dt, World& world, const Weather& weather);
    bool allArrived() const;
    void stepSnowmanScene(float dt, World& world);
    void stepFightScene(float dt, World& world, SnowCursor* cursor);
    void stepClimbScene(float dt, World& world, SnowCursor* cursor);
    void jumpTo(Kid& k, float x, float y, uint8_t kind);
    void leap(Kid& k);
    void land(Kid& k, World& world);
    void exitKid(Kid& k, const World& world);
    bool findNeighbour(const World& world, const Kid& k, bool freshOnly, float& x, float& y);
    void decideDescent(Kid& k, World& world);
    void checkSnowUnderfoot(float dt, const World& world);
    void checkLedge(const World& world);
    void throwSnowball(Kid& k, SnowCursor* cursor);
    void launchBall(float x, float y, float toX, float toY, float flight, float radius, int team, bool atCursor);
    void stepSnowballs(float dt, World& world, const Weather& weather, SnowCursor* cursor);
    void stepSnowmen(float dt, World& world, const Weather& weather, const SnowCursor* cursor);
    void crumble(const Snowman& s, World& world, float pushX, float pushY);
    void leave();
    void abandon();

    void add(float ax, float ay, float bx, float by, float ra, float rb, uint32_t color);
    void circle(float x, float y, float r, uint32_t color) { add(x, y, x, y, r, r, color); }
    void drawSnowBall(float x, float y, float r, float spin);
    void drawKid(const Kid& k, const Weather& weather);
    void drawSnowman(const Snowman& s);
    void handOf(const Kid& k, float& x, float& y) const;

    uint32_t rng = 1;
    float time = 0, sceneTime = 0, nextScene = 25, stageTime = 0;
    PlayScene scene = PlayScene::None, wanted = PlayScene::None;
    Stage stage = Stage::Idle;
    std::vector<Kid> kids;
    std::vector<Snowman> snowmen;
    std::vector<Snowball> balls;
    std::vector<Shape> drawn;

    // The snowman being built.
    float spot = 0;
    int ballIndex = 0;
    int side = 1;
    float ballX = 0, ballY = 0, ballR = 0, ballSpin = 0;
    bool ballRolling = false;
    float placeFromX = 0, placeFromY = 0;
    float fightLength = 0;
    int thrown = 0, hits = 0;
    float exitLeft = 0, exitRight = 0;

    // Where the scene is being played.
    bool onWindow = false;
    float ledgeY = 0;
    float lowSnowTime = 0, snowCheck = 0;
    int softLanded = 0, hardLanded = 0, hardOnIcon = 0, heaps = 0, hops = 0;
};
