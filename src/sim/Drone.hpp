#pragma once
#include <cstddef>
#include <optional>
#include <string_view>
#include "Vec3.hpp"

// ── Enumerations ──────────────────────────────────────────────────────────────

enum class Team : std::uint8_t {
    Alpha = 0,
    Beta  = 1,
};

/// The behaviour mode assigned by the decision tree each tick.
enum class DroneState : std::uint8_t {
    Patrol,
    Attack,
    Flank,
    Retreat,
    Dead,
};

/// C-string label used in the JSON wire format.
[[nodiscard]] constexpr std::string_view to_string(DroneState s) noexcept {
    switch (s) {
        case DroneState::Patrol:  return "patrol";
        case DroneState::Attack:  return "attack";
        case DroneState::Flank:   return "flank";
        case DroneState::Retreat: return "retreat";
        case DroneState::Dead:    return "dead";
    }
    return "unknown";
}

// ── Drone ─────────────────────────────────────────────────────────────────────

struct Drone {
    std::size_t id;
    Team        team;
    Vec3        pos;
    Vec3        vel;
    double      health     {100.0};
    double      max_health {100.0};
    DroneState  state      {DroneState::Patrol};

    /// Index into Simulation::drones_ of the current attack/flank target.
    std::optional<std::size_t> target_idx;

    /// Patrol destination.
    Vec3 waypoint;

    /// Perpendicular arc direction used when flanking.
    Vec3 flank_offset;

    // ── Construction ────────────────────────────────────────────────────────

    Drone(std::size_t id_, Team team_, Vec3 pos_) noexcept
        : id{id_}, team{team_}, pos{pos_}, waypoint{pos_} {}

    // ── Queries ─────────────────────────────────────────────────────────────

    [[nodiscard]] bool   is_alive()    const noexcept { return health > 0.0; }
    [[nodiscard]] double health_pct()  const noexcept { return health / max_health; }
};
