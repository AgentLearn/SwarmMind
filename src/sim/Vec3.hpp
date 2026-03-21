#pragma once
#include <cmath>
#include <concepts>
#include <format>
#include <string>

/// Immutable 3-component vector.  All operations are `constexpr` and operate
/// on value-copied temporaries — the compiler will inline and optimise them
/// away entirely in release builds.
struct Vec3 {
    double x{}, y{}, z{};

    constexpr Vec3() noexcept = default;
    constexpr Vec3(double x_, double y_, double z_) noexcept
        : x{x_}, y{y_}, z{z_} {}

    // ── Arithmetic ─────────────────────────────────────────────────────────

    [[nodiscard]] constexpr Vec3 operator+(Vec3 o) const noexcept {
        return {x + o.x, y + o.y, z + o.z};
    }
    [[nodiscard]] constexpr Vec3 operator-(Vec3 o) const noexcept {
        return {x - o.x, y - o.y, z - o.z};
    }
    [[nodiscard]] constexpr Vec3 operator*(double s) const noexcept {
        return {x * s, y * s, z * s};
    }

    // ── Spatial ────────────────────────────────────────────────────────────

    [[nodiscard]] constexpr double len_sq() const noexcept {
        return x*x + y*y + z*z;
    }
    [[nodiscard]] double len() const noexcept {
        return std::sqrt(len_sq());
    }

    [[nodiscard]] Vec3 norm() const noexcept {
        const double l = len();
        return (l < 1e-9) ? Vec3{} : (*this * (1.0 / l));
    }

    [[nodiscard]] double dist(Vec3 o)    const noexcept { return (*this - o).len();    }
    [[nodiscard]] constexpr double dist_sq(Vec3 o) const noexcept { return (*this - o).len_sq(); }

    /// Cross product — used to compute perpendicular flanking axes.
    [[nodiscard]] constexpr Vec3 cross(Vec3 o) const noexcept {
        return {
            y * o.z - z * o.y,
            z * o.x - x * o.z,
            x * o.y - y * o.x,
        };
    }

    /// Linear interpolation: `*this + (o - *this) * t`
    [[nodiscard]] constexpr Vec3 lerp(Vec3 o, double t) const noexcept {
        return *this + (o - *this) * t;
    }

    // ── Formatting (C++23 std::format support) ─────────────────────────────

    [[nodiscard]] std::string to_string() const {
        return std::format("({:.2f}, {:.2f}, {:.2f})", x, y, z);
    }
};

// Allow `2.0 * v` as well as `v * 2.0`
[[nodiscard]] inline constexpr Vec3 operator*(double s, Vec3 v) noexcept {
    return v * s;
}
