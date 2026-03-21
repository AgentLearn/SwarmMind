#pragma once
#include <cstddef>
#include <variant>
#include <vector>
#include "Drone.hpp"
#include "Vec3.hpp"
#include "SpatialGrid.hpp"

// ── Tunable constants ─────────────────────────────────────────────────────────

inline constexpr double kAttackRange    = 25.0;
inline constexpr double kFlankRange     = 55.0;
inline constexpr double kLowHealthPct   = 0.28;
inline constexpr double kAllyHelpRadius = 40.0;
inline constexpr double kDamagePerTick  = 0.7;

// ── Decision variant ──────────────────────────────────────────────────────────

struct AttackDecision { std::size_t target_idx; };
struct FlankDecision  { std::size_t target_idx; Vec3 offset; };
struct RetreatDecision{};
struct PatrolDecision {};
struct DeadDecision   {};

using Decision = std::variant<
    AttackDecision,
    FlankDecision,
    RetreatDecision,
    PatrolDecision,
    DeadDecision
>;

// ── Public API ────────────────────────────────────────────────────────────────

namespace decision_tree {

[[nodiscard]]
std::vector<Decision> decide_all(const std::vector<Drone>& drones,
                                 const SpatialGrid& grid);

void apply_decisions(std::vector<Drone>& drones,
                     std::vector<Decision> decisions);

} // namespace decision_tree
