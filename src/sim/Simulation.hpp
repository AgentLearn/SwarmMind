#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include "Drone.hpp"
#include "DecisionTree.hpp"
#include "SpatialGrid.hpp"

// ── Phase ─────────────────────────────────────────────────────────────────────

enum class Phase : std::uint8_t { Running, Resetting };

// ── Simulation ────────────────────────────────────────────────────────────────

class Simulation {
public:
    explicit Simulation(std::size_t drones_per_team);

    /// Advance the world by one tick.  Thread-safe.
    void step();

    /// Serialise the current world state to a JSON string.
    [[nodiscard]] std::string snapshot_json() const;

private:
    // ── Core state ────────────────────────────────────────────────────────────
    mutable std::mutex  mu_;
    std::vector<Drone>  drones_;
    std::uint64_t       tick_            {0};
    Phase               phase_           {Phase::Running};
    double              reset_countdown_ {0.0};
    std::size_t         drones_per_team_;

    // ── Performance: reused per-tick allocations ──────────────────────────────
    SpatialGrid              grid_;        // rebuilt O(N) each tick
    std::vector<Decision>    decisions_;   // avoids alloc every tick
    struct Hit { std::size_t target_idx; double damage; };
    std::vector<Hit>         hits_;        // combat buffer
    mutable std::string      json_buf_;    // serialisation buffer

    // ── Helpers ───────────────────────────────────────────────────────────────
    void   spawn_drones();
    void   move_drones();
    void   resolve_combat();
    [[nodiscard]] std::size_t alive_count(Team team) const noexcept;
    [[nodiscard]] std::string build_json() const;
};
