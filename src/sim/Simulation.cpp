#include "Simulation.hpp"
#include "DecisionTree.hpp"
#include "SpatialGrid.hpp"
#include <algorithm>
#include <cmath>
#include <glaze/glaze.hpp>
#include <random>
#include <ranges>

// ── Glaze wire-format structs ─────────────────────────────────────────────────
// Must have external linkage (no anonymous namespace) for glaze reflection.

struct DroneSnap {
    int              id{}, team{};
    double           x{}, y{}, z{};
    double           vx{}, vy{}, vz{};
    double           health{}, maxHp{};
    std::string_view state{};
};

struct WorldSnap {
    std::uint64_t          tick{};
    std::string_view       phase{};
    std::size_t            alphaAlive{}, betaAlive{};
    double                 resetIn{};
    std::vector<DroneSnap> drones{};
};

// ── Module-internal helpers (anonymous namespace) ─────────────────────────────

namespace {

constexpr double kTickDt      = 0.05;
constexpr double kMaxSpeed    = 9.0;
constexpr double kPatrolSpeed = 4.5;
constexpr double kBoundsHalf  = 150.0;
constexpr double kBoundsYMax  = 100.0;
constexpr double kSteerFactor = 0.12;
constexpr double kResetDelay  = 5.0;

thread_local std::mt19937 rng{std::random_device{}()};

double rand_range(double lo, double hi) {
    return std::uniform_real_distribution{lo, hi}(rng);
}
Vec3 random_waypoint() {
    return { rand_range(-100, 100), rand_range(10, 70), rand_range(-100, 100) };
}
void clamp_pos(Vec3& v) noexcept {
    v.x = std::clamp(v.x, -kBoundsHalf, kBoundsHalf);
    v.y = std::clamp(v.y, 2.0,          kBoundsYMax);
    v.z = std::clamp(v.z, -kBoundsHalf, kBoundsHalf);
}

} // anonymous namespace

// ── Construction ──────────────────────────────────────────────────────────────

Simulation::Simulation(std::size_t drones_per_team)
    : drones_per_team_{drones_per_team}
{
    drones_.reserve(drones_per_team * 2);
    decisions_.reserve(drones_per_team * 2);
    hits_.reserve(drones_per_team * 2);
    json_buf_.reserve(64 * 1024);
    spawn_drones();
}

// ── Public API ────────────────────────────────────────────────────────────────

void Simulation::step()
{
    std::scoped_lock lock{mu_};
    ++tick_;

    if (phase_ == Phase::Running) {
        if (alive_count(Team::Alpha) == 0 || alive_count(Team::Beta) == 0) {
            phase_           = Phase::Resetting;
            reset_countdown_ = kResetDelay;
        }
    }

    if (phase_ == Phase::Resetting) {
        reset_countdown_ -= kTickDt;
        if (reset_countdown_ <= 0.0) {
            spawn_drones();
            phase_ = Phase::Running;
        }
        return;
    }

    // ── 0. Rebuild spatial grid — O(N) ───────────────────────────────────────
    grid_.rebuild(drones_);

    // ── 1. Decision pass — O(N·k) via grid ───────────────────────────────────
    decisions_ = decision_tree::decide_all(drones_, grid_);

    // ── 2. Apply decisions ────────────────────────────────────────────────────
    decision_tree::apply_decisions(drones_, std::move(decisions_));

    // ── 3. Move ───────────────────────────────────────────────────────────────
    move_drones();

    // ── 4. Combat ─────────────────────────────────────────────────────────────
    resolve_combat();
}

std::string Simulation::snapshot_json() const
{
    std::scoped_lock lock{mu_};
    return build_json();
}

// ── Private: movement ─────────────────────────────────────────────────────────

