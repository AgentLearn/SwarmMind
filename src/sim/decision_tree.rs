/// Decision tree for drone AI.
///
/// # Rust design note
/// Because Rust's borrow checker prevents us from holding a `&mut Drone` while
/// also reading the rest of `&[Drone]`, we split the AI into **two phases**:
///
/// 1. **Decide** (read-only): iterate all drones, produce a `Vec<Decision>`.
/// 2. **Apply** (mutable):    iterate `drones_mut().zip(decisions)` and mutate.
///
/// This is not just a borrow-checker workaround — it's the cleanest possible
/// separation of concerns and happens to be cache-friendly too.
use super::drone::{Drone, DroneState, Team};
use super::vec3::Vec3;

// ── Tunable constants ─────────────────────────────────────────────────────────

pub const ATTACK_RANGE:     f64 = 25.0;   // metres — engage within this distance
pub const FLANK_RANGE:      f64 = 55.0;   // metres — consider flanking
pub const LOW_HEALTH_PCT:   f64 = 0.28;   // retreat threshold
pub const ALLY_HELP_RADIUS: f64 = 40.0;   // radius to count supporting allies
pub const DAMAGE_PER_TICK:  f64 = 0.7;    // HP removed per tick when in range

// ── Decision enum ─────────────────────────────────────────────────────────────

/// The output of the decision tree — what this drone will do this tick.
///
/// Using a rich enum lets the simulation `match` exhaustively and makes it
/// impossible to forget a case at compile time.
#[derive(Debug)]
pub enum Decision {
    /// Health critical — break off and flee.
    Retreat,
    /// Close in on `target_idx` and fire.
    Attack { target_idx: usize },
    /// Arc around `target_idx` using `offset` as perpendicular direction.
    Flank  { target_idx: usize, offset: Vec3 },
    /// No threat in range — move toward a patrol waypoint.
    Patrol,
    /// Drone is dead — do nothing.
    Dead,
}

// ── Public API ────────────────────────────────────────────────────────────────

/// Compute a `Decision` for every drone in the slice (pure, read-only).
///
/// Returns one `Decision` per drone in the same order as `drones`.
pub fn decide_all(drones: &[Drone]) -> Vec<Decision> {
    drones
        .iter()
        .map(|d| decide_one(d, drones))
        .collect()
}

/// Apply a pre-computed `Vec<Decision>` onto the drone slice (mutating).
pub fn apply_decisions(drones: &mut Vec<Drone>, decisions: Vec<Decision>) {
    for (drone, decision) in drones.iter_mut().zip(decisions) {
        apply_one(drone, decision);
    }
}

// ── Core decision tree ────────────────────────────────────────────────────────

fn decide_one(d: &Drone, all: &[Drone]) -> Decision {
    if !d.is_alive() {
        return Decision::Dead;
    }

    let (nearest_enemy, enemy_dist) = nearest_alive_enemy(d, all);
    let nearby_allies = count_allies_in_radius(d, all, ALLY_HELP_RADIUS);

    // The decision tree — evaluated top-down, first match wins.
    match nearest_enemy {
        // ── No enemies anywhere → patrol ─────────────────────────────────────
        None => Decision::Patrol,

        Some(enemy) => {
            let dist = enemy_dist;

            if d.health_pct() < LOW_HEALTH_PCT {
                // Critical health — always retreat regardless of enemies
                Decision::Retreat

            } else if dist <= ATTACK_RANGE {
                // Enemy right on top of us — attack
                Decision::Attack { target_idx: enemy.id }

            } else if dist <= FLANK_RANGE && nearby_allies >= 2 {
                // Enemy nearby AND we have backup — flank
                let to_enemy = enemy.pos.sub(d.pos).norm();
                let up       = Vec3::new(0.0, 1.0, 0.0);
                // Alternate flank side by drone parity so they spread out
                let perp = if d.id % 2 == 0 {
                    to_enemy.cross(up).norm()
                } else {
                    up.cross(to_enemy).norm()
                };
                Decision::Flank { target_idx: enemy.id, offset: perp }

            } else if dist <= FLANK_RANGE {
                // Outnumbered but still aggressive — solo charge
                Decision::Attack { target_idx: enemy.id }

            } else {
                Decision::Patrol
            }
        }
    }
}

fn apply_one(d: &mut Drone, decision: Decision) {
    match decision {
        Decision::Dead => {
            d.state      = DroneState::Dead;
            d.target_idx = None;
        }
        Decision::Retreat => {
            d.state      = DroneState::Retreat;
            d.target_idx = None;
        }
        Decision::Attack { target_idx } => {
            d.state      = DroneState::Attack;
            d.target_idx = Some(target_idx);
        }
        Decision::Flank { target_idx, offset } => {
            d.state        = DroneState::Flank;
            d.target_idx   = Some(target_idx);
            d.flank_offset = offset;
        }
        Decision::Patrol => {
            d.state      = DroneState::Patrol;
            d.target_idx = None;
        }
    }
}

// ── Helpers ───────────────────────────────────────────────────────────────────

/// Returns the nearest living enemy and its distance, or `(None, ∞)`.
fn nearest_alive_enemy<'a>(d: &Drone, all: &'a [Drone]) -> (Option<&'a Drone>, f64) {
    all.iter()
        .filter(|o| o.team != d.team && o.is_alive())
        .fold((None, f64::INFINITY), |(best, best_dist), o| {
            let dist = d.pos.dist(o.pos);
            if dist < best_dist { (Some(o), dist) } else { (best, best_dist) }
        })
}

/// Counts living allies (excluding `d`) within `radius`.
fn count_allies_in_radius(d: &Drone, all: &[Drone], radius: f64) -> usize {
    let r2 = radius * radius;
    all.iter()
        .filter(|o| o.id != d.id && o.team == d.team && o.is_alive())
        .filter(|o| d.pos.dist_sq(o.pos) <= r2)
        .count()
}
