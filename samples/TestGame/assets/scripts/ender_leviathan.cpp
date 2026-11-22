// The Ender Leviathan of the Arcan mod (its EnderLeviathan.java,
// motion/LeviathanMotion.java, LeviathanBolt.java, LeviathanMeteor.java), ported
// to GameWorldshaper for TestGame.
//
// The creature runs on the mod's own clock, 20 ticks a second, with the mod's
// steering (it flies the way it faces, slows for sharp turns, feels for terrain
// ahead), its attacks, its teleport, its roll (banking, rolling its back or belly
// toward a target, barrel rolls), the aim of its head and the twist of its body.
// Its body is posed as the mod poses it, over the clips: a swimming wave, a body
// that follows the path it flew (a turn passes down it as a bend), a neck that
// carries the aim, fins that beat and sweep back with speed. Between two ticks it
// is drawn where it is in between, so it moves smoothly at any frame rate.
//
// Animation hooks: the blows land when the clip says so. The clips carry their
// cues (assets/models/ender_leviathan/ender_leviathan.events): a bolt leaves its
// port, the jaws close, the tail whips, the fins throw the current, the beam
// fires, each as the clip passes it (the gameplay event "anim.<cue>"). Every cue
// also has its tick from the mod: should a clip's event not come (an engine
// before 0.8.57), the blow lands on time anyway.
//
// Around it is TestGame's encounter (COMBAT_UPDATE.md): party scaling, 60,000
// health, healing crystals, ammunition, Oracles, Void Brand, the collapse, wipe
// and reset, the HUD. Its blows hurt as the mod's do x1.25 (TestGame's balance:
// tail 20, bite 30, contact 10, current 8); it summons nobody.
//
// Test hooks (process-local, never saved): GWS_LEVIATHAN_TEST=<salvo|beam|roar|
// meteors|tail|current|bite|oracles|oracle_shoot|brand|collapse|death|teleport|
// patrol> (an attack's name with "+" after it: chosen, so it flies to it first),
// GWS_LEVIATHAN_TEST_SPOT=x,y,z (where the tests hold the player, from home),
// GWS_LEVIATHAN_LOG=1 (what it does, twice a second, and every hook).
#include "schizo_script.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace leviathan {

constexpr float PI = 3.14159265358979f;
constexpr float DEG = PI / 180.0f;      // degrees -> radians
constexpr float TICK = 0.05f;           // the mod's tick
constexpr float LIFT = 4.7f;            // MODEL_LIFT: the body's middle above its position
constexpr float HURT = 1.25f;           // TestGame's balance over the mod's damage
constexpr float BITE_ABOVE = 8.0f;      // BITE_MIN_HEIGHT: it bites only who is this high above the ground

// ---- vectors and the body's frame ------------------------------------------
struct V {
    float x = 0, y = 0, z = 0;
    V operator+(V b) const { return {x + b.x, y + b.y, z + b.z}; }
    V operator-(V b) const { return {x - b.x, y - b.y, z - b.z}; }
    V operator*(float k) const { return {x * k, y * k, z * k}; }
};
inline float dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(V a) { return std::sqrt(dot(a, a)); }
inline float hlen(V a) { return std::sqrt(a.x * a.x + a.z * a.z); }
inline V flat(V a) { return {a.x, 0, a.z}; }
inline V unit(V a, V fallback = {0, 0, 1}) { const float n = length(a); return n > 1e-4f ? a * (1 / n) : fallback; }
inline V mix(V a, V b, float t) { return a + (b - a) * t; }
inline float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }
inline float hdist(V a, V b) { return hlen(a - b); }
/// Minecraft's wrapDegrees and approachDegrees.
inline float wrap(float d) { d = std::fmod(d, 360.0f); if (d >= 180) d -= 360; if (d < -180) d += 360; return d; }
inline float approach(float now, float goal, float step) { return now + clampf(wrap(goal - now), -step, step); }
inline float seg_dist(V p, V a, V b) {
    const V d = b - a;
    const float t = clampf(dot(p - a, d) / std::max(1e-6f, dot(d, d)), 0, 1);
    return length(p - (a + d * t));
}

/// The body's axes (the mod's Frame): yaw and pitch as Minecraft has them
/// (degrees; yaw 0 faces +z, pitch + is nose down), roll + lifts the right side.
struct Frame { V forward, up, right; };
inline Frame frame(float yaw, float pitch, float roll) {
    const float sy = std::sin(yaw * DEG), cy = std::cos(yaw * DEG), sp = std::sin(pitch * DEG), cp = std::cos(pitch * DEG);
    Frame f;
    f.forward = {-sy * cp, -sp, cy * cp};
    const V level{-sp * sy, cp, sp * cy};   // straight up, made square to forward
    const V left = cross(level, f.forward);
    f.up = level * std::cos(roll * DEG) + left * std::sin(roll * DEG);
    f.right = cross(f.forward, f.up);
    return f;
}
/// The engine's Euler angles (degrees, R = Rz Ry Rx) that turn the model (its +x
/// the body's left, +y up, +z forward: the mod's render space turned round) to a frame.
inline void euler_of(const Frame& f, float out[3]) {
    const V c0 = f.right * -1.0f, c1 = f.up, c2 = f.forward;   // the rotation's columns
    const float s = clampf(-c0.z, -1, 1);
    float x, z;
    const float y = std::asin(s);
    if (std::abs(s) < 0.99999f) { x = std::atan2(c1.z, c2.z); z = std::atan2(c0.y, c0.x); }
    else { z = 0; x = std::atan2(-c2.y, c1.y); }
    out[0] = x / DEG; out[1] = y / DEG; out[2] = z / DEG;
}

/// 3x3 rotations, for re-expressing a bone's turn in its joint's own axes.
struct M3 { float m[3][3]; };
inline M3 mul(const M3& a, const M3& b) {
    M3 r{};
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) for (int k = 0; k < 3; ++k) r.m[i][j] += a.m[i][k] * b.m[k][j];
    return r;
}
inline M3 rot_x(float t) { const float c = std::cos(t), s = std::sin(t); return {{{1, 0, 0}, {0, c, -s}, {0, s, c}}}; }
inline M3 rot_y(float t) { const float c = std::cos(t), s = std::sin(t); return {{{c, 0, s}, {0, 1, 0}, {-s, 0, c}}}; }
inline M3 rot_z(float t) { const float c = std::cos(t), s = std::sin(t); return {{{c, -s, 0}, {s, c, 0}, {0, 0, 1}}}; }
/// Euler angles (degrees) with R = Rz Ry Rx: what the engine's rotations take.
inline void euler_zyx(const M3& r, float out[3]) {
    const float s = clampf(-r.m[2][0], -1, 1), y = std::asin(s);
    float x, z;
    if (std::abs(s) < 0.99999f) { x = std::atan2(r.m[2][1], r.m[2][2]); z = std::atan2(r.m[1][0], r.m[0][0]); }
    else { z = 0; x = std::atan2(-r.m[1][2], r.m[1][1]); }
    out[0] = x / DEG; out[1] = y / DEG; out[2] = z / DEG;
}
/// A bone's turn in the mod's terms (radians: GeckoLib's x, y, z, applied z, y,
/// x) as the engine's degrees about that bone's joint. Two steps: the mod's
/// space is the glTF's turned round (x and z the other way), and the rig's joints
/// keep Blender's axes (its root stands them up: a joint's z is up, -y forward).
/// So a turn about the model's x stays about x, about its up (y) is about the
/// joint's z, and about its forward (z) is about the joint's -y.
inline void joint_degrees(const float r[3], float out[3]) {
    const float x = -r[0], y = r[1], z = -r[2];                   // in the glTF's axes
    euler_zyx(mul(mul(rot_y(-z), rot_z(y)), rot_x(x)), out);      // R_gltf = Rz Ry Rx, in the joint's axes
}

// ---- the model -----------------------------------------------------------------
/// A point of the mod's model (render space: pixels, +x the creature's right,
/// front -z) in the glTF, which is that space turned round, in metres, lifted.
inline V gltf(float x, float y, float z) { return {-x / 16, y / 16 + LIFT, -z / 16}; }
const V MOUTH = {0, LIFT, 18.875f};     // the mouth locator, on the head

/// The pieces of its hit box (PART_SPECS): a point of a bone and a box round it.
struct PartSpec { const char* name; const char* bone; float x, y, z, width, height; };
const PartSpec PARTS[15] = {
    {"head", "head", 0, 0, -240, 7.5f, 7.0f},
    {"neck", "neck_mid", 0, -2, -138, 6.0f, 6.0f},
    {"neck_base", "neck_base", 0, -9, -97, 6.5f, 6.5f},
    {"trunk_rear", "body_rear", 0, -5, 182, 7.6f, 7.3f},
    {"haunch", "haunch", 0, 0, 267, 5.9f, 6.0f},
    {"tail_1", "tail_1", 0, -2, 336, 5.0f, 5.0f},
    {"tail_2", "tail_2", 0, 2, 388, 4.4f, 4.8f},
    {"tail_3", "tail_3", 0, -2, 442, 3.8f, 3.5f},
    {"tail_4", "tail_4", 0, 5, 497, 3.4f, 4.0f},
    {"tail_5", "tail_5", 0, 0, 552, 3.0f, 3.0f},
    {"fluke", "tail_fluke", 0, 0, 644, 7.5f, 2.6f},
    {"pectoral_a", "pectoral_left", -83, 0, 124, 7.0f, 1.6f},
    {"pectoral_b", "pectoral_right", 80, 0, 133, 7.0f, 1.6f},
    {"pelvic_a", "pelvic_left", -71, 0, 339, 7.0f, 1.6f},
    {"pelvic_b", "pelvic_right", 68, 0, 347, 7.0f, 1.6f}};
enum { P_HEAD = 0, P_TAIL_3 = 7, P_FLUKE = 10, P_PECTORAL_L = 11, P_PECTORAL_R = 12 };
/// The scene's hit regions ("Leviathan hit <i>") and the piece each one is.
const int PROXY_PART[15] = {0, 1, 3, 4, 6, 8, 10, 11, 12, 2, 5, 7, 9, 13, 14};

/// The launcher ports (locators) and the ticks at which each fires.
const char* const PORTS[9] = {"projectile_neck", "projectile_front", "projectile_right_1", "projectile_left_1",
                              "projectile_right_2", "projectile_left_2", "projectile_tail_1", "projectile_tail_2",
                              "projectile_tail_3"};
const int PORT_TICKS[9] = {21, 24, 27, 29, 32, 35, 38, 41, 43};
const int STROKES[3] = {26, 46, 66};    // REPULSION_STROKES
const float TAIL_RADII[4] = {5.5f, 6.5f, 8.0f, 9.5f};
/// Where the tail_strike clip has the tail (right, up, back from the body's
/// middle): the mod's table, used only when the engine cannot say where the bones are.
const float TAIL_TIMES[5] = {1.07f, 1.23f, 1.40f, 1.57f, 1.73f};
const float TAIL_OFFSETS[5][4][3] = {
    {{6, -2, 27}, {8, -2, 30}, {9, -3, 33}, {10, -3, 38}},
    {{10, -2, 24}, {13, -2, 25}, {17, -2, 26}, {22, -2, 26}},
    {{9, -2, 25}, {12, -2, 26}, {16, -2, 26}, {21, -2, 26}},
    {{7, -1, 26}, {10, -1, 28}, {13, -1, 29}, {18, -2, 30}},
    {{6, -1, 27}, {8, -1, 29}, {11, -1, 31}, {16, -1, 34}}};

struct ClipInfo { const char* name; float seconds; };
const ClipInfo CLIPS[] = {{"idle", 6.4f}, {"bite", 1.4f}, {"roar", 2.8f}, {"dorsal_salvo", 3.6f}, {"mouth_beam", 4.4f},
                          {"hurt", .65f}, {"death", 4.8f}, {"tail_strike", 3.4f}, {"repulsion_current", 4.8f}};

// ---- the body, worked out as it flies (LeviathanMotion) -------------------------
constexpr int NB = 23;
const char* const BONES[NB] = {"body_front",
    "body_rear", "haunch", "tail_1", "tail_2", "tail_3", "tail_4", "tail_5", "tail_fluke",
    "neck_base", "neck_mid", "neck", "head",
    "pectoral_right", "pectoral_right_tip", "pelvic_right", "pelvic_right_tip",
    "pectoral_left", "pectoral_left_tip", "pelvic_left", "pelvic_left_tip",
    "fluke_left", "fluke_right"};
using Pose = std::array<std::array<float, 3>, NB>;   // radians, the mod's (GeckoLib's) terms

struct Motion {
    static constexpr int H = 64;
    double yaw[H]{}, pitch[H]{}, roll[H]{};
    int newest = -1, samples = 0;
    double phase = 0, speed = 0, aim_yaw = 0, aim_pitch = 0, twist = 0, effort = 1;
    double clip_w = 0, clip_goal = 0, wind = 0, slam = 0;

    void push(double w, double s) { wind += (w - wind) * .6; slam += (s - slam) * .7; }
    void tick(double y, double p, double r, double blocks_per_tick) {
        newest = (newest + 1) % H;
        yaw[newest] = y; pitch[newest] = p; roll[newest] = r;
        samples = std::min(H, samples + 1);
        speed += (blocks_per_tick - speed) * .12;
        clip_w += (clip_goal - clip_w) * .18;
        phase += .10 + speed * .20;
        if (phase > 1e6) phase -= 1e6;
    }
    /// After a teleport: the body forgets the way it came.
    void settle(double y, double p, double r) {
        for (int i = 0; i < H; ++i) { yaw[i] = y; pitch[i] = p; roll[i] = r; }
        samples = std::max(samples, 1);
        if (newest < 0) newest = 0;
    }
    double back(const double* s, double ago) const {
        if (samples == 0) return 0;
        const double c = std::min(std::max(ago, 0.0), double(samples - 1));
        const int lo = int(std::floor(c));
        const double f = c - lo;
        const double a = s[(newest - lo + H * 2) % H], b = s[(newest - std::min(lo + 1, samples - 1) + H * 2) % H];
        return a + (b - a) * f;
    }
    static double wrapd(double d) { d = std::fmod(d, 360.0); if (d >= 180) d -= 360; if (d < -180) d += 360; return d; }