void Simulation::move_drones()
{
    for (auto& d : drones_) {
        if (!d.is_alive()) continue;

        Vec3 desired{};

        switch (d.state) {
        case DroneState::Attack:
            if (d.target_idx) {
                const auto& t = drones_[*d.target_idx];
                if (t.is_alive())
                    desired = (t.pos - d.pos).norm() * kMaxSpeed;
            }
            break;

        case DroneState::Flank:
            if (d.target_idx) {
                const auto& t = drones_[*d.target_idx];
                if (t.is_alive())
                    desired = ((t.pos - d.pos).norm()
                               + d.flank_offset * 1.2).norm() * kMaxSpeed;
            }
            break;

        case DroneState::Retreat: {
            const auto [eid, dummy] = grid_.nearest_enemy_any(d, drones_);
            if (eid != std::size_t(-1))
                desired = (d.pos - drones_[eid].pos).norm() * (kMaxSpeed * 1.1);
            break;
        }

        case DroneState::Patrol:
            if (d.pos.dist(d.waypoint) < 6.0)
                d.waypoint = random_waypoint();
            desired = (d.waypoint - d.pos).norm() * kPatrolSpeed;
            break;

        case DroneState::Dead:
            continue;
        }

        d.vel = d.vel.lerp(desired, kSteerFactor);
        if (const double spd = d.vel.len(); spd > kMaxSpeed)
            d.vel = d.vel * (kMaxSpeed / spd);
        d.pos = d.pos + d.vel * kTickDt;
        clamp_pos(d.pos);
    }
}

// ── Private: combat ───────────────────────────────────────────────────────────

void Simulation::resolve_combat()
{
    hits_.clear();

    for (const auto& d : drones_) {
        if (d.state != DroneState::Attack || !d.is_alive() || !d.target_idx)
            continue;
        const auto& t = drones_[*d.target_idx];
        if (t.is_alive() && d.pos.dist(t.pos) <= kAttackRange)
            hits_.push_back({*d.target_idx, kDamagePerTick});
    }

    for (const auto& [tidx, dmg] : hits_)
        drones_[tidx].health = std::max(0.0, drones_[tidx].health - dmg);
}

// ── Private: helpers ──────────────────────────────────────────────────────────

void Simulation::spawn_drones()
{
    drones_.clear();
    std::size_t id = 0;
    for (std::size_t i = 0; i < drones_per_team_; ++i)
        drones_.emplace_back(id++, Team::Alpha,
            Vec3{rand_range(-120,-80), rand_range(10,60), rand_range(-60,60)});
    for (std::size_t i = 0; i < drones_per_team_; ++i)
        drones_.emplace_back(id++, Team::Beta,
            Vec3{rand_range(80,120), rand_range(10,60), rand_range(-60,60)});
}

std::size_t Simulation::alive_count(Team team) const noexcept
{
    return static_cast<std::size_t>(
        std::ranges::count_if(drones_,
            [team](const Drone& d){ return d.team == team && d.is_alive(); }));
}

// ── Private: JSON serialisation (glaze) ───────────────────────────────────────

std::string Simulation::build_json() const
{
    const bool resetting = (phase_ == Phase::Resetting);

    WorldSnap snap;
    snap.tick       = tick_;
    snap.phase      = resetting ? "resetting" : "running";
    snap.alphaAlive = alive_count(Team::Alpha);
    snap.betaAlive  = alive_count(Team::Beta);
    snap.resetIn    = resetting ? std::max(0.0, reset_countdown_) : 0.0;

    snap.drones.resize(drones_.size());
    for (std::size_t i = 0; i < drones_.size(); ++i) {
        const auto& d = drones_[i];
        auto&       s = snap.drones[i];
        s.id     = static_cast<int>(d.id);
        s.team   = static_cast<int>(d.team);
        s.x = d.pos.x; s.y = d.pos.y; s.z = d.pos.z;
        s.vx = d.vel.x; s.vy = d.vel.y; s.vz = d.vel.z;
        s.health = std::max(0.0, d.health);
        s.maxHp  = d.max_health;
        s.state  = to_string(d.state);
    }

    json_buf_.clear();
    auto result = glz::write_json(snap, json_buf_);
    (void)result;
    return json_buf_;
}
