#pragma once
#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>
#include "Drone.hpp"
#include "Vec3.hpp"

// ── SpatialGrid ───────────────────────────────────────────────────────────────
//
// Divides the world into uniform cubic cells.  Each cell holds the indices of
// alive drones whose position falls within it.
//
// At 2000 drones over a 300×300×100 world with cell_size=25:
//   Grid:           12 × 4 × 12 = 576 cells
//   Avg per cell:   ~3.5 drones
//   Attack query:   3³ = 27 cells  → ~94  candidates   (vs 2000 linear scan)
//   Flank query:    5³ = 125 cells → ~437 candidates   (vs 2000 linear scan)
//   Decision pass:  ~8× faster than brute force at 2000 drones
//
// Rebuild: O(N)   Query: O(k)  where k = drones in searched cells.

class SpatialGrid {
public:
    // ── World / grid parameters ───────────────────────────────────────────────

    static constexpr double kCellSize = 25.0;
    static constexpr double kMinX = -150.0, kMaxX = 150.0;
    static constexpr double kMinY =    0.0, kMaxY = 105.0;
    static constexpr double kMinZ = -150.0, kMaxZ = 150.0;

    static constexpr int kNX =
        static_cast<int>((kMaxX - kMinX) / kCellSize) + 1; // 13
    static constexpr int kNY =
        static_cast<int>((kMaxY - kMinY) / kCellSize) + 1; // 5
    static constexpr int kNZ =
        static_cast<int>((kMaxZ - kMinZ) / kCellSize) + 1; // 13
    static constexpr int kNCells = kNX * kNY * kNZ;         // 845

    // ── Construction ──────────────────────────────────────────────────────────

    SpatialGrid() { cells_.resize(kNCells); }

    // ── Rebuild ───────────────────────────────────────────────────────────────

    /// Clear and repopulate from the current drone positions.  O(N).
    void rebuild(const std::vector<Drone>& drones)
    {
        for (auto& cell : cells_) cell.clear();

        for (const auto& d : drones) {
            if (!d.is_alive()) continue;
            if (const int idx = cell_index(d.pos); idx >= 0)
                cells_[static_cast<std::size_t>(idx)].push_back(d.id);
        }
    }

    // ── Spatial queries ───────────────────────────────────────────────────────

    /// Nearest living drone on `enemy_team` within `max_radius`.
    /// Returns {id, distance} or {npos, ∞} if none found.
    [[nodiscard]]
    std::pair<std::size_t, double>
    nearest_enemy(const Drone& d, Team enemy_team,
                  const std::vector<Drone>& all,
                  double max_radius) const noexcept
    {
        std::size_t best_id   = std::size_t(-1);
        double      best_dist = std::numeric_limits<double>::infinity();

        visit_radius(d.pos, max_radius, [&](std::size_t id) noexcept {
            const auto& o = all[id];
            if (o.team != enemy_team || !o.is_alive()) return;
            const double dist = d.pos.dist(o.pos);
            if (dist < best_dist) { best_dist = dist; best_id = id; }
        });

        return {best_id, best_dist};
    }

    /// Count living allies (excluding `d`) within `radius`.
    [[nodiscard]]
    std::size_t count_allies(const Drone& d,
                             const std::vector<Drone>& all,
                             double radius) const noexcept
    {
        const double r2 = radius * radius;
        std::size_t  n  = 0;

        visit_radius(d.pos, radius, [&](std::size_t id) noexcept {
            const auto& o = all[id];
            if (o.id == d.id || o.team != d.team || !o.is_alive()) return;
            if (d.pos.dist_sq(o.pos) <= r2) ++n;
        });

        return n;
    }