    Pose pose() const {
        static const double REAR_LAG[8] = {2.5, 5, 7.5, 10, 12.5, 15, 18, 21};
        static const double NECK_SHARE[4] = {.22, .30, .28, .20};
        const double R = 3.14159265358979 / 180.0;
        Pose p{};
        auto add = [&](int b, double x, double y, double z) {
            p[b][0] += float(x); p[b][1] += float(y); p[b][2] += float(z);
        };
        const double act = std::min(1.0, std::max(.3, speed / .9)) * effort;
        const double lag = std::min(1.0, speed / .45);
        // The trunk breathes with the wave.
        add(0, .03 * act * std::sin(phase - .2), 0, 0);
        // Each part behind it turns by the change of heading between it and the
        // part in front (where the body was a few ticks ago), plus the wave and the twist.
        double previous = 0;
        for (int i = 0; i < 8; ++i) {
            const double along = (i + 1) / 8.0;
            const double dy = wrapd(back(yaw, REAR_LAG[i]) - back(yaw, previous)) * lag;
            const double dp = wrapd(back(pitch, REAR_LAG[i]) - back(pitch, previous)) * lag;
            const double dr = wrapd(back(roll, REAR_LAG[i]) - back(roll, previous)) * lag;
            double rx = -dp * R * .9, ry = -dy * R * .9, rz = dr * R * .9;
            previous = REAR_LAG[i];
            const double amplitude = (.025 + .085 * along * along) * act;
            rx += amplitude * std::sin(phase - i * .62);
            ry += amplitude * .45 * std::sin(phase * .5 - i * .5 + 1.3) * (.4 + along);
            rz += twist / 8;
            add(1 + i, rx, ry, rz);
        }
        // The neck counter-sways against the tail and carries the aim.
        for (int i = 0; i < 4; ++i)
            add(9 + i, aim_pitch * NECK_SHARE[i] + .02 * act * std::sin(phase + 1.9 + i * .5),
                -aim_yaw * NECK_SHARE[i] + .015 * act * std::sin(phase * .5 + i), -twist * .12 / 4);
        // Fins: they beat with the wave, the tips a little behind, sweep back with
        // speed, tuck in a hard climb or dive, and trail in a roll.
        const double roll_rate = wrapd(back(roll, 0) - back(roll, 3)) * R / 3;
        const double pitch_rate = wrapd(back(pitch, 0) - back(pitch, 3)) / 3;
        const double climb = std::max(0.0, std::min(1.0, -back(pitch, 0) / 45));
        const double beat = .45 * act * (1 + .35 * climb) * std::sin(phase * .5);
        const double sweep = .10 + .30 * std::min(1.0, speed / 1.4) + .25 * std::min(1.0, std::abs(pitch_rate) / 2.5) +
                             .15 * std::min(1.0, std::abs(back(pitch, 0)) / 70) * std::min(1.0, speed / 1.4);
        const double tip = .6 * act * std::sin(phase * .5 - .9);
        const double pelvic = .35 * act * std::sin(phase * .5 - 1.7);
        const double pelvic_tip = .5 * act * std::sin(phase * .5 - 2.5);
        for (int k = 0; k < 2; ++k) {
            const double s = k == 0 ? 1.0 : -1.0;   // the right side, then the left
            const int b = 13 + k * 4;
            add(b, 0, -s * sweep, s * (beat + roll_rate * 2));
            add(b + 1, 0, 0, s * tip);
            add(b + 2, 0, -s * sweep * .7, s * pelvic);
            add(b + 3, 0, 0, s * pelvic_tip);
        }
        // The fluke flicks on the tail's wave.
        add(21, 0, 0, -.12 * act * std::sin(phase - 5.2));
        add(22, 0, 0, .12 * act * std::sin(phase - 5.2));
        if (clip_w > .01) {   // a big clip is in charge: the pose gives way to it, all but a little
            const float keep = float(1 - .85 * clip_w);
            for (auto& r : p) for (auto& c : r) c *= keep;
        }
        if (wind > .002 || slam > .002)   // the strokes of the current: drawn forward and up, slammed back
            for (int k = 0; k < 2; ++k) {
                const double s = k == 0 ? 1.0 : -1.0;
                const int b = 13 + k * 4;
                add(b, 0, -s * (1.15 * slam - .6 * wind), s * (.9 * wind - .7 * slam));
                add(b + 1, 0, 0, s * (.8 * wind - 1.0 * slam));
                add(b + 2, 0, -s * (.8 * slam - .4 * wind), s * (.5 * wind - .4 * slam));
            }
        return p;
    }
};

// ---- the fight ------------------------------------------------------------------
enum Attack { NONE, BITE, ROAR, SALVO, BEAM, METEORS, TAIL, CURRENT };
struct AttackInfo { const char* clip; int ticks; const char* name; const char* message; };
const AttackInfo ATTACKS[8] = {
    {nullptr, 0, "none", ""},
    {"bite", 28, "bite", "DIVING BITE - dodge its jaws"},
    {"roar", 56, "roar", "VOID ROAR - get beyond 46 metres"},
    {"dorsal_salvo", 72, "salvo", "DORSAL SALVO - dodge the bolts and levitation clouds"},
    {"mouth_beam", 142, "beam", "MOUTH BEAM - leave the marked line or take cover"},
    {"roar", 66, "meteors", "METEOR RAIN - move after the cyan marker turns red"},
    {"tail_strike", 68, "tail", "TAIL STRIKE - clear its right flank"},
    {"repulsion_current", 96, "current", "REPULSION CURRENT - leave its forward cone"}};
enum class State { Dormant, Fighting, Dead, Defeated, Recruit };

struct Fighter {
    unsigned id = 0;
    float stun = 0, shred = 0, lift = 0, brand = -1, cleanse = 0, threat = 0;
    V previous, tick_prev, tick_now;
    unsigned brand_model = 0;
};
struct Bolt { unsigned mesh = 0, target = 0; V p, v; int life = 0, sphere = -1; };
struct Meteor { unsigned mesh = 0, target = 0; V p, impact; int delay = 0; float fall = 0; bool locked = false; };
struct Crystal { unsigned id = 0; V p; float respawn = 0; };
struct Oracle { unsigned id = 0; V p; bool broken = false; unsigned visual = 0; };
struct Shown { V p; float yaw = 0, pitch = 0, roll = 0; };

struct Boss {
    const SchizoScriptApi* a = nullptr;
    unsigned self = 0, player = 0;
    bool anim_api = false, log_on = false, review = false, pose_test = false;
    State state = State::Dormant;
    bool engaged = false, wiped = false, rewarded = false;
    std::string hud_path = "ui/leviathan_boss.ui";
    unsigned rng = 0x9e3779b9u;

    // the creature (the mod's fields; ticks, blocks per tick, degrees)
    V p, vel, home, spawn, last_position;
    bool has_last = false, jumped = false;
    int ticks = 0, phase = 1;
    Attack attack = NONE, pending = NONE;
    int attack_tick = 0, pending_ticks = 0, cooldown = 60, roars_owed = 0, since_hurt_clip = 100;
    float orbit = 0;
    int orbit_side = 1, attacks_since_bite = 0, stagger_ticks = 0;
    bool collapsing = false;
    V collapse_point;
    int collapse_stage = 0, collapse_tick = 0, collapse_total = 0;
    int meteor_rest = 300, tail_rest = 40, repulsion_rest = 80;
    bool diving = false, bitten = false, bite_hit = false, bite_open = false;
    float sight_shift = 0;
    int sight_checked = -100;
    std::vector<unsigned> struck, meteor_victims, list;
    int meteors_left = 0, meteor_next = 0;
    unsigned focus = 0, locked_id = 0;
    int focus_since = 0;
    bool blinking = false, blink_faces = false;
    V blink_to, blink_anchor;
    int blink_delay = 0, blink_cooldown = 200;
    float yaw = 0, pitch = 0, roll = 0, yaw_rate = 0, roll_rate = 3.5f, roll_goal = 0, pitch_goal = 0;
    bool has_roll_goal = false, has_pitch_goal = false, has_aim_at = false;
    float aim_yaw = 0, aim_pitch = 0, aim_rate = .05f, twist = 0, twist_goal = 0;
    V aim_at;
    int spin = 0, next_spin = 300, death_time = 0;
    int beam = 0;                        // 0, 1 charging, 2 firing (the mod's BEAM)
    bool beam_on = false, beam_done = false, sweeping = false, tail_windup = false;
    int stroke_windup_until = -1;
    std::vector<V> tail_before;
    Motion motion;
    Pose pose_now{}, pose_before{};
    Shown shown_now, shown_before;
    float acc = 0;
    // the clips and their cues
    std::string action_clip;
    float action_left = 0;
    bool action_hold = false;
    std::vector<std::string> heard, done;
    int cues_clip = 0, cues_fallback = 0;
    // what the engine says of the body as it is drawn
    V placed, mouth_now, head_dir_now{0, 0, 1};
    V parts_now[15];
    Frame shown_frame = frame(0, 0, 0);
    std::vector<Bolt> bolts;
    std::vector<Meteor> meteors;

    // the encounter (TestGame)
    std::vector<Fighter> fighters;
    std::vector<unsigned> proxies;   // by scene order, see PROXY_PART
    std::vector<Crystal> crystals;
    std::vector<Oracle> oracles;
    std::vector<int> order;
    std::vector<V> zones;
    std::vector<unsigned> zone_models;
    std::array<unsigned, 3> brand_models{};
    float clock = 0, hud = 0, fx_time = 0, fx_dt = 0, activation_range = 120, stat_scale = 1;
    float max_health = 60000, last_health = 60000, darkness = 0, whiteout = 0;
    float first_oracles = 120, first_brand = 60, oracle_due = -1, brand_due = -1, oracle_left = 0;
    float oracle_show = 0, oracle_step = 1.3f, recruit_time = 10, healing_tick = 0, supply_timer = 0, focus_timer = 0;
    int party = 1, oracle_round = 0, oracle_progress = 0, oracle_shown = -1;
    bool give_ammo = false, oracle_active = false, oracle_showing = false, brand_active = false;
    mutable bool vfx_failed = false;

