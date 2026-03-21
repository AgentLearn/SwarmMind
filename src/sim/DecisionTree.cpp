#include "DecisionTree.hpp"
#include "SpatialGrid.hpp"
#include <algorithm>
#include <ranges>
#include <limits>
#include <span>

template<typename... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

// ── Internal helpers ──────────────────────────────────────────────────────────

namespace {

Decision decide_one(const Drone& d,
                    const std::vector<Drone>& all,
                    const SpatialGrid& grid)
{
    if (!d.is_alive()) return DeadDecision{};

    const Team enemy_team = (d.team == Team::Alpha) ? Team::Beta : Team::Alpha;
    const auto [enemy_id, enemy_dist] =
        grid.nearest_enemy(d, enemy_team, all, kFlankRange);

    if (enemy_id == std::size_t(-1))
        return PatrolDecision{};

    const Drone& enemy = all[enemy_id];

    if (d.health_pct() < kLowHealthPct)
        return RetreatDecision{};

    if (enemy_dist <= kAttackRange)
        return AttackDecision{enemy_id};

    if (enemy_dist <= kFlankRange) {
        const std::size_t allies = grid.count_allies(d, all, kAllyHelpRadius);
        if (allies >= 2) {
            const Vec3 to_enemy = (enemy.pos - d.pos).norm();
            const Vec3 up       = {0.0, 1.0, 0.0};
            Vec3 perp = (d.id % 2 == 0)
                ? to_enemy.cross(up).norm()
                : up.cross(to_enemy).norm();
            return FlankDecision{enemy_id, perp};
        }
        return AttackDecision{enemy_id};
    }

    return PatrolDecision{};
}

} // anonymous namespace

// ── Public implementation ─────────────────────────────────────────────────────

namespace decision_tree {

std::vector<Decision> decide_all(const std::vector<Drone>& drones,
                                 const SpatialGrid& grid)
{
    std::vector<Decision> out;
    out.reserve(drones.size());
    std::ranges::transform(drones, std::back_inserter(out),
        [&](const Drone& d) { return decide_one(d, drones, grid); });
    return out;
}

void apply_decisions(std::vector<Drone>& drones,
                     std::vector<Decision> decisions)
{
    for (auto&& [drone, decision] : std::views::zip(drones, decisions)) {
        std::visit(overloaded{
            [&](const AttackDecision& a) {
                drone.state      = DroneState::Attack;
                drone.target_idx = a.target_idx;
            },
            [&](const FlankDecision& f) {
                drone.state        = DroneState::Flank;
                drone.target_idx   = f.target_idx;
                drone.flank_offset = f.offset;
            },
            [&](RetreatDecision) {
                drone.state      = DroneState::Retreat;
                drone.target_idx = std::nullopt;
            },
            [&](PatrolDecision) {
                drone.state      = DroneState::Patrol;
                drone.target_idx = std::nullopt;
            },
            [&](DeadDecision) {
                drone.state      = DroneState::Dead;
                drone.target_idx = std::nullopt;
            },
        }, decision);
    }
}

} // namespace decision_tree