    /// Nearest living enemy with no distance cap (for retreat movement).
    /// Returns {id, distance} or {npos, ∞}.
    [[nodiscard]]
    std::pair<std::size_t, double>
    nearest_enemy_any(const Drone& d, const std::vector<Drone>& all) const noexcept
    {
        // Expand search ring by ring until we find something.
        const Team enemy_team = (d.team == Team::Alpha) ? Team::Beta : Team::Alpha;
        const int  cx0 = clamp_cx(d.pos.x);
        const int  cy0 = clamp_cy(d.pos.y);
        const int  cz0 = clamp_cz(d.pos.z);

        std::size_t best_id   = std::size_t(-1);
        double      best_dist = std::numeric_limits<double>::infinity();

        for (int ring = 0; ring <= std::max({kNX, kNY, kNZ}); ++ring) {
            visit_ring(cx0, cy0, cz0, ring, [&](std::size_t id) noexcept {
                const auto& o = all[id];
                if (o.team != enemy_team || !o.is_alive()) return;
                const double dist = d.pos.dist(o.pos);
                if (dist < best_dist) { best_dist = dist; best_id = id; }
            });
            // Once we've found something and expanded one extra ring, stop.
            if (best_id != std::size_t(-1) && ring > 0) break;
        }
        return {best_id, best_dist};
    }

private:
    // ── Storage ───────────────────────────────────────────────────────────────

    std::vector<std::vector<std::size_t>> cells_;

    // ── Index arithmetic ──────────────────────────────────────────────────────

    [[nodiscard]] static int clamp_cx(double x) noexcept {
        return std::clamp(static_cast<int>((x - kMinX) / kCellSize), 0, kNX - 1);
    }
    [[nodiscard]] static int clamp_cy(double y) noexcept {
        return std::clamp(static_cast<int>((y - kMinY) / kCellSize), 0, kNY - 1);
    }
    [[nodiscard]] static int clamp_cz(double z) noexcept {
        return std::clamp(static_cast<int>((z - kMinZ) / kCellSize), 0, kNZ - 1);
    }

    [[nodiscard]] int cell_index(Vec3 pos) const noexcept {
        const int cx = static_cast<int>((pos.x - kMinX) / kCellSize);
        const int cy = static_cast<int>((pos.y - kMinY) / kCellSize);
        const int cz = static_cast<int>((pos.z - kMinZ) / kCellSize);
        if (cx < 0 || cx >= kNX || cy < 0 || cy >= kNY || cz < 0 || cz >= kNZ)
            return -1;
        return (cx * kNY + cy) * kNZ + cz;
    }

    [[nodiscard]] const std::vector<std::size_t>&
    cell(int cx, int cy, int cz) const noexcept {
        return cells_[static_cast<std::size_t>((cx * kNY + cy) * kNZ + cz)];
    }

    // ── Visitors ──────────────────────────────────────────────────────────────

    /// Call fn(drone_id) for every drone in cells overlapping a sphere.
    template<typename F>
    void visit_radius(Vec3 pos, double radius, F&& fn) const noexcept
    {
        const int r_cells = static_cast<int>(radius / kCellSize) + 1;
        const int cx0 = clamp_cx(pos.x);
        const int cy0 = clamp_cy(pos.y);
        const int cz0 = clamp_cz(pos.z);

        for (int cx = std::max(0, cx0 - r_cells);
             cx <= std::min(kNX - 1, cx0 + r_cells); ++cx)
        for (int cy = std::max(0, cy0 - r_cells);
             cy <= std::min(kNY - 1, cy0 + r_cells); ++cy)
        for (int cz = std::max(0, cz0 - r_cells);
             cz <= std::min(kNZ - 1, cz0 + r_cells); ++cz)
            for (std::size_t id : cell(cx, cy, cz))
                fn(id);
    }

    /// Call fn(drone_id) for all drones exactly `ring` cells away (Manhattan).
    template<typename F>
    void visit_ring(int cx0, int cy0, int cz0, int ring, F&& fn) const noexcept
    {
        const int x0 = std::max(0, cx0 - ring);
        const int x1 = std::min(kNX - 1, cx0 + ring);
        const int y0 = std::max(0, cy0 - ring);
        const int y1 = std::min(kNY - 1, cy0 + ring);
        const int z0 = std::max(0, cz0 - ring);
        const int z1 = std::min(kNZ - 1, cz0 + ring);

        for (int cx = x0; cx <= x1; ++cx)
        for (int cy = y0; cy <= y1; ++cy)
        for (int cz = z0; cz <= z1; ++cz) {
            // Only visit cells on the surface of the ring cube
            if (std::abs(cx - cx0) != ring
             && std::abs(cy - cy0) != ring
             && std::abs(cz - cz0) != ring) continue;
            for (std::size_t id : cell(cx, cy, cz))
                fn(id);
        }
    }
};
