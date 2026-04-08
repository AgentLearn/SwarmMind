use serde::Serialize;
use super::vec3::Vec3;

// ── Team ──────────────────────────────────────────────────────────────────────

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum Team {
    Alpha = 0,
    Beta  = 1,
}

// ── DroneState ────────────────────────────────────────────────────────────────

/// The behaviour mode selected by the decision tree each tick.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum DroneState {
    Patrol,
    Attack,
    Flank,
    Retreat,
    Dead,
}

// ── Drone ─────────────────────────────────────────────────────────────────────

pub struct Drone {
    pub id:           usize,
    pub team:         Team,
    pub pos:          Vec3,
    pub vel:          Vec3,
    pub health:       f64,
    pub max_health:   f64,
    pub state:        DroneState,

    /// Index into `Simulation::drones` of the current attack/flank target.
    pub target_idx:   Option<usize>,

    /// Destination used while patrolling.
    pub waypoint:     Vec3,

    /// Perpendicular arc direction assigned when flanking.
    pub flank_offset: Vec3,
}

impl Drone {
    pub fn new(id: usize, team: Team, pos: Vec3) -> Self {
        Self {
            id,
            team,
            pos,
            vel:          Vec3::default(),
            health:       100.0,
            max_health:   100.0,
            state:        DroneState::Patrol,
            target_idx:   None,
            waypoint:     pos,
            flank_offset: Vec3::default(),
        }
    }

    #[inline] pub fn is_alive(&self)    -> bool { self.health > 0.0 }
    #[inline] pub fn health_pct(&self)  -> f64  { self.health / self.max_health }
}