    // ---- small things ---------------------------------------------------------
    float rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xffffff) / float(0x1000000); }
    int rint(int n) { return std::min(n - 1, int(rnd() * n)); }
    float gauss() { const float u = std::max(1e-6f, rnd()), v = rnd(); return std::sqrt(-2 * std::log(u)) * std::cos(2 * PI * v); }
    void say(const char* fmt, ...) const {
        char text[320];
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(text, sizeof text, fmt, args);
        va_end(args);
        a->log(a->ctx, text);
    }
    V pos(unsigned id) const { float v[3]{}; if (id) a->get_position(a->ctx, id, v); return {v[0], v[1], v[2]}; }
    void place(unsigned id, V q) const { if (id) { float v[3]{q.x, q.y, q.z}; a->set_position(a->ctx, id, v); } }
    void scale(unsigned id, V q) const { if (id) { float v[3]{q.x, q.y, q.z}; a->set_scale(a->ctx, id, v); } }
    void rotation(unsigned id, float x, float y, float z) const { if (id) { float v[3]{x, y, z}; a->set_rotation_euler(a->ctx, id, v); } }
    float attr(unsigned id, const char* n) const { return id ? a->get_attribute(a->ctx, id, n) : 0; }
    void attribute(unsigned id, const char* n, float v) const { if (id) a->set_attribute(a->ctx, id, n, v); }
    void impulse(unsigned id, V v) const { float q[3]{v.x, v.y, v.z}; a->add_impulse(a->ctx, id, q); }
    float maxhp(unsigned id) const { const float h = attr(id, "MaxHealth"); return h > 0 ? h : 100; }
    bool alive(unsigned id) const { return id && attr(id, "Health") > 0; }
    void damage(unsigned id, float n, const char* type = "void") const {
        if (!alive(id)) return;
        const float dealt = a->apply_damage(a->ctx, id, n * stat_scale * (phase == 3 ? 1.2f : phase == 2 ? 1.1f : 1.f), type);
        if (log_on) say("[leviathan] hurts %u: %.1f %s (dealt %.1f) during %s", id, n, type, dealt, ATTACKS[attack].name);
    }
    void message(const char* s, float t = 3) const { if (a->show_message) a->show_message(a->ctx, s, t); a->log(a->ctx, s); }
    void event(const char* s) const { if (a->emit_event) a->emit_event(a->ctx, s); }
    void ui(const char* id, const std::string& t) const { if (a->ui_set_text) a->ui_set_text(a->ctx, hud_path.c_str(), id, t.c_str()); }
    void fx(const char* n, V q, float s = 1, V d = {0, 1, 0}, float reach = 0) const {
        if (!a->spawn_effect) return;
        float v[3]{q.x, q.y, q.z}, dir[3]{d.x, d.y, d.z};
        if (!a->spawn_effect(a->ctx, n, v, dir, s, reach) && !vfx_failed) { vfx_failed = true; say("Leviathan ERROR: missing particle definition %s", n); }
    }
    void line(const char* n, V from, V to, float s = 1) const { const V d = to - from; fx(n, from, s, unit(d), length(d)); }
    unsigned model(const char* file, V q, float s = 1) const { float v[3]{q.x, q.y, q.z}; return a->spawn_mesh ? a->spawn_mesh(a->ctx, file, v, s) : 0; }
    void remove(unsigned& id) const { if (id) a->destroy_entity(a->ctx, id); id = 0; }
    Fighter* fighter(unsigned id) { for (auto& f : fighters) if (f.id == id) return &f; return nullptr; }
    float above_ground(unsigned id) const { const V q = pos(id); return q.y - ground_at(q); }
    static V center(V feet) { return feet + V{0, .9f, 0}; }     // a body's middle (bodyCenter)
    static V eye(V feet) { return feet + V{0, 1.6f, 0}; }

    // What the world is made of: rays pass through the Leviathan, its parts and
    // props, and the fighters (the mod's ClipContext sees only blocks).
    bool ignored(unsigned id) const {
        if (!id) return false;
        if (id == self) return true;
        for (unsigned q : proxies) if (q == id) return true;
        for (auto& c : crystals) if (c.id == id) return true;
        for (auto& o : oracles) if (o.id == id || o.visual == id) return true;
        for (auto& f : fighters) if (f.id == id) return true;
        return false;
    }
    bool solid_ray(V from, V dir, float dist, V* hit_out = nullptr) const {
        if (!a->raycast || dist <= 0) return false;
        V o = from;
        float left = dist;
        for (int i = 0; i < 6 && left > .01f; ++i) {
            float of[3]{o.x, o.y, o.z}, d[3]{dir.x, dir.y, dir.z}, h[3]{};
            unsigned id = 0;
            if (!a->raycast(a->ctx, of, d, left, h, &id)) return false;
            const V hp{h[0], h[1], h[2]};
            if (!ignored(id)) { if (hit_out) *hit_out = hp; return true; }
            left -= length(hp - o) + .05f;
            o = hp + dir * .05f;
        }
        return false;
    }
    bool clear(V from, V to) const { const V d = to - from; const float n = length(d); return n < .01f || !solid_ray(from, d * (1 / n), n); }
    float ground_at(V q) const {
        V hit;
        if (solid_ray(V{q.x, std::max(q.y, home.y) + 200, q.z}, V{0, -1, 0}, 600, &hit)) return hit.y;
        return home.y;
    }
    V ground_point(V q) const { q.y = ground_at(q) + .12f; return q; }

    // ---- the body as the engine draws it ----------------------------------------
    /// A point of the model on a bone, as posed and placed now.
    V bone_at(const char* bone, V model_point, V fallback) const {
        if (anim_api) {
            const float mp[3]{model_point.x, model_point.y, model_point.z};
            float out[3];
            if (a->bone_point(a->ctx, self, bone, mp, out)) return {out[0], out[1], out[2]};
        }
        return fallback;
    }
    V joint(const char* bone, V fallback) const {
        float v[3];
        return a->bone_position && a->bone_position(a->ctx, self, bone, v) ? V{v[0], v[1], v[2]} : fallback;
    }
    /// A point of the mod's model (render pixels) in the world, by the body's frame alone.
    V world(float x, float y, float z) const {
        const Frame f = frame(yaw, pitch, roll);
        return p + V{0, LIFT, 0} + f.right * (x / 16) + f.up * (y / 16) - f.forward * (z / 16);
    }
    V mouth() const { return mouth_now; }
    V head_direction() const { return beam == 3 ? V{0, 1, 0} : head_dir_now; }
    V part(int i) const { return parts_now[i]; }
    void read_body() {
        const Frame f = shown_frame;
        const V base = placed + f.up * LIFT;
        for (int i = 0; i < 15; ++i) {
            const PartSpec& s = PARTS[i];
            const V guess = base + f.right * (s.x / 16) + f.up * (s.y / 16) - f.forward * (s.z / 16);
            parts_now[i] = anim_api ? bone_at(s.bone, gltf(s.x, s.y, s.z), guess) : joint(s.bone, guess);
        }
        const V guess = base + f.forward * MOUTH.z;
        mouth_now = anim_api ? bone_at("head", MOUTH, guess) : joint("mouth", guess);
        if (anim_api) {
            const V ahead = bone_at("head", MOUTH + V{0, 0, 2}, mouth_now + f.forward * 2);
            head_dir_now = unit(ahead - mouth_now, f.forward);
        } else {
            head_dir_now = unit(mouth_now - joint("head", mouth_now - f.forward), f.forward);
        }
    }

    // ---- clips -------------------------------------------------------------------
    static float clip_seconds(const char* n) { for (auto& c : CLIPS) if (!std::strcmp(c.name, n)) return c.seconds; return 1; }
    static bool ours(const std::string& clip) { for (auto& c : CLIPS) if (clip == c.name) return true; return false; }
    void play(const char* n, bool loop, float speed, float blend, bool restart) {
        if (!a->play_animation || !a->play_animation(a->ctx, self, n, loop, speed, blend, restart)) say("Leviathan: no clip '%s'", n);
    }
    /// An "action" clip over the idle loop (the mod's action controller: 4 ticks in).
    void action(const char* n, float speed = 1, bool hold = false) {
        play(n, false, speed, .2f, true);
        action_clip = n;
        action_left = clip_seconds(n) / speed;
        action_hold = hold;
    }
    void clips(float dt) {
        if (action_left <= 0 || action_hold) return;
        action_left -= dt;
        if (action_left <= 0) { action_clip.clear(); play("idle", true, 1, .5f, false); }   // the movement controller: 10 ticks
    }
    /// A cue of the playing clip: true once, when the clip passes it (the event),
    /// or at its tick from the mod if the event does not come.
    bool cue(const char* name, int index, int due, int now) {
        const std::string key = index < 0 ? std::string(name) : std::string(name) + "|" + std::to_string(index);
        if (std::find(done.begin(), done.end(), key) != done.end()) return false;
        const auto it = std::find(heard.begin(), heard.end(), key);
        const bool by_clip = it != heard.end();
        if (!by_clip && now < due + (anim_api ? 4 : 0)) return false;
        if (by_clip) heard.erase(it);
        done.push_back(key);
        if (by_clip) ++cues_clip; else if (anim_api) ++cues_fallback;
        if (log_on) say("[leviathan] hook %s at tick %d (%s)", key.c_str(), now, by_clip ? "clip event" : "tick");
        return true;
    }
    void reset_cues() { heard.clear(); done.clear(); }
    /// The gameplay events since the last frame: the clips' cues, and who hit it.
    void listen() {
        if (!a->event_count || !a->event_get) return;
        const int n = a->event_count(a->ctx);
        for (int i = 0; i < n; ++i) {
            char name[64]{}, param[128]{};
            unsigned who = 0, target = 0;
            float value = 0;
            if (!a->event_get(a->ctx, i, name, sizeof name, &who, &target, &value, param, sizeof param)) continue;
            if (!std::strncmp(name, "anim.", 5)) {
                if (who && who != self) continue;
                const std::string text(param);
                const size_t bar = text.find('|');
                if (!ours(bar == std::string::npos ? text : text.substr(0, bar))) continue;
                std::string key = name + 5;
                if (bar != std::string::npos) key += "|" + text.substr(bar + 1);
                if (heard.size() > 64) heard.clear();
                heard.push_back(key);
            } else if (!std::strcmp(name, "combat.hit") || !std::strcmp(name, "ability.hit")) {
                bool on_me = target == self;
                for (unsigned q : proxies) on_me = on_me || q == target;
                if (on_me) if (Fighter* f = fighter(who)) f->threat += value;
            }
        }
    }

    // ---- who it fights -------------------------------------------------------------
    void refresh_fighters() {
        std::vector<unsigned> ids, present;
        if (player) ids.push_back(player);
        if (a->entity_count && a->entity_at && a->has_tag) {
            for (int i = 0, n = a->entity_count(a->ctx); i < n; ++i) {
                const unsigned id = a->entity_at(a->ctx, i);
                present.push_back(id);
                if (id != player && a->has_tag(a->ctx, id, "team.player") && hdist(pos(id), home) < 260) ids.push_back(id);
            }
            // Departed peers must not block the wipe's party-ready countdown.
            fighters.erase(std::remove_if(fighters.begin(), fighters.end(), [&](const Fighter& f) {
                return f.id != player && std::find(present.begin(), present.end(), f.id) == present.end();
            }), fighters.end());
        }
        for (unsigned id : ids)
            if (!fighter(id)) { Fighter f; f.id = id; f.previous = f.tick_prev = f.tick_now = pos(id); fighters.push_back(f); }
        const int members = std::max(1, int(fighters.size()));
        if (engaged && state == State::Fighting && members != party) {
            const float fraction = attr(self, "Health") / std::max(1.f, max_health);
            party = members;
            first_oracles = 120 - clampf(float(party - 1) / 7, 0, 1) * 60;
            stat_scale = 1 + .2f * (party - 1);
            max_health = 60000 * stat_scale;
            attribute(self, "MaxHealth", max_health);
            attribute(self, "Health", fraction * max_health);
            last_health = attr(self, "Health");
            event("leviathan.party.changed");
        }
    }
    int living() const { int n = 0; for (auto& f : fighters) if (alive(f.id) && hdist(pos(f.id), home) < 260) ++n; return n; }
    /// Everyone it can fight, nearest first (FOLLOW_RANGE 180).
    void make_list() {
        list.clear();
        if (!engaged || state != State::Fighting) return;
        for (auto& f : fighters) if (alive(f.id) && length(f.tick_now - p) <= 180) list.push_back(f.id);
        std::sort(list.begin(), list.end(), [&](unsigned x, unsigned y) { return length(pos(x) - p) < length(pos(y) - p); });
    }
    V motion_of(unsigned id) { Fighter* f = fighter(id); return f ? f->tick_now - f->tick_prev : V{}; }
    float focus_weight(unsigned id) {
        const Fighter* f = fighter(id);
        const float hurt_it = std::min(4.0f, (f ? f->threat : 0) / 250);
        const float airborne = above_ground(id) > BITE_ABOVE ? 1.6f : 1.0f;
        return (1 + hurt_it) * airborne / (1 + length(pos(id) - p) / 50);
    }
    unsigned find_target() {
        if (list.empty()) { focus = 0; return 0; }
        const bool present = focus && std::find(list.begin(), list.end(), focus) != list.end();
        const bool idle = attack == NONE && pending == NONE;
        if (!present || (idle && list.size() > 1 && ticks - focus_since > 260)) {
            float total = 0;
            for (unsigned id : list) total += focus_weight(id);
            float pick = rnd() * total;
            unsigned chosen = list[0];
            for (unsigned id : list) { pick -= focus_weight(id); if (pick <= 0) { chosen = id; break; } }
            focus = chosen;
            focus_since = ticks;
        }
        return focus;
    }
    unsigned port_target(int port, unsigned fallback) const { return list.size() <= 1 ? fallback : list[port % list.size()]; }
    unsigned locked_target(unsigned fallback) const { return alive(locked_id) ? locked_id : fallback; }
    float distance_to_tail(unsigned id) const {
        float best = 1e9f;
        for (int i = P_TAIL_3 + 1; i <= P_FLUKE; ++i) best = std::min(best, length(part(i) - pos(id)));
        return std::min(best, length(part(P_TAIL_3) - pos(id)));
    }
    bool tail_victim_near() const { for (unsigned id : list) if (distance_to_tail(id) <= 28) return true; return false; }
    unsigned nearest_to_tail() const {
        unsigned best = 0;
        float d = 1e9f;
        for (unsigned id : list) { const float t = distance_to_tail(id); if (t < d) { d = t; best = id; } }
        return best;
    }
    bool crowded() const {
        int near = 0;
        for (unsigned id : list) { const float d = length(pos(id) - p); if (d <= 18) return true; if (d <= 40) ++near; }
        return near >= 2;
    }

    // ---- statuses on the fighters (TestGame) ---------------------------------------
    void status(Fighter& f, float stun, float shred) {
        f.stun = std::max(f.stun, stun);
        f.shred = std::max(f.shred, shred);
        if (stun > 0) a->add_tag(a->ctx, f.id, "status.stunned");
        if (shred > 0) a->add_tag(a->ctx, f.id, "status.shredded");
    }
    void status_tick(float dt) {
        for (auto& f : fighters) {
            if (f.brand > 0) a->add_tag(a->ctx, f.id, "status.void_brand"); else a->remove_tag(a->ctx, f.id, "status.void_brand");
            if (f.stun > 0) { f.stun -= dt; if (f.stun <= 0) a->remove_tag(a->ctx, f.id, "status.stunned"); }
            if (f.shred > 0) { f.shred -= dt; if (f.shred <= 0) a->remove_tag(a->ctx, f.id, "status.shredded"); }
            if (f.lift > 0 && alive(f.id)) {   // levitation (Levitation II: up at 2 m/s)
                f.lift -= dt;
                const V v = (pos(f.id) - f.previous) * (1 / std::max(dt, .001f));
                float q[3]{clampf(v.x, -12, 12), 2.0f, clampf(v.z, -12, 12)};
                a->set_velocity(a->ctx, f.id, q);
            }
            f.previous = pos(f.id);
        }
    }
    void clear_status() {
        for (auto& f : fighters) {
            f.stun = f.shred = f.lift = 0;
            f.brand = -1;
            f.cleanse = 0;
            a->remove_tag(a->ctx, f.id, "status.void_brand");
            a->remove_tag(a->ctx, f.id, "status.stunned");
            a->remove_tag(a->ctx, f.id, "status.shredded");
        }
    }

    // ---- steering (the mod's fly / steer / avoidTerrain) ------------------------------
    V surface_normal(V origin, V f, float look, V hit) const {
        const V side = unit(cross(f, V{0, 1, 0}), V{1, 0, 0}), upv = cross(side, f);
        V h1, h2;
        if (solid_ray(origin + side * .7f, f, look + 3, &h1) && solid_ray(origin + upv * .7f, f, look + 3, &h2)) {
            V n = unit(cross(h1 - hit, h2 - hit), f * -1.0f);
            if (dot(n, f) > 0) n = n * -1.0f;
            return n;
        }
        return f * -1.0f;
    }
    /// Feelers ahead: where one meets something solid, the wanted way is pushed off
    /// its face and upward, the harder the nearer it is.
    V avoid_terrain(V desired, float look) const {
        const V origin = p + V{0, LIFT, 0};
        const Frame body = frame(yaw, pitch, 0);
        const V feelers[4] = {body.forward, desired, unit(body.forward + body.right * .35f), unit(body.forward - body.right * .35f)};
        V result = desired;
        bool changed = false;
        for (const V& f : feelers) {
            V hit;
            if (!solid_ray(origin, f, look, &hit)) continue;
            const float urgency = 1 - length(hit - origin) / look;
            result = result + surface_normal(origin, f, look, hit) * (urgency * 1.6f) + V{0, urgency * .9f, 0};
            changed = true;
        }
        return changed ? unit(result) : desired;
    }
    void steer(V want, float max_speed, float turn, float pitch_step, float response, bool arrive) {
        const V to = want - p;
        const float distance = length(to);
        const V fwd = frame(yaw, pitch, 0).forward;
        V desired = distance < 1e-3f ? fwd : to * (1 / distance);
        desired = avoid_terrain(desired, arrive ? 34.0f : 18.0f);
        const float before = yaw;
        if (desired.x * desired.x + desired.z * desired.z > .01f) yaw = wrap(approach(yaw, std::atan2(-desired.x, desired.z) / DEG, turn));
        // The further the nose is from where it should point, the harder it pitches (up to twice).
        const float goal = clampf(has_pitch_goal ? pitch_goal : -std::atan2(desired.y, hlen(desired)) / DEG, -88, 88);
        const float step = std::max(pitch_step, std::min(pitch_step * 2, std::abs(goal - pitch) * .14f));
        pitch = approach(pitch, goal, step);
        // The further round it must turn, the slower it goes (never below 40%); the
        // further it has to go, the faster (up to 80% more).
        const float ahead = dot(fwd, desired);
        const float turn_factor = clampf(.4f + .6f * (ahead + .5f) / 1.3f, .4f, 1);
        const float hurry = 1 + clampf((distance - 60) / 120, 0, .8f);
        const float speed = (arrive ? std::min(max_speed * hurry, distance * .12f) : max_speed) * turn_factor;
        const V heading = frame(yaw, pitch, 0).forward;
        vel = vel + (heading * speed - vel) * response;
        yaw_rate += (wrap(yaw - before) - yaw_rate) * .2f;
    }
    void fly(V want, float max_speed, float turn) { steer(want, max_speed, turn * 1.35f, 4.2f, .11f, true); }
    void face_toward(V point, float yaw_step, float pitch_step) {
        const V to = point - (p + V{0, LIFT, 0});
        const float before = yaw;
        yaw = wrap(approach(yaw, std::atan2(-to.x, to.z) / DEG, yaw_step));
        pitch = approach(pitch, clampf(-std::atan2(to.y, hlen(to)) / DEG, -83, 83), pitch_step);
        yaw_rate += (wrap(yaw - before) - yaw_rate) * .2f;
    }
    void set_roll_goal(float g) { roll_goal = g; has_roll_goal = true; }
    void set_aim(V at, float rate) { aim_at = at; has_aim_at = true; aim_rate = rate; }
    /// The roll that points the back at a direction, or (either) the back or the
    /// belly, whichever is cheaper.
    float roll_toward(V direction, bool either) const {
        const Frame level = frame(yaw, pitch, 0);
        const V left = cross(level.up, level.forward), d = unit(direction);
        const float back = std::atan2(dot(d, left), dot(d, level.up)) / DEG;
        if (!either) return back;
        const float belly = back > 0 ? back - 180 : back + 180;
        return roll_cost(back) <= roll_cost(belly) ? back : belly;
    }
    float roll_cost(float goal) const { return std::abs(wrap(goal - roll)) + 35 * std::abs(wrap(goal)) / 180; }
    static float prefer_upright(float goal) {
        const float w = wrap(goal), size = std::abs(w);
        return size <= 100 ? w : (w > 0 ? 1.f : -1.f) * (100 + (size - 100) * .5f);
    }
    V around(V anchor, float radius, float height) {
        const V now = p - anchor;
        const float angle = std::atan2(now.z, now.x) + orbit_side * (1.2f + rnd() * 1.6f);
        return anchor + V{std::cos(angle) * radius, height, std::sin(angle) * radius};
    }
    V dive_start(unsigned target) const {
        const V t = pos(target), away = flat(p - t);
        const V side = dot(away, away) < 1 ? V{1, 0, 0} : unit(away);
        return V{t.x + side.x * 46, t.y + 34, t.z + side.z * 46};
    }
    float dive_speed(unsigned target) { return clampf(length(motion_of(target)) * 1.5f + .9f, 2.5f, 4.2f); }
    V tail_pass_point(unsigned victim, float radius) const {
        const V t = pos(victim), around_v = flat(p - t);
        const float angle = std::atan2(around_v.z, around_v.x) + .6f;
        return t + V{std::cos(angle) * radius, 3, std::sin(angle) * radius};
    }
    V attack_spot(Attack which, unsigned target) {
        const V anchor = pos(target);
        switch (which) {
            case BITE: return dive_start(target);
            case TAIL: return around(anchor, 30, 4);
            case CURRENT: return around(anchor, 34, 10);
            case SALVO: return around(anchor, 44, 24);
            case BEAM: return around(anchor, 58, 20);
            default: return around(anchor, 40, 18);
        }
    }
    /// A circle round the target, ahead of where it is, from where it can see it.
    void orbit_target(unsigned target, float radius, float height, float speed) {
        const V t = pos(target), around_v = p - t;
        float angle = std::atan2(around_v.z, around_v.x) + orbit_side * .55f;
        if (ticks - sight_checked >= 10) {
            sight_checked = ticks;
            sight_shift = 0;
            const V eye_point = center(t);
            if (!clear(t + V{std::cos(angle) * radius, height, std::sin(angle) * radius}, eye_point))
                for (int step = 1; step <= 10; ++step) {
                    const float shift = (step % 2 == 1 ? 1 : -1) * ((step + 1) / 2) * .45f;
                    if (clear(t + V{std::cos(angle + shift) * radius, height, std::sin(angle + shift) * radius}, eye_point)) { sight_shift = shift; break; }
                }
        }
        angle += sight_shift;
        fly(t + V{std::cos(angle) * radius, height, std::sin(angle) * radius}, speed, 3);
    }
    /// The bite's dive, the way a phantom swoops: the MOUTH is steered onto where
    /// the prey will be when it gets there.
    void dive_at(unsigned target) {
        const V m = mouth();
        const float speed = dive_speed(target);
        const V velocity = motion_of(target), body = center(pos(target));
        V aim = body;
        for (int pass = 0; pass < 3; ++pass) aim = body + velocity * std::min(25.0f, length(m - aim) / speed);
        steer(aim - (m - p), speed, 9, 8, .3f, false);
        set_aim(eye(pos(target)), .12f);
        set_roll_goal(roll_toward(eye(pos(target)) - p, true));
    }
    void climb_away() {
        const V ahead = frame(yaw, 0, 0).forward;
        steer(p + V{ahead.x * 70, 60, ahead.z * 70}, 2.5f * .9f, 7, 7, .2f, false);
        set_roll_goal(0);
    }
    void patrol(float rate) {
        orbit += .006f * rate;
        const float weave = std::sin(ticks * .017f) * 14 + std::sin(ticks * .0071f + .7f) * 24;
        const float radius = 108 + std::sin(ticks * .011f + 1) * 14;
        fly(home + V{std::cos(orbit) * radius, 58 + weave, std::sin(orbit) * radius}, .85f * rate, 1.8f);
        if (spin == 0 && --next_spin <= 0) { spin = 40; next_spin = 400 + rint(500); event("leviathan.barrel_roll"); }
        look_about();
    }
    void look_about() {
        has_aim_at = false;
        aim_yaw += (std::sin(ticks * .019f) * .5f - aim_yaw) * .03f;
        aim_pitch += (std::sin(ticks * .013f + 2) * .25f - aim_pitch) * .03f;
    }
    /// Before the fight it swims a slow, small circle where it was placed.
    void drift() {
        orbit += .004f;
        fly(spawn + V{std::cos(orbit) * 26, std::sin(ticks * .013f) * 3, std::sin(orbit) * 26}, .35f, 1.8f);
        look_about();
    }

    // ---- teleporting (thinkBlink / blink) ---------------------------------------------
    void body_mist(V at, bool burst) {
        const Frame f = frame(yaw, pitch, roll);
        for (int i = 0; i < 14; i += 3) {
            const V q = at + V{0, LIFT, 0} - f.forward * ((-300 + i * 85) / 16.0f);
            fx(burst ? "leviathan_teleport" : "leviathan_portal_mist", q, burst ? 2.0f : 1.0f);
        }
        if (burst) fx("leviathan_teleport", at + V{0, LIFT, 0}, 3);
    }
    void start_blink(V to, V anchor, bool faces) {
        to.y = std::max(to.y, ground_at(to) + 6);
        blinking = true;
        blink_to = to;
        blink_anchor = anchor;
        blink_faces = faces;
        blink_delay = 18;
        fx("leviathan_teleport_charge", p + V{0, LIFT, 0}, 3);
        fx("leviathan_teleport_charge", to + V{0, LIFT, 0}, 3);
        event("leviathan.teleport.warning");
    }
    void think_blink(unsigned target) {
        if (blinking) {
            if (--blink_delay % 2 == 0) { body_mist(p, false); body_mist(blink_to, false); }
            if (blink_delay <= 0) blink();
            return;
        }
        if (blink_cooldown > 0) { --blink_cooldown; return; }
        if (attack != NONE || stagger_ticks > 0) return;
        const V home_point = home + V{0, 58, 0};
        if (length(p - home_point) > 230) {
            start_blink(home_point + V{std::cos(orbit) * 108, 0, std::sin(orbit) * 108}, home, false);
            return;
        }
        if (!target) return;
        const V anchor = pos(target);
        bool go = false;
        V dest;
        if (length(anchor - p) > 150) { dest = around(anchor, 64, 22); go = true; }
        else if (pending != NONE) {
            const V spot = attack_spot(pending, target), to_spot = spot - p;
            const bool behind = pending_ticks > 8 && length(to_spot) > 45 && dot(unit(to_spot), frame(yaw, pitch, 0).forward) < -.3f;
            if (behind || pending_ticks > 90) { dest = spot; go = true; }
        } else if (phase >= 2 && rint(450) == 0) { dest = around(anchor, 50, 26); go = true; }
        if (go) start_blink(dest, anchor, pending == BITE);
    }
    void blink() {
        const V from = p, to = blink_to, anchor = blink_anchor;
        blinking = false;
        blink_cooldown = phase >= 3 ? 200 : 300;
        if (attack != NONE) return;
        // It arrives pointing where it is going: along the circle round the one it
        // fights, or at it for a bite.
        const V around_v = to - anchor;
        const float angle = std::atan2(around_v.z, around_v.x);
        V heading = blink_faces ? flat(anchor - to) : V{-std::sin(angle) * orbit_side, 0, std::cos(angle) * orbit_side};
        heading = unit(heading);
        body_mist(from, true);
        p = to;
        yaw = std::atan2(-heading.x, heading.z) / DEG;
        pitch = 0;
        vel = heading * .8f;
        yaw_rate = 0;
        last_position = to;
        pending_ticks = std::min(pending_ticks, 30);
        motion.settle(yaw, pitch, roll);
        jumped = true;
        body_mist(to, true);
        event("leviathan.teleport");
    }

    // ---- orientation (finishOrientation) -----------------------------------------------
    void finish_orientation() {
        if (spin > 0) { --spin; roll = wrap(roll + 9); }   // a barrel roll
        else {
            const float goal = has_roll_goal ? prefer_upright(roll_goal) : -clampf(yaw_rate * 14, -50, 50);
            roll = wrap(approach(roll, goal, roll_rate));
        }
        if (has_aim_at) {
            // The head is steered by where it points now (from the mouth, as drawn)
            // against where the target lies from the mouth.
            const Frame f = frame(yaw, pitch, roll);
            const V want = aim_at - mouth();
            if (dot(want, want) > 1e-4f) {
                const V w = unit(want), now = local_head();
                const float want_yaw = std::atan2(dot(w, f.right), dot(w, f.forward));
                const float want_up = std::atan2(dot(w, f.up), std::hypot(dot(w, f.right), dot(w, f.forward)));
                const float now_yaw = std::atan2(now.x, now.z), now_up = std::atan2(now.y, std::hypot(now.x, now.z));
                aim_yaw = clampf(aim_yaw + clampf(wrap((want_yaw - now_yaw) / DEG) * DEG, -aim_rate, aim_rate), -1.25f, 1.25f);
                aim_pitch = clampf(aim_pitch + clampf(wrap((want_up - now_up) / DEG) * DEG, -aim_rate, aim_rate), -1.0f, 1.2f);
            }
        }
        twist += (twist_goal - twist) * .08f;
    }
    /// Where the head points as drawn, in the drawn body's own terms (x right, y up, z forward).
    V local_head() const {
        const V d = head_dir_now;
        return {dot(d, shown_frame.right), dot(d, shown_frame.up), dot(d, shown_frame.forward)};
    }
    void spin_down() {
        roll = wrap(roll + 2.5f);
        aim_pitch += (-.6f - aim_pitch) * .04f;
        aim_yaw *= .97f;
        twist += (0 - twist) * .1f;
    }

    // ---- choosing and starting attacks ---------------------------------------------------
    void update_phase() {
        const float h = attr(self, "Health");
        const int next = h <= max_health / 3 ? 3 : h <= max_health * 2 / 3 ? 2 : 1;
        if (next > phase) {
            phase = next;
            ++roars_owed;
            a->set_flag(a->ctx, "leviathan.phase", phase);
            if (oracle_due < 0) { oracle_due = first_oracles; brand_due = first_brand; }
            event("leviathan.phase");
        }
    }
    void choose() {
        if (roars_owed > 0) { --roars_owed; pending = ROAR; pending_ticks = 0; return; }
        const float chance = rnd();
        unsigned high = 0;
        float nearest = 1e9f;
        for (unsigned id : list)
            if (above_ground(id) > BITE_ABOVE) { const float d = length(pos(id) - mouth()); if (d < nearest) { nearest = d; high = id; } }
        if (tail_rest <= 0 && rnd() < (tail_victim_near() ? .9f : .4f)) pending = TAIL;
        else if (repulsion_rest <= 0 && rnd() < (crowded() ? .8f : .35f)) pending = CURRENT;
        else if (high && (attacks_since_bite >= 2 || (nearest < 26 && chance < .6f))) {
            pending = BITE;
            diving = false;
            focus = high;
            focus_since = ticks;
        } else if (phase >= 2 && meteor_rest <= 0 && rnd() < .6f) pending = METEORS;
        else pending = chance < (phase >= 2 ? .25f : .3f) ? SALVO : BEAM;   // the salvo is the fallback
        orbit_side = rnd() < .5f ? 1 : -1;
        pending_ticks = 0;
    }
    void approach_attack(unsigned target, float rate) {
        ++pending_ticks;
        const V t = pos(target);
        switch (pending) {
            case BITE: {
                if (above_ground(target) <= BITE_ABOVE) { pending = NONE; cooldown = 10; return; }   // out of reach of a bite
                if (!diving) {
                    // It climbs to a point high above and out from the target...
                    const V start = dive_start(target);
                    fly(start, 1.7f * rate, 4);
                    set_aim(eye(t), .09f);
                    set_roll_goal(roll_toward(eye(t) - p, true));
                    const float f = hlen(t - p), need = clampf(start.y - t.y - 8, 4, 16);
                    if ((p.y + LIFT > t.y + need && f > 20 && f < 95) || pending_ticks > 90) diving = true;
                } else {
                    // ...and comes down on it; the jaws open on the way.
                    dive_at(target);
                    if (length(mouth() - center(t)) < dive_speed(target) * 11) begin(pending);
                }
                if (pending_ticks > 260 && pending == BITE) { diving = true; begin(pending); }
                break;
            }
            case TAIL: {
                // A flyby: it curves round its victim with them on its right; the tail
                // comes round as they pass abeam.
                const unsigned victim = tail_victim_near() ? nearest_to_tail() : target;
                fly(tail_pass_point(victim, 24), 1.6f * rate, 4);
                const V to = flat(pos(victim) - p);
                const float reach = length(to);
                const V ahead = frame(yaw, 0, 0).forward, right{-ahead.z, 0, ahead.x};
                const float along = reach < 1 ? 0 : dot(ahead, unit(to)), side = reach < 1 ? 0 : dot(right, unit(to));
                const bool abeam = side > .5f && along > -.26f && along < .58f && reach > 16 && reach < 32 &&
                                   std::abs(p.y + LIFT - pos(victim).y) < 22;
                if (abeam || pending_ticks > 140) begin(pending);
                break;
            }
            case CURRENT: {
                const V away = flat(p - t);
                const V side = dot(away, away) < 1 ? V{1, 0, 0} : unit(away);
                fly(t + side * 34 + V{0, 10, 0}, 1.5f * rate, 4);
                set_aim(center(t), .05f);
                if (length(t - p) < 46 || pending_ticks > 100) begin(pending);
                break;
            }
            case SALVO: {
                // Circling the target on its side, its back toward it.
                orbit_target(target, 44, 24, 1.15f * rate);
                set_roll_goal(roll_toward(eye(t) - p, false));
                const bool seen = clear(p + V{0, LIFT, 0}, center(t));
                if ((seen && (pending_ticks > 55 || (pending_ticks > 25 && std::abs(wrap(roll - roll_goal)) < 25))) || pending_ticks > 170) begin(pending);
                break;
            }
            case BEAM: {
                orbit_target(target, 58, 20, .7f * rate);
                set_aim(center(t), .06f);
                set_roll_goal(roll_toward(center(t) - p, true));
                const bool seen = clear(mouth(), center(t));   // terrain is cover: it does not start blind
                if ((seen && (pending_ticks > 45 || (pending_ticks > 25 && std::abs(aim_yaw) < .2f && length(t - p) < 95))) || pending_ticks > 170) {
                    locked_id = target;
                    begin(pending);
                }
                break;
            }
            default: begin(pending);
        }
    }
    void begin(Attack next) {
        attack = next;
        pending = NONE;
        attack_tick = 0;
        struck.clear();
        reset_cues();
        bitten = bite_hit = bite_open = false;
        beam = 0;
        beam_on = beam_done = sweeping = tail_windup = false;
        stroke_windup_until = -1;
        tail_before.clear();
        if (next == TAIL) tail_rest = 110;
        if (next == CURRENT) repulsion_rest = phase >= 3 ? 200 : 280;
        attacks_since_bite = next == BITE ? 0 : attacks_since_bite + 1;
        action(ATTACKS[next].clip, next == BEAM ? .62f : 1.0f);
        message(ATTACKS[next].message);
        event((std::string("leviathan.") + ATTACKS[next].name).c_str());
        if (next == METEORS) {
            // Up to six fighters, each with a meteor on them wherever they are.
            std::vector<unsigned> pool = list;
            for (int i = int(pool.size()) - 1; i > 0; --i) std::swap(pool[i], pool[rint(i + 1)]);
            if (pool.size() > 6) pool.resize(6);
            meteor_victims = pool;
            const float count = (8 + 3 * std::max(0, int(pool.size()) - 1)) * (phase >= 3 ? 1.4f : 1.0f);
            meteors_left = std::min(40, int(std::ceil(count)));
            meteor_next = 0;
            meteor_rest = phase >= 3 ? 400 : 600;
        }
        if (log_on) say("[leviathan] begin %s at tick %d (phase %d)", ATTACKS[next].name, ticks, phase);
    }
    void end_attack() {
        const bool follow_up = attack == TAIL || attack == CURRENT || attack == BITE;
        if (attack == BITE && !bite_hit) attacks_since_bite = 2;   // it missed: it goes at the same prey again
        attack = NONE;
        beam = 0;
        beam_on = sweeping = false;
        locked_id = 0;
        cooldown = std::max(12, 46 - phase * 9) / (follow_up ? 2 : 1);
        if (rint(3) == 0) spin = 28;
    }

    // ---- the attacks -------------------------------------------------------------------
    void tick_attack(unsigned target, float rate) {
        ++attack_tick;
        switch (attack) {
            case BITE: {
                if (!bitten) {
                    if (target) dive_at(target);
                    if (cue("bite_open", -1, 9, attack_tick)) bite_open = true;
                    const bool closing = cue("bite_close", -1, 16, attack_tick);
                    if ((bite_open && bite_within_reach()) || closing) {
                        bitten = true;
                        bite_hit = hurt_around(mouth(), 8, 24 * HURT) > 0;
                        if (log_on) say("[leviathan] bite at tick %d: %s", attack_tick, bite_hit ? "hit" : "missed");
                        fx("leviathan_bite", mouth(), 2);
                        event("leviathan.bite.hit");
                    }
                } else climb_away();
                break;
            }
            case ROAR: {
                // It rears up, throws its head back and twists as it roars.
                vel = vel * .92f;
                pitch_goal = -38;
                has_pitch_goal = true;
                pitch = approach(pitch, -38, 1.4f);
                has_aim_at = false;
                aim_pitch = clampf(aim_pitch + (.5f - aim_pitch) * .05f, -1, 1);
                twist_goal = .6f * std::sin(attack_tick * .16f);
                set_roll_goal(wrap(roll + 6));
                if (cue("roar_blast", -1, 20, attack_tick)) roar();
                break;
            }
            case SALVO: {
                if (target) {
                    orbit_target(target, 44, 24, 1.15f * rate);
                    set_roll_goal(roll_toward(eye(pos(target)) - p, false));
                    twist_goal = .5f * std::sin(clampf((attack_tick - 10) / 40.0f, 0, 1) * PI);   // a corkscrew as the plates open
                }
                for (int i = 0; i < 9; ++i) if (cue("bolt", i, PORT_TICKS[i], attack_tick)) fire_bolt(i, port_target(i, target));
                break;
            }
            case BEAM: {
                // It hangs where it is, turning only to keep its prey in front of it.
                vel = vel * .86f;
                const unsigned locked = locked_target(target);
                if (cue("beam_fire", -1, 30, attack_tick)) { beam_on = true; event("leviathan.beam.fire"); }
                if (beam_on && cue("beam_end", -1, 100, attack_tick)) { beam_on = false; beam_done = true; }
                const bool winding = !beam_on && !beam_done;
                if (locked) {
                    const V body = center(pos(locked));
                    face_toward(body, 2.4f, 2.2f);
                    set_roll_goal(roll_toward(body - p, true));
                    // the head locks hard on for the wind-up, then holds on a little behind
                    set_aim(winding ? body : body + motion_of(locked) * 5, winding ? .09f : .05f);
                    if (winding && attack_tick % 3 == 0) fx("leviathan_beam_warning", ground_point(pos(locked)));
                }
                beam = winding ? 1 : beam_on ? 2 : 0;
                if (beam_on) fire_beam(); else if (winding) beam_charge_fx();
                break;
            }
            case METEORS: {
                // It rears up over the island and calls the meteors down.
                vel = vel * .9f;
                pitch_goal = -30;
                has_pitch_goal = true;
                pitch = approach(pitch, -30, 1.2f);
                has_aim_at = false;
                aim_pitch = clampf(aim_pitch + (.45f - aim_pitch) * .05f, -1, 1);
                twist_goal = .5f * std::sin(attack_tick * .14f);
                set_roll_goal(0);
                if (attack_tick >= 16 && attack_tick <= 46 && attack_tick % 2 == 0) summon_meteors();
                break;
            }
            case TAIL: {
                // It does not stop: it curves on round its victim, leans into the turn,
                // rolls back as the tail whips across, and flies off wider after.
                unsigned victim = nearest_to_tail();
                if (!victim) victim = target;
                const bool after = sweeping ? false : done_cue("tail_strike_end");
                if (victim) fly(tail_pass_point(victim, after ? 44.0f : 24.0f), (after ? 1.5f : 1.25f) * rate, 3.4f);
                set_roll_goal(attack_tick < 16 ? -26.0f : attack_tick < 30 ? 16.0f : -6.0f);
                roll_rate = 7;
                const float shoulders = clampf((attack_tick - 14) / 8.0f, 0, 1) * clampf((40 - attack_tick) / 8.0f, 0, 1);
                twist_goal = -.55f * shoulders + .2f * std::sin(attack_tick * .2f);
                if (cue("tail_windup", -1, 4, attack_tick)) tail_windup = true;
                if (cue("tail_strike_start", -1, 22, attack_tick)) { sweeping = true; tail_windup = false; event("leviathan.tail.strike"); }
                if (sweeping && cue("tail_strike_end", -1, 34, attack_tick)) sweeping = false;
                if (tail_windup && attack_tick % 2 == 0) fx("leviathan_teleport_charge", part(P_FLUKE), 1.5f);
                if (sweeping) tail_sweep();
                break;
            }
            case CURRENT: {
                // It faces the one it pushes off and hangs while the fins throw the current, three times.
                vel = vel * .94f;
                if (target && attack_tick < STROKES[0]) face_toward(center(pos(target)), 6, 3);
                set_roll_goal(0);
                for (int k = 0; k < 3; ++k) {
                    if (cue("stroke_windup", k, STROKES[k] - 8, attack_tick)) stroke_windup_until = attack_tick + 8;
                    if (cue("stroke", k, STROKES[k], attack_tick)) {
                        stroke_windup_until = -1;
                        repulse();
                        vel = vel + frame(yaw, pitch, 0).forward * -.55f;   // the body kicks back
                    }
                }
                if (attack_tick < stroke_windup_until && attack_tick % 2 == 0) {
                    fx("leviathan_port_flash", part(P_PECTORAL_L), 1.2f);
                    fx("leviathan_port_flash", part(P_PECTORAL_R), 1.2f);
                }
                break;
            }
            default: break;
        }
        if (attack_tick >= ATTACKS[attack].ticks) end_attack();
    }
    bool done_cue(const char* key) const { return std::find(done.begin(), done.end(), std::string(key)) != done.end(); }
    bool bite_within_reach() const {
        for (unsigned id : list) if (above_ground(id) > BITE_ABOVE && length(center(pos(id)) - mouth()) < 8) return true;
        return false;
    }
    int hurt_around(V at, float radius, float amount) {
        int hurt = 0;
        for (unsigned id : list) {
            if (above_ground(id) <= BITE_ABOVE || length(center(pos(id)) - at) > radius) continue;
            damage(id, amount, "physical");
            impulse(id, unit(flat(pos(id) - at), V{1, 0, 0}) * 10 + V{0, 5, 0});
            ++hurt;
        }
        return hurt;
    }
    void roar() {
        fx("leviathan_roar", p + V{0, 6, 0}, 15.33f);
        for (auto& f : fighters) {
            if (!alive(f.id) || length(pos(f.id) - p) > 46) continue;
            const V away = flat(pos(f.id) - p);
            impulse(f.id, unit(away, V{1, 0, 0}) * 7 + V{0, 5, 0});
            status(f, 1, std::min(30.0f, 20 * stat_scale));   // stunned 1 s, armour shredded 20 s (SHRED_TICKS)
        }
        event("leviathan.roar.pulse");
    }
    void fire_bolt(int port, unsigned target) {
        const Frame f = frame(yaw, pitch, roll);
        const V from = joint(PORTS[port], p + V{0, LIFT + 3, 0});
        V direction = f.up;
        if (target) direction = f.up * .75f + unit(center(pos(target)) - from) * .25f;
        direction = unit(unit(direction) + V{gauss() * .25f, gauss() * .15f, gauss() * .25f});
        Bolt b;
        b.p = from;
        b.v = direction * .3f;
        b.target = target;
        b.mesh = model("assets/models/ender_leviathan/leviathan_bolt.glb", from);
        bolts.push_back(b);
        fx("leviathan_port_flash", from);
        event("leviathan.bolt.launched");
    }
    void beam_charge_fx() {
        const V from = mouth();
        V end = from + head_direction() * 110, hit;
        if (solid_ray(from, head_direction(), 110, &hit)) end = hit;
        fx("leviathan_beam_charge", from, .35f + attack_tick * TICK * 1.3f);
        line("leviathan_beam_guide", from, end);
    }
    /// The beam leaves the mouth the way the head points (it follows the head round)
    /// and stops at the first solid thing: terrain is cover.
    void fire_beam() {
        const V from = mouth(), d = head_direction();
        V to = from + d * 110, hit;
        const bool landed = solid_ray(from, d, 110, &hit);
        if (landed) to = hit;
        line("leviathan_beam", from, to, 2);
        fx("leviathan_beam_charge", from, 2);
        if (landed) fx("leviathan_beam_impact", to, 2);
        if (attack_tick % 5 == 0)
            for (unsigned id : list)
                if (seg_dist(center(pos(id)), from, to) < 3.2f + .3f) damage(id, maxhp(id) * .05f + 2);
    }
    void summon_meteors() {
        std::vector<unsigned> victims;
        for (unsigned id : list) if (std::find(meteor_victims.begin(), meteor_victims.end(), id) != meteor_victims.end()) victims.push_back(id);
        if (victims.empty() || meteors_left <= 0) return;
        const int batches = (46 - attack_tick) / 2 + 1;
        const int now = int(std::ceil(meteors_left / double(batches)));
        for (int i = 0; i < now; ++i) {
            const unsigned victim = victims[meteor_next++ % victims.size()];
            const bool aimed = meteor_next <= int(victims.size());   // everybody's first one follows them
            V spot = pos(victim);
            if (!aimed) { spot = spot + V{gauss() * 6, 0, gauss() * 6}; spot.y = ground_at(spot); }
            Meteor m;
            m.impact = spot;
            m.p = spot + V{0, 70, 0};
            m.delay = 4 + rint(12);
            m.target = aimed ? victim : 0;
            m.locked = !aimed;
            m.mesh = model("assets/models/ender_leviathan/leviathan_meteor.glb", m.p);
            meteors.push_back(m);
        }
        meteors_left -= now;
        if (log_on) say("[leviathan] meteors: %d called at tick %d, %d left", now, attack_tick, meteors_left);
        event("leviathan.meteor.launched");
    }
    /// The whip: each piece of the tail, where the clip has it, hurts and throws what
    /// it meets, once each.
    void tail_sweep() {
        const Frame f = frame(yaw, pitch, roll);
        const V origin = p + V{0, LIFT, 0};
        std::vector<V> now(4);
        for (int k = 0; k < 4; ++k) {
            if (anim_api) now[k] = part(P_TAIL_3 + k);
            else {   // the mod's own table of the clip
                const float time = attack_tick * TICK;
                int high = 0;
                while (high < 4 && TAIL_TIMES[high] < time) ++high;
                const int low = std::max(0, high - 1);
                const float span = TAIL_TIMES[high] - TAIL_TIMES[low];
                const float t = span < 1e-6f ? 0 : clampf((time - TAIL_TIMES[low]) / span, 0, 1);
                const float* o0 = TAIL_OFFSETS[low][k];
                const float* o1 = TAIL_OFFSETS[high][k];
                now[k] = origin + f.right * (o0[0] + (o1[0] - o0[0]) * t) + f.up * (o0[1] + (o1[1] - o0[1]) * t) -
                         f.forward * (o0[2] + (o1[2] - o0[2]) * t);
            }
        }
        if (tail_before.size() != 4) tail_before = now;
        for (int k = 0; k < 4; ++k) {
            line("leviathan_tail_arc", tail_before[k], now[k], 1.5f);
            for (unsigned id : list) {
                if (std::find(struck.begin(), struck.end(), id) != struck.end()) continue;
                if (seg_dist(center(pos(id)), tail_before[k], now[k]) > TAIL_RADII[k] + .3f) continue;
                struck.push_back(id);
                damage(id, 16 * HURT, "physical");
                impulse(id, unit(flat(pos(id) - origin), f.right) * 8 + V{0, 5, 0});
                event("leviathan.tail.hit");
            }
        }
        if (log_on)
            for (unsigned id : list) {
                float best = 1e9f;
                for (int k = 0; k < 4; ++k) best = std::min(best, seg_dist(center(pos(id)), tail_before[k], now[k]) - TAIL_RADII[k]);
                say("[leviathan] tail t%d: fluke %.1f %.1f %.1f, nearest fighter %.1f m outside its reach", attack_tick, now[3].x, now[3].y, now[3].z, best);
            }
        tail_before = now;
    }
    /// One stroke of the current: a cone 60 m long and 42 degrees to each side.
    void repulse() {
        const V d = frame(yaw, pitch, roll).forward, origin = p + V{0, LIFT, 0} + d * 5;
        fx("leviathan_current", origin, 1, d, 60);
        for (unsigned id : list) {
            const V to = center(pos(id)) - origin;
            const float distance = length(to);
            if (distance >= 60 || distance < 1e-3f || dot(unit(to), d) < std::cos(42 * DEG)) continue;
            damage(id, 6 * HURT);
            impulse(id, unit(flat(d), unit(flat(to))) * (14 - 7 * distance / 60) + V{0, 3, 0});
        }
        event("leviathan.current.stroke");
    }
    /// Anything that swims into the body is thrown out of it (every half second).
    void contact_damage() {
        if (ticks % 10 != 0) return;
        for (unsigned id : list) {
            const V q = pos(id), c = center(q);
            bool touch = length(c - (p + V{0, LIFT, 0})) < 5.5f;
            for (int i = 0; i < 15 && !touch; ++i) {
                const V d = c - part(i);
                const float w = PARTS[i].width / 2 + .3f, h = PARTS[i].height / 2 + .9f;
                touch = std::abs(d.x) < w && std::abs(d.z) < w && std::abs(d.y) < h;
            }
            if (!touch) continue;
            damage(id, 8 * HURT, "physical");
            impulse(id, unit(q - p) * 5 + V{0, 3, 0});
        }
    }

    // ---- bolts, their spheres, and meteors (their own ticks) ------------------------------
    void levitate(V at, float radius) {
        for (auto& f : fighters) if (alive(f.id) && length(center(pos(f.id)) - at) < radius + .5f) f.lift = std::max(f.lift, 2.0f);
    }
    void burst(Bolt& b) {
        b.sphere = 0;
        b.v = V{};
        remove(b.mesh);
        fx("leviathan_bolt_burst", b.p, 1.67f);
        for (auto& f : fighters) if (alive(f.id) && length(center(pos(f.id)) - b.p) < 4) damage(f.id, 6 * HURT);
        levitate(b.p, 2);
        event("leviathan.levitation.created");
    }
    void bolts_tick() {
        for (size_t i = 0; i < bolts.size();) {
            Bolt& b = bolts[i];
            if (b.sphere >= 0) {   // the levitation sphere: swells, then holds for 8 s
                ++b.sphere;
                const float radius = 5 * std::min(1.0f, .3f + b.sphere / 8.0f);
                if (b.sphere % 10 == 0) levitate(b.p, radius);
                if (b.sphere % 2 == 0) fx("leviathan_levitation", b.p, radius / 3);
                if (b.sphere >= 160) { bolts.erase(bolts.begin() + i); continue; }
                ++i;
                continue;
            }
            // It speeds up over its flight and curves after its target.
            const float speed = std::min(.85f * (1 + 2 * std::min(1.0f, b.life / 170.0f)), length(b.v) + .04f);
            V direction = unit(b.v, V{0, 1, 0});
            if (b.life > 6 && b.life < 150 && alive(b.target)) {
                const V want = center(pos(b.target)) - b.p;
                if (dot(want, want) > 1e-4f) {
                    const float turn = std::min(.5f, .08f * speed / .85f);
                    direction = unit(direction * (1 - turn) + unit(want) * turn);
                }
            }
            b.v = direction * speed;
            const V next = b.p + b.v;
            unsigned touched = 0;
            for (auto& f : fighters)
                if (alive(f.id) && seg_dist(center(pos(f.id)), b.p, next) < 1.2f) { touched = f.id; break; }
            V hit;
            if (touched) {
                damage(touched, 11 * HURT);
                b.p = center(pos(touched));
                burst(b);
            } else if (solid_ray(b.p, direction, speed, &hit)) {
                b.p = hit;
                burst(b);
            } else {
                b.p = next;
                if (++b.life > 170) burst(b);
                else {
                    place(b.mesh, b.p);
                    rotation(b.mesh, b.life * 7.0f, b.life * 5.5f, b.life * 4.25f);
                    fx("leviathan_bolt_trail", b.p, 1, direction * -1.0f);
                }
            }
            ++i;
        }
    }
    void meteors_tick() {
        for (size_t i = 0; i < meteors.size();) {
            Meteor& m = meteors[i];
            if (!m.locked) {   // it follows its fighter until it is 40 m above them
                if (!alive(m.target) || m.p.y - m.impact.y <= 40) { m.locked = true; event("leviathan.meteor.locked"); }
                else {
                    const V want = pos(m.target);
                    const float dx = want.x - m.impact.x, dz = want.z - m.impact.z, d = std::sqrt(dx * dx + dz * dz);
                    const float k = d < 1e-6f ? 0 : std::min(d, .45f) / d;
                    m.impact = V{m.impact.x + dx * k, want.y, m.impact.z + dz * k};
                    m.p.x = m.impact.x;
                    m.p.z = m.impact.z;
                }
            }
            if (ticks % 4 == 0) fx(m.locked ? "leviathan_meteor_locked" : "leviathan_meteor_warning", ground_point(m.impact), 2);
            fx("leviathan_meteor_trail", m.p);
            bool boom = false;
            if (m.delay > 0) --m.delay;
            else {
                m.fall = std::min(m.fall + .07f, 1.7f);
                m.p.y -= m.fall;
                const float g = ground_at(m.p);
                boom = m.p.y <= g || m.p.y <= m.impact.y + .5f;
                for (auto& f : fighters) {
                    if (boom || !alive(f.id)) continue;
                    const V q = pos(f.id);
                    boom = std::abs(q.x - m.p.x) < 1.5f && std::abs(q.z - m.p.z) < 1.5f && q.y < m.p.y + 2.2f && q.y + 1.8f > m.p.y;
                }
                if (boom) m.p.y = std::max(m.p.y, g);
            }
            place(m.mesh, m.p);
            rotation(m.mesh, 0, ticks * 4.0f, 0);
            if (boom) {
                // The blast (power 3.5, at a third of its strength), then a tenth of their health.
                const V at = m.p + V{0, 1, 0};
                for (auto& f : fighters) {
                    if (!alive(f.id)) continue;
                    const float d = length(center(pos(f.id)) - at);
                    if (d < 7) {
                        const float k = 1 - d / 7;
                        damage(f.id, ((k * k + k) / 2 * 49 + 1) * .3f * HURT, "physical");
                        impulse(f.id, unit(pos(f.id) - at) * (5 * k) + V{0, 4 * k, 0});
                    }
                    if (d <= 6.3f) damage(f.id, maxhp(f.id) * .10f);
                }
                if (log_on) say("[leviathan] meteor lands at %.1f %.1f %.1f (%s)", m.p.x, m.p.y, m.p.z, m.target ? "aimed" : "scattered");
                fx("leviathan_meteor_impact", m.p, 2);
                event("leviathan.meteor.impact");
                remove(m.mesh);
                meteors.erase(meteors.begin() + i);
                continue;
            }
            ++i;
        }
    }
    void clear_effects() {
        for (auto& b : bolts) remove(b.mesh);
        for (auto& m : meteors) remove(m.mesh);
        bolts.clear();
        meteors.clear();
    }

    // ---- staggered, collapsing, dying ----------------------------------------------------
    void stagger(int t) {
        stagger_ticks = t;
        attack = pending = NONE;
        locked_id = 0;
        blinking = false;
        beam = 0;
        beam_on = sweeping = false;
        cooldown = t + 40;
        action("hurt");
        message("ORACLES COMPLETE - STAGGERED: +60% DAMAGE", 5);
        event("leviathan.stagger");
    }
    void begin_collapse() {
        cancel_mechanics();
        clear_effects();
        collapsing = true;
        collapse_point = home + V{0, 58, 0};
        collapse_stage = collapse_tick = collapse_total = 0;
        attack = pending = NONE;
        stagger_ticks = 0;
        locked_id = 0;
        blinking = false;
        beam = 0;
        action("roar");
        message("ORACLES FAILED - THE VOID COLLAPSES", 7);
        event("leviathan.collapse");
    }
    /// It flies to the middle, high up, as fast as it can, hangs with its nose to
    /// the sky, charges for two seconds and fires a column of light straight up.
    void collapse_behaviour() {
        ++collapse_tick;
        ++collapse_total;
        const V to = collapse_point - p;
        if (collapse_stage == 0) {
            fly(collapse_point, 2.2f, 4.5f);
            if (length(to) < 14 || collapse_tick > 400) { collapse_stage = 1; collapse_tick = 0; event("leviathan.collapse.charging"); }
        } else {
            vel = vel * .85f + to * .01f;
            pitch = approach(pitch, -80, 2.5f);
            has_aim_at = false;
            aim_pitch += (1 - aim_pitch) * .1f;
            aim_yaw += (0 - aim_yaw) * .1f;
            twist_goal = .5f * std::sin(collapse_tick * .12f);
            set_roll_goal(0);
            if (collapse_stage == 1) {
                beam = 1;
                fx("leviathan_beam_charge", mouth(), 1 + collapse_tick * TICK * 2);
                if (collapse_tick >= 40) {
                    collapse_stage = 2;
                    collapse_tick = 0;
                    beam = 3;
                    action("mouth_beam", .62f, true);
                    event("leviathan.collapse.firing");
                }
            } else {
                line("leviathan_collapse_beam", mouth(), mouth() + V{0, 400, 0}, 5.2f);
                whiteout = clampf((collapse_tick * TICK - 3) / 4, 0, 1);
                ui("mechanic", "COLLAPSE - THE WORLD IS UNRAVELLING");
            }
        }
        finish_orientation();
        if ((collapse_stage == 2 && collapse_tick >= 140) || collapse_total >= 1500) {
            for (auto& f : fighters) if (alive(f.id)) attribute(f.id, "Health", 0);
            event("leviathan.collapse.wipe");
            wipe();
        }
    }
    void die() {
        state = State::Dead;
        death_time = 0;
        attack = pending = NONE;
        blinking = false;
        collapsing = false;
        stagger_ticks = 0;
        beam = 0;
        clear_effects();
        cancel_mechanics();
        clear_status();
        reset_cues();
        action("death", 1, true);
        message("ENDER LEVIATHAN DEFEATED", 8);
        event("leviathan.defeated");
    }
    void death_tick() {
        ++death_time;
        vel = vel * .9f + V{0, -.004f, 0};
        spin_down();
        if (death_time % 4 == 0) fx("leviathan_death", part(P_HEAD), 2);
        if (cue("death_burst", -1, 94, death_time)) fx("leviathan_roar", p + V{0, LIFT, 0}, 8);
        if (death_time >= 96) defeated();
    }
    void defeated() {
        state = State::Defeated;
        engaged = false;
        hide();
        if (!rewarded) {
            rewarded = true;
            a->grant_xp(a->ctx, player, 2500);
            a->set_flag(a->ctx, "leviathan.defeated", 1);
            a->set_flag(a->ctx, "leviathan.active", 0);
        }
        if (a->ui_hide) a->ui_hide(a->ctx, hud_path.c_str());
    }
    void hide() {
        p = home + V{0, -5000, 0};
        place(self, p);
        for (unsigned q : proxies) place(q, p);
        for (auto& c : crystals) place(c.id, p);
    }

    // ---- the tick ---------------------------------------------------------------------------
    void tick() {
        ++ticks;
        for (auto& f : fighters) { f.tick_prev = f.tick_now; f.tick_now = pos(f.id); }
        if (state == State::Dead) { death_tick(); if (state == State::Dead) { travel(); body(); } return; }
        ++since_hurt_clip;
        if (ticks % 40 == 0 && state == State::Fighting) {
            update_phase();
            for (auto& f : fighters) { f.threat *= .85f; if (f.threat < 1) f.threat = 0; }
        }
        has_roll_goal = has_pitch_goal = has_aim_at = false;
        aim_rate = .05f;
        roll_rate = phase >= 3 ? 6.5f : 4.5f;
        twist_goal = 0;
        make_list();
        if (attack == NONE) {
            meteor_rest = std::max(0, meteor_rest - 1);
            tail_rest = std::max(0, tail_rest - 1);
            repulsion_rest = std::max(0, repulsion_rest - 1);
        }
        if (pose_test) { vel = V{}; attack = pending = NONE; }
        else if (state == State::Dormant) { drift(); finish_orientation(); }
        else if (collapsing) {
            collapse_behaviour();
            if (state != State::Fighting) return;
        } else {
            const unsigned target = find_target();
            think_blink(target);
            if (stagger_ticks > 0) {   // staggered: it hangs still, open to blows
                --stagger_ticks;
                vel = vel * .9f;
                twist_goal = .35f * std::sin(ticks * .3f);
                if (ticks % 4 == 0) fx("leviathan_stagger", p + V{0, LIFT, 0}, 3);
                contact_damage();
                finish_orientation();
            } else {
                const float rate = phase >= 3 ? 1.35f : 1.0f;
                if (attack != NONE) tick_attack(target, rate);
                else if (!target) { pending = NONE; patrol(rate); }
                else if (pending == NONE) { --cooldown; patrol(rate); if (cooldown <= 0) choose(); }
                else approach_attack(target, rate);
                contact_damage();
                finish_orientation();
            }
        }
        bolts_tick();
        meteors_tick();
        travel();
        body();
        if (log_on && ticks % 10 == 0) log_state();
    }
    void travel() {
        p = p + vel;
        const float floor_y = ground_at(p) + 1;   // it may not swim into the ground
        if (p.y < floor_y) { p.y = floor_y; vel.y = std::max(vel.y, 0.0f); }
    }
    /// The body follows what it has done (the mod's tick(): the motion's inputs).
    void body() {
        float bpt = has_last ? length(p - last_position) : 0;
        if (bpt > 6) { bpt = float(motion.speed); motion.settle(yaw, pitch, roll); }
        last_position = p;
        has_last = true;
        motion.aim_yaw = aim_yaw;
        motion.aim_pitch = aim_pitch;
        motion.twist = twist;
        motion.effort = attack == NONE ? 1.0 : .7;
        motion.clip_goal = (attack == TAIL || attack == CURRENT) ? 1.0 : 0.0;
        double wind = 0, slam = 0;
        if (attack == CURRENT)
            for (int stroke : STROKES) {
                if (attack_tick >= stroke - 16 && attack_tick < stroke - 1) {
                    const double u = std::min(1.0, std::max(0.0, (attack_tick - (stroke - 16)) / 14.0));
                    wind = std::max(wind, u * u * (3 - 2 * u));
                }
                if (attack_tick >= stroke - 1 && attack_tick <= stroke + 9) slam = std::max(slam, std::sin(3.14159265 * (attack_tick - (stroke - 1)) / 10.0));
            }
        motion.push(wind, slam);
        motion.tick(yaw, pitch, roll, bpt);
        pose_now = motion.pose();
        if (pose_test) pose_case();
        shown_now = {p, yaw, pitch, roll};
    }
    /// GWS_LEVIATHAN_TEST=pose: it hangs still, level, and one known rotation (the mod's
    /// terms) is laid on at a time; the log says where the head, the right fin and the
    /// fluke went, in the body's own terms (x right, y up, z forward).
    void pose_case() {
        static const char* names[] = {"neutral", "aim right (aim_yaw +0.8)", "aim up (aim_pitch +0.6)",
                                      "right fin z +0.6 (tip should lift)", "tail_1 y +0.5 (tail should swing right)",
                                      "twist +1.6 (rear z)"};
        const int which = (ticks / 40) % 6;
        for (auto& r : pose_now) r = {0, 0, 0};
        if (which == 1) for (int i = 0; i < 4; ++i) pose_now[9 + i][1] = float(-.8 * (i == 0 ? .22 : i == 1 ? .30 : i == 2 ? .28 : .20));
        if (which == 2) for (int i = 0; i < 4; ++i) pose_now[9 + i][0] = float(.6 * (i == 0 ? .22 : i == 1 ? .30 : i == 2 ? .28 : .20));
        if (which == 3) pose_now[13][2] = .6f;
        if (which == 4) pose_now[3][1] = .5f;
        if (which == 5) for (int i = 1; i <= 8; ++i) pose_now[i][2] = .2f;
        if (ticks % 40 == 39) {
            const Frame f = shown_frame;
            const V c = placed + f.up * LIFT;
            auto local = [&](V q) { const V d = q - c; return V{dot(d, f.right), dot(d, f.up), dot(d, f.forward)}; };
            const V h = local_head(), fin = local(part(P_PECTORAL_R)), fluke = local(part(P_FLUKE)), t5 = local(part(9));
            say("[leviathan] pose %s: head %.0f/%.0f deg, right fin %.1f %.1f %.1f, tail_5 %.1f %.1f %.1f, fluke %.1f %.1f %.1f", names[which],
                std::atan2(h.x, h.z) / DEG, std::atan2(h.y, std::hypot(h.x, h.z)) / DEG, fin.x, fin.y, fin.z, t5.x, t5.y, t5.z, fluke.x, fluke.y, fluke.z);
        }
    }
    void log_state() {
        const V h = local_head();
        const float now_yaw = std::atan2(h.x, h.z) / DEG, now_up = std::atan2(h.y, std::hypot(h.x, h.z)) / DEG;
        // Where the head should point, in the same terms (the aim works when the two meet).
        float want_yaw = 0, want_up = 0;
        if (has_aim_at) {
            const V w = unit(aim_at - mouth());
            const V l{dot(w, shown_frame.right), dot(w, shown_frame.up), dot(w, shown_frame.forward)};
            want_yaw = std::atan2(l.x, l.z) / DEG;
            want_up = std::atan2(l.y, std::hypot(l.x, l.z)) / DEG;
        }
        say("[leviathan] t%d %s/%s phase %d pos %.1f %.1f %.1f yaw %.0f pitch %.0f roll %.0f speed %.2f aim %.2f/%.2f head %.0f/%.0f%s%.0f/%.0f twist %.2f hooks %d clip / %d tick",
            ticks, ATTACKS[attack].name, ATTACKS[pending].name, phase, p.x, p.y, p.z, yaw, pitch, roll, motion.speed, aim_yaw,
            aim_pitch, now_yaw, now_up, has_aim_at ? " want " : " (free) ", want_yaw, want_up, twist, cues_clip, cues_fallback);
    }

    // ---- drawing it, between two ticks ----------------------------------------------------
    void present(float alpha) {
        const Shown& b = shown_before;
        const Shown& n = shown_now;
        const V at_p = mix(b.p, n.p, alpha);
        const float y = b.yaw + wrap(n.yaw - b.yaw) * alpha, pt = b.pitch + (n.pitch - b.pitch) * alpha,
                    r = b.roll + wrap(n.roll - b.roll) * alpha;
        const Frame f = frame(y, pt, r);
        shown_frame = f;
        float e[3];
        euler_of(f, e);
        a->set_rotation_euler(a->ctx, self, e);
        placed = at_p + V{0, LIFT, 0} - f.up * LIFT;   // the model turns about its middle, not its feet
        place(self, placed);
        if (a->set_bone_rotation)
            for (int i = 0; i < NB; ++i) {
                float r[3], deg[3];
                for (int c = 0; c < 3; ++c) r[c] = pose_before[i][c] + (pose_now[i][c] - pose_before[i][c]) * alpha;
                joint_degrees(r, deg);
                a->set_bone_rotation(a->ctx, self, BONES[i], deg);
            }
        read_body();
        for (size_t i = 0; i < proxies.size(); ++i) place(proxies[i], part(PROXY_PART[i]));
    }
    void snap() { shown_now = shown_before = {p, yaw, pitch, roll}; pose_before = pose_now; }
    void rebase() {
        // Something else moved it (the world's origin): everything it holds moves with it.
        if (state == State::Defeated || state == State::Recruit) return;
        const V shift = pos(self) - placed;
        if (length(shift) < 1) return;
        p = p + shift;
        home = home + shift;
        spawn = spawn + shift;
        last_position = last_position + shift;
        shown_now.p = shown_now.p + shift;
        shown_before.p = shown_before.p + shift;
        placed = placed + shift;
        blink_to = blink_to + shift;
        collapse_point = collapse_point + shift;
        for (auto& b : bolts) b.p = b.p + shift;
        for (auto& m : meteors) { m.p = m.p + shift; m.impact = m.impact + shift; }
        for (auto& c : crystals) c.p = c.p + shift;
        for (auto& o : oracles) o.p = o.p + shift;
        for (auto& q : zones) q = q + shift;
        for (auto& f : fighters) { f.previous = f.previous + shift; f.tick_now = f.tick_now + shift; f.tick_prev = f.tick_prev + shift; }
        for (auto& q : tail_before) q = q + shift;
    }

    // ---- TestGame's encounter --------------------------------------------------------------
    void hide_oracles() {
        for (auto& o : oracles) { remove(o.visual); place(o.id, home + V{0, -5000, 0}); o.broken = false; attribute(o.id, "Health", 1000); }
    }
    void cancel_mechanics() {
        oracle_active = oracle_showing = brand_active = false;
        hide_oracles();
        order.clear();
        zones.clear();
        for (auto& id : zone_models) remove(id);
        zone_models.clear();
        for (auto& id : brand_models) remove(id);
        for (auto& f : fighters) { f.brand = -1; f.cleanse = 0; f.brand_model = 0; }
        ui("mechanic", "");
        ui("sequence", "");
        ui("brand", "");
    }
    void reset_crystals() { for (auto& c : crystals) { c.respawn = 0; attribute(c.id, "Health", 120); place(c.id, c.p); } }
    void reset_creature() {
        attack = pending = NONE;
        cooldown = 80;   // after its awakening roar
        roars_owed = 0;
        meteor_rest = 300;
        tail_rest = 40;
        repulsion_rest = 80;
        blink_cooldown = 200;
        blinking = false;
        stagger_ticks = 0;
        collapsing = false;
        spin = 0;
        next_spin = 300;
        attacks_since_bite = 0;
        focus = locked_id = 0;
        beam = 0;
        whiteout = 0;
        reset_cues();
    }
    void awaken() {
        if (state == State::Recruit) {
            p = home + V{0, 24, 0};
            vel = V{};
            pitch = roll = 0;
            motion.settle(yaw, pitch, roll);
            last_position = p;
            snap();
            placed = p;   // upright, the model's place is its position
            place(self, placed);
            for (unsigned q : proxies) attribute(q, "Health", 100000);
            fx("leviathan_teleport", p + V{0, LIFT, 0}, 3);
        }
        const float fraction = state == State::Recruit ? 1.f : clampf(attr(self, "Health") / std::max(1.f, max_health), 0, 1);
        engaged = true;
        wiped = false;
        state = State::Fighting;
        refresh_fighters();
        party = std::max(1, living());
        stat_scale = 1 + .2f * (party - 1);
        max_health = 60000 * stat_scale;
        attribute(self, "MaxHealth", max_health);
        attribute(self, "Health", max_health * fraction);
        last_health = max_health * fraction;
        for (unsigned id : proxies) attribute(id, "Health", 100000);
        attribute(self, "resist.magic", .25f);
        attribute(self, "resist.void", .4f);
        attribute(self, "resist.percent_health", .75f);
        for (unsigned id : proxies) {
            attribute(id, "resist.magic", .25f);
            attribute(id, "resist.void", .4f);
            attribute(id, "resist.percent_health", .75f);
        }
        first_oracles = 120 - clampf(float(party - 1) / 7, 0, 1) * 60;
        oracle_due = brand_due = -1;
        phase = 1;
        oracle_round = 0;
        if (a->ui_show) a->ui_show(a->ctx, hud_path.c_str());
        if (give_ammo) { a->add_item(a->ctx, player, "rifle_rounds", 600); a->add_item(a->ctx, player, "pistol_rounds", 200); }
        reset_crystals();
        supplies();
        supply_timer = 30;
        reset_creature();
        action("roar");   // it roars as the fight begins
        message("ENDER LEVIATHAN - aim for the head; destroy crystals for ammo", 7);
        event("leviathan.started");
        a->set_flag(a->ctx, "leviathan.active", 1);
        a->set_flag(a->ctx, "leviathan.phase", 1);
    }
    void supplies() {
        const char* items[]{"rifle_rounds", "pistol_rounds", "shotgun_shells"};
        const int limits[]{600, 200, 60};
        for (auto& f : fighters)
            if (alive(f.id) && hdist(pos(f.id), home) < 260)
                for (int i = 0; i < 3; ++i) {
                    const int held = a->item_count ? a->item_count(a->ctx, f.id, items[i]) : 0;
                    if (held < limits[i]) a->add_item(a->ctx, f.id, items[i], limits[i] - held);
                }
        event("leviathan.supplies");
    }
    void crystals_tick(float dt) {
        int feeding = 0;
        healing_tick += dt;
        for (auto& c : crystals) {
            if (c.respawn > 0) {
                c.respawn -= dt;
                if (c.respawn <= 0) { attribute(c.id, "Health", 120); place(c.id, c.p); fx("leviathan_crystal_return", c.p, 2); event("leviathan.crystal.respawn"); }
            } else if (attr(c.id, "Health") <= 0) {
                c.respawn = 210 - clampf(float(party - 1) / 7, 0, 1) * 90;
                place(c.id, home + V{0, -5000, 0});
                fx("leviathan_crystal_break", c.p, 2);
                event("leviathan.crystal.destroyed");
                for (auto& f : fighters) if (alive(f.id) && hdist(pos(f.id), home) < 260) a->add_item(a->ctx, f.id, "rifle_rounds", 60);
            } else if (length(c.p - p) < 150) {
                ++feeding;
                if (fx_dt > 0) line("leviathan_healing_link", c.p, p + V{0, LIFT, 0});
            }
        }
        if (healing_tick >= .25f) {
            const float step = healing_tick;
            healing_tick = 0;
            if (feeding && stagger_ticks <= 0 && attr(self, "Health") < max_health) {
                attribute(self, "Health", std::min(max_health, attr(self, "Health") + feeding * (max_health * .0002f) * step));
                event("leviathan.healed");
            }
        }
    }
    void start_oracles() {
        if (oracles.size() < 6) { a->log(a->ctx, "Leviathan ERROR: missing permanent Oracle targets"); return; }
        cancel_mechanics();
        oracle_active = oracle_showing = true;
        oracle_progress = 0;
        oracle_shown = -1;
        oracle_show = 0;
        oracle_step = 1.3f;
        const int count = std::min<int>(int(oracles.size()), 6 + 2 * std::min(oracle_round, party)), n = std::min(10, count - 2);
        std::vector<int> pool;
        for (int i = 0; i < count; ++i) pool.push_back(i);
        for (int i = count - 1; i > 0; --i) std::swap(pool[i], pool[rint(i + 1)]);
        order.assign(pool.begin(), pool.begin() + n);
        oracle_left = 2 + 45 + 10 * n + 1.3f * n;
        for (int i = 0; i < count; ++i) {
            const float t = i * 2 * PI / count;
            oracles[i].p = ground_point(home + V{std::cos(t) * 80, 0, std::sin(t) * 80});
            oracles[i].broken = false;
            place(oracles[i].id, oracles[i].p);
            attribute(oracles[i].id, "Health", 1000);
            if (!oracles[i].visual) oracles[i].visual = model(("assets/models/ender_leviathan/leviathan_oracle_" + std::to_string(i) + ".glb").c_str(), oracles[i].p);
        }
        if (attack == NONE && !collapsing) action("roar");   // announceMechanic
        message("THE ORACLES AWAKEN - watch, then shoot the sequence in order", 7);
        event("leviathan.oracles.started");
    }
    void oracle_tick(float dt) {
        if (!oracle_active) return;
        oracle_left -= dt;
        oracle_show += dt;
        const int count = std::min<int>(int(oracles.size()), 6 + 2 * std::min(oracle_round, party));
        if (oracle_left <= 0) { begin_collapse(); return; }
        const int showing = int(oracle_show / oracle_step);
        if (oracle_showing && showing >= int(order.size())) {
            oracle_showing = false;
            oracle_shown = -1;
            message("ORACLES - shoot the sequence shown above", 4);
            event("leviathan.oracles.input");
        }
        if (oracle_showing && showing < int(order.size()) && showing != oracle_shown) {
            oracle_shown = showing;
            fx("leviathan_oracle_cue", oracles[order[showing]].p + V{0, 3, 0}, 2);
            event("leviathan.oracles.cue");
        }
        bool right = false, wrong = false;
        for (int i = 0; i < count; ++i) {
            auto& o = oracles[i];
            if (!o.broken && attr(o.id, "Health") < 1000 && !oracle_showing) { if (i == order[oracle_progress]) right = true; else wrong = true; }
            if (!o.broken) attribute(o.id, "Health", 1000);
            if (fx_dt > 0 && !o.broken) {
                fx(("leviathan_oracle_" + std::to_string(i)).c_str(), o.p);
                if (oracle_showing && order[std::min(showing, int(order.size() - 1))] == i) fx("leviathan_oracle_cue", o.p + V{0, 3, 0});
            }
        }
        if (right) {
            auto& o = oracles[order[oracle_progress]];
            o.broken = true;
            remove(o.visual);
            place(o.id, home + V{0, -5000, 0});
            fx("leviathan_oracle_success", o.p, 2);
            ++oracle_progress;
            event("leviathan.oracles.correct");
            char text[80];
            std::snprintf(text, sizeof text, "Oracle %d / %d resolved", oracle_progress, int(order.size()));
            message(text, 1);
            if (oracle_progress == int(order.size())) {
                oracle_active = false;
                hide_oracles();
                ++oracle_round;
                oracle_due = 180 + rnd() * 120;
                ui("mechanic", "");
                ui("sequence", "");
                stagger(300);
                return;
            }
        } else if (wrong) {
            oracle_left -= 8;
            oracle_progress = 0;
            oracle_showing = true;
            oracle_show = 0;
            oracle_step = .7f;
            oracle_shown = -1;
            for (int i = 0; i < count; ++i) {
                oracles[i].broken = false;
                place(oracles[i].id, oracles[i].p);
                attribute(oracles[i].id, "Health", 1000);
                if (!oracles[i].visual) oracles[i].visual = model(("assets/models/ender_leviathan/leviathan_oracle_" + std::to_string(i) + ".glb").c_str(), oracles[i].p);
            }
            message("WRONG ORACLE - 8 seconds lost; watch again", 4);
            event("leviathan.oracles.wrong");
        }
        const char* colors[]{"RED", "AMBER", "YELLOW", "GREEN", "CYAN", "VIOLET", "BLUE", "TEAL"};
        char label[120];
        std::snprintf(label, sizeof label, "ORACLES %d/%d | %ds | %s", oracle_progress, int(order.size()), int(oracle_left), oracle_showing ? "WATCH" : "SHOOT IN ORDER");
        ui("mechanic", label);
        std::string text;
        for (size_t i = 0; i < order.size(); ++i) {
            if (i) text += " > ";
            if (int(i) < oracle_progress) text += "[OK] ";
            if (oracle_showing && int(i) > showing) { text += "?"; continue; }
            text += std::to_string(order[i] + 1) + " " + colors[order[i] % 8];
        }
        ui("sequence", text);
    }
    void start_brand() {
        if (oracle_active || brand_active) return;
        const int count = std::min(3, std::max(1, (living() + 2) / 3));
        int n = 0;
        for (auto& f : fighters)
            if (alive(f.id) && n < count) {
                f.brand = 30;
                f.cleanse = 0;
                brand_models[n++] = f.brand_model = model("assets/models/ender_leviathan/leviathan_void_brand.glb", pos(f.id) + V{0, 3, 0});
            }
        if (!n) return;
        brand_active = true;
        zones.clear();
        for (int i = 0; i < n + 1; ++i) {
            const float t = 2 * PI * i / (n + 1) + .7f;
            const V q = ground_point(home + V{std::cos(t) * 52, 0, std::sin(t) * 52});
            zones.push_back(q);
            zone_models.push_back(model("assets/models/ender_leviathan/leviathan_cleansing_anchor.glb", q));
        }
        message("VOID BRAND - stand ALONE in a pale circle for 4 seconds", 7);
        event("leviathan.brand.started");
    }
    void brand_tick(float dt) {
        if (!brand_active) return;
        bool any = false;
        float least = 30, most = 0;
        for (auto& f : fighters) {
            if (f.brand < 0) continue;
            f.brand -= dt;
            const V q = pos(f.id);
            place(f.brand_model, q + V{0, 3, 0});
            rotation(f.brand_model, 0, clock * 30, 0);
            const float pulse = 1 + (f.brand < 3 ? .25f : .08f) * std::sin(clock * (f.brand < 3 ? 14 : 5));
            scale(f.brand_model, {pulse, pulse, pulse});
            int inside = -1;
            bool contested = false;
            for (size_t i = 0; i < zones.size(); ++i)
                if (hdist(q, zones[i]) <= 3.2f && std::abs(q.y - zones[i].y) < 4) {
                    inside = int(i);
                    int others = 0;
                    for (auto& o : fighters) if (o.brand >= 0 && alive(o.id) && hdist(pos(o.id), zones[i]) <= 3.2f) ++others;
                    contested = others > 1;
                    break;
                }
            if (inside >= 0 && !contested) f.cleanse += dt; else f.cleanse = 0;
            if (fx_dt > 0) {
                fx("leviathan_brand", q + V{0, 1, 0}, 2.67f);
                if (inside >= 0) fx(contested ? "leviathan_cleanse_contested" : "leviathan_cleanse_progress", zones[inside]);
            }
            least = std::min(least, f.brand);
            most = std::max(most, f.cleanse);
            if (f.cleanse >= 4 || !alive(f.id)) {
                f.brand = -1;
                for (auto& id : brand_models) if (id == f.brand_model) id = 0;
                remove(f.brand_model);
                fx("leviathan_cleanse_success", q + V{0, 1, 0}, 2);
                event("leviathan.brand.cleansed");
            } else if (f.brand <= 0) {
                f.brand = -1;
                for (auto& id : brand_models) if (id == f.brand_model) id = 0;
                remove(f.brand_model);
                fx("leviathan_brand_blast", q + V{0, 1, 0}, 2.67f);
                for (auto& victim : fighters)
                    if (alive(victim.id) && length(pos(victim.id) - q) < 8) { damage(victim.id, maxhp(victim.id) * .6f); status(victim, 0, 10); }
                event("leviathan.brand.burst");
            } else any = true;
        }
        if (fx_dt > 0) for (auto& q : zones) fx("leviathan_cleanse", q);
        char text[128];
        std::snprintf(text, sizeof text, "VOID BRAND: %ds | cleanse %.1f/4 s | pale circles, alone", int(std::max(0.f, least)), most);
        ui("brand", text);
        if (!any) {
            brand_active = false;
            for (auto& id : zone_models) remove(id);
            zone_models.clear();
            for (auto& id : brand_models) remove(id);
            zones.clear();
            brand_due = 75 + rnd() * 30;
            ui("brand", "");
        }
    }
    void wipe() {
        clear_effects();
        cancel_mechanics();
        clear_status();
        engaged = false;
        wiped = true;
        state = State::Recruit;
        recruit_time = 10;
        whiteout = 0;
        collapsing = false;
        attack = pending = NONE;
        reset_crystals();
        hide();
        play("idle", true, 1, .3f, true);
        action_left = 0;
        a->set_flag(a->ctx, "leviathan.active", 0);
        event("leviathan.wipe");
        message("THE ENCOUNTER RESETS - return to face it again", 6);
        if (a->ui_hide) a->ui_hide(a->ctx, hud_path.c_str());
    }

    // ---- start and update -------------------------------------------------------------------
    void start(const SchizoScriptApi* api, unsigned entity) {
        *this = Boss{};
        a = api;
        char capability[8]{};
        if (!a->get_setting || a->get_setting(a->ctx, "engine.combat_api", capability, 8) != 1 || capability[0] != '1') {
            message("Leviathan requires GameWorldshaper 0.8.55. Open TestGame through its project launcher.", 8);
            return;
        }
        char hooks[8]{};
        anim_api = a->get_setting(a->ctx, "engine.animation_events", hooks, 8) == 1 && hooks[0] == '1' && a->bone_point;
        log_on = std::getenv("GWS_LEVIATHAN_LOG") != nullptr;
        review = std::getenv("GWS_LEVIATHAN_REVIEW") != nullptr;
        self = entity;
        p = placed = pos(self);   // placed upright: its position is the mod's position
        home = {p.x, p.y - 24, p.z};
        player = a->find_entity(a->ctx, "Player");
        if (!player) { message("Leviathan needs an entity named Player", 8); return; }
        if (a->get_param_float) {
            home.y = a->get_param_float(a->ctx, self, "ground_height", home.y);
            activation_range = a->get_param_float(a->ctx, self, "activation_range", 120);
            first_brand = a->get_param_float(a->ctx, self, "first_brand", 60);
        }
        if (a->get_param_bool) give_ammo = a->get_param_bool(a->ctx, self, "give_ammo", false);
        if (a->get_param_string) {
            char path[256]{};
            a->get_param_string(a->ctx, self, "hud", path, 256, "ui/leviathan_boss.ui");
            if (path[0]) hud_path = path;
        }
        // The way the model faces (its +z) as placed: the mod's yaw.
        float f[3]{0, 0, -1};
        if (a->get_forward && a->get_forward(a->ctx, self, f)) yaw = std::atan2(f[0], -f[2]) / DEG;
        spawn = p;
        max_health = attr(self, "MaxHealth");
        if (max_health <= 0) max_health = 60000;
        last_health = attr(self, "Health");
        for (int i = 0; i < 15; ++i) {
            const unsigned id = a->find_entity(a->ctx, ("Leviathan hit " + std::to_string(i)).c_str());
            if (!id) continue;
            proxies.push_back(id);
            attribute(id, "Health", 100000);
            if (!a->set_damage_receiver || !a->set_damage_receiver(a->ctx, id, self, i == 0 ? 1.75f : (i == 1 || i == 9) ? 1.2f : 1.f))
                message("Leviathan hit region could not be linked", 8);
        }
        for (int i = 0; i < 10; ++i) {
            const unsigned id = a->find_entity(a->ctx, ("Leviathan crystal " + std::to_string(i)).c_str());
            if (!id) continue;
            V q = pos(id);
            q.y = ground_at(q) + 2;
            crystals.push_back({id, q, 0});
            place(id, q);
        }
        for (int i = 0; i < 8; ++i) {
            const unsigned id = a->find_entity(a->ctx, ("Leviathan oracle " + std::to_string(i)).c_str());
            if (id) oracles.push_back({id, {}, false, 0});
        }
        hide_oracles();
        refresh_fighters();
        motion.settle(yaw, pitch, roll);
        body();
        snap();
        for (int i = 0; i < 15; ++i) parts_now[i] = world(PARTS[i].x, PARTS[i].y, PARTS[i].z);
        mouth_now = world(0, 0, -302);
        head_dir_now = frame(yaw, pitch, roll).forward;
        say("Leviathan: animation hooks %s", anim_api ? "on (clip events, bone points)" : "off (an engine before 0.8.57: cues by tick)");
        if (a->get_flag && a->get_flag(a->ctx, "leviathan.defeated") && attr(self, "Health") <= 0) {
            state = State::Defeated;
            hide();
            play("death", false, 100, 0, true);
            return;
        }
        play("idle", true, 1, 0, true);
        if (activation_range <= 0) awaken();
        else a->log(a->ctx, "Leviathan placed in the main world: approach its territory or damage it to awaken.");
        test_hooks();
    }
    /// Explicit, process-local smoke-test hooks; they never save the project or player.
    void test_hooks() {
        const char* test = std::getenv("GWS_LEVIATHAN_TEST");
        if (!test) return;
        const std::string t(test);
        place(player, test_spot());
        if (state != State::Fighting) awaken();
        oracle_due = 180;
        brand_due = 60;
        if (a->ui_hide) a->ui_hide(a->ctx, "ui/menu.ui");
        refresh_fighters();
        make_list();
        if (t == "oracles" || t == "oracle_shoot") { phase = 2; start_oracles(); if (t == "oracle_shoot") stagger(20000); }
        else if (t == "brand") { phase = 2; start_brand(); }
        else if (t == "collapse") begin_collapse();
        else if (t == "death") { attribute(self, "Health", 0); die(); }
        else if (t == "teleport") { blink_cooldown = 0; start_blink(home + V{45, 30, 0}, pos(player), false); }
        else if (t == "patrol") cooldown = 1 << 30;
        else if (t == "pose") { pose_test = true; yaw = pitch = roll = 0; motion.settle(0, 0, 0); }
        else {
            const Attack attacks[]{SALVO, BEAM, ROAR, METEORS, TAIL, CURRENT, BITE};
            for (Attack x : attacks) {
                // "<attack>": at once; "<attack>+": chosen, so it flies there first as in the fight.
                const bool now = t == ATTACKS[x].name, approach = t == std::string(ATTACKS[x].name) + "+";
                if (!now && !approach) continue;
                phase = 3;
                focus = player;
                if (now) { if (x == BEAM) locked_id = player; begin(x); }
                else { pending = x; pending_ticks = 0; diving = false; cooldown = 0; }
            }
        }
    }
    /// Where the tests hold the player: GWS_LEVIATHAN_TEST_SPOT=x,y,z from home, or 110 m out, 30 up.
    V test_spot() const {
        V at{0, 30, 110};
        if (const char* s = std::getenv("GWS_LEVIATHAN_TEST_SPOT")) std::sscanf(s, "%f,%f,%f", &at.x, &at.y, &at.z);
        return home + at;
    }
    void env() { if (a->set_environment) a->set_environment(a->ctx, darkness, whiteout); }

    void update(const SchizoScriptApi* api, float dt) {
        a = api;
        if (!self || !player || dt <= 0) return;
        dt = std::min(dt, .1f);
        if (review) { place(player, test_spot()); attribute(player, "Health", maxhp(player)); }
        rebase();
        listen();
        clock += dt;
        hud += dt;
        fx_time += dt;
        fx_dt = fx_time >= .05f ? fx_time : 0;
        if (fx_dt > 0) fx_time = 0;
        const bool here = state != State::Defeated && state != State::Recruit;
        darkness += clampf((here ? 1.f : 0.f) - darkness, -dt / 8, dt / 4);
        status_tick(dt);
        focus_timer -= dt;
        if (focus_timer <= 0) { refresh_fighters(); focus_timer = 2; }
        if (state == State::Defeated) { env(); return; }
        if (state == State::Recruit) {
            if (living() >= party) {
                recruit_time -= dt;
                if (recruit_time <= 0) { wiped = false; phase = 1; awaken(); event("leviathan.reset"); }
            } else recruit_time = 10;
            env();
            return;
        }
        if (state != State::Dead) {
            const float h = attr(self, "Health"), drop = std::max(0.f, last_health - h);
            // Damage has already resolved its type, region and resistance exactly once.
            if (stagger_ticks > 0 && drop > 0) a->apply_damage(a->ctx, self, drop * .6f, "");
            if (engaged && drop > 0 && attack == NONE && stagger_ticks <= 0 && !collapsing && since_hurt_clip > 40) {
                since_hurt_clip = 0;
                action("hurt");
                event("leviathan.hurt");
            }
            if (attr(self, "Health") <= 0) { engaged = true; die(); }
        }
        if (state == State::Dormant && alive(player) &&
            (attr(self, "Health") < max_health || hdist(pos(player), home) <= activation_range))
            awaken();
        if (state == State::Fighting) {
            bool any_alive = false;
            for (auto& f : fighters) any_alive = any_alive || alive(f.id);
            if (collapsing ? !any_alive : living() == 0) { wipe(); env(); return; }
            if (!collapsing && activation_range > 0 && hdist(pos(player), home) > activation_range + 140 && living() <= 1) {
                wipe();
                event("leviathan.disengaged");
                env();
                return;
            }
            if (!collapsing) {
                supply_timer -= dt;
                if (supply_timer <= 0) { supplies(); supply_timer = 30; }
                crystals_tick(dt);
                if (phase >= 2) {
                    oracle_due -= dt;
                    brand_due -= dt;
                    if (!oracle_active && !brand_active && stagger_ticks <= 0) {
                        if (oracle_due <= 0) start_oracles();
                        else if (brand_due <= 0) start_brand();
                    }
                    oracle_tick(dt);
                    brand_tick(dt);
                }
            }
            if (a->get_flag)
                for (const char* n : {"oracles", "brand", "collapse"}) {
                    const std::string key = std::string("leviathan.trigger.") + n;
                    if (!a->get_flag(a->ctx, key.c_str())) continue;
                    a->set_flag(a->ctx, key.c_str(), 0);
                    if (!std::strcmp(n, "oracles")) start_oracles();
                    else if (!std::strcmp(n, "brand")) start_brand();
                    else begin_collapse();
                }
            const char* test = std::getenv("GWS_LEVIATHAN_TEST");
            if (test && !std::strcmp(test, "oracle_shoot") && oracle_active && !oracle_showing) {
                const unsigned camera = a->find_entity(a->ctx, "PlayerCamera");
                const V goal = oracles[order[oracle_progress]].p + V{0, 1.5f, 0};
                place(player, goal + V{0, 8, 35});
                const V d = goal - pos(camera);
                const float y = std::atan2(-d.x, -d.z) / DEG, x = std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z)) / DEG;
                rotation(player, 0, y, 0);
                rotation(camera, x, y, 0);
            }
        }
        if (state == State::Recruit) { env(); return; }
        // The creature, on the mod's clock.
        acc += dt;
        for (int n = 0; acc >= TICK && n < 4; ++n) {
            acc -= TICK;
            shown_before = shown_now;
            pose_before = pose_now;
            tick();
            if (jumped) { shown_before = shown_now; pose_before = pose_now; jumped = false; }
            if (state == State::Recruit || state == State::Defeated) { env(); return; }
        }
        acc = std::min(acc, TICK);
        clips(dt);
        present(acc / TICK);
        last_health = attr(self, "Health");
        if (hud >= .2f && engaged) {
            hud = 0;
            char text[160];
            std::snprintf(text, sizeof text, "ENDER LEVIATHAN  %d / %d | PHASE %d", int(std::max(0.f, attr(self, "Health"))), int(max_health), phase);
            ui("boss_health", text);
            int active = 0;
            for (auto& c : crystals) if (c.respawn <= 0) ++active;
            std::snprintf(text, sizeof text, "HEALING CRYSTALS %d/10 | %s", active,
                          stagger_ticks > 0 ? "STAGGERED +60% DAMAGE" : "Destroy crystals | Ammo refills every 30s");
            ui("crystals", text);
        }
        env();
    }
};
Boss boss;

}  // namespace leviathan

SCHIZO_SCRIPT void on_start(const SchizoScriptApi* api, unsigned entity) { leviathan::boss.start(api, entity); }
SCHIZO_SCRIPT void on_update(const SchizoScriptApi* api, unsigned, float dt) { leviathan::boss.update(api, dt); }
