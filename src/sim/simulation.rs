use rand::Rng;
use serde::Serialize;

use super::decision_tree::{self, ATTACK_RANGE, DAMAGE_PER_TICK};
use super::drone::{Drone, DroneState, Team};
use super::vec3::Vec3;

// ── Constants ─────────────────────────────────────────────────────────────────

const TICK_DT:        f64 = 0.05;   // seconds per tick → 20 TPS
const MAX_SPEED:      f64 = 9.0;    // max drone speed  (units/s)
const PATROL_SPEED:   f64 = 4.5;    // patrol cruise speed
const BOUNDS_HALF:    f64 = 150.0;  // world ±X and ±Z
const BOUNDS_H_MAX:   f64 = 100.0;  // world max Y
const STEER_FACTOR:   f64 = 0.12;   // velocity smoothing (0..1)
const RESET_DELAY:    f64 = 5.0;    // seconds before respawn after one side is wiped

// ── Phase ─────────────────────────────────────────────────────────────────────

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum Phase { Running, Resetting }

// ── Simulation ────────────────────────────────────────────────────────────────

pub struct Simulation {
    pub drones:          Vec<Drone>,
    pub tick:            u64,
    pub phase:           Phase,
    pub drones_per_team: usize,
    reset_countdown:     f64,
}

impl Simulation {
    pub fn new(drones_per_team: usize) -> Self {
        let mut sim = Self {
            drones: Vec::new(),
            tick: 0,
            phase: Phase::Running,
            drones_per_team,
            reset_countdown: 0.0,
        };
        sim.spawn_drones();
        sim
    }

    // ── Tick ──────────────────────────────────────────────────────────────────

    pub fn step(&mut self) {
        self.tick += 1;

        // Check for end-of-battle
        if self.phase == Phase::Running {
            let alpha_alive = self.alive_count(Team::Alpha);
            let beta_alive  = self.alive_count(Team::Beta);
            if alpha_alive == 0 || beta_alive == 0 {
                self.phase          = Phase::Resetting;
                self.reset_countdown = RESET_DELAY;
            }
        }

        if self.phase == Phase::Resetting {
            self.reset_countdown -= TICK_DT;
            if self.reset_countdown <= 0.0 {
                self.spawn_drones();
                self.phase = Phase::Running;
            }
            return;
        }

        // ── 1. Decision pass (pure read, borrow-checker-safe) ─────────────────
        let decisions = decision_tree::decide_all(&self.drones);

        // ── 2. Apply decisions (mutate state fields) ──────────────────────────
        decision_tree::apply_decisions(&mut self.drones, decisions);

        // ── 3. Movement ───────────────────────────────────────────────────────
        // We need to read enemy positions for retreat, so collect retreat
        // targets first, then apply movement.
        let retreat_targets: Vec<Option<Vec3>> = self.drones.iter().map(|d| {
            if d.state == DroneState::Retreat {
                self.drones.iter()
                    .filter(|o| o.team != d.team && o.is_alive())
                    .map(|o| (o.pos, d.pos.dist_sq(o.pos)))
                    .min_by(|a, b| a.1.partial_cmp(&b.1).unwrap())
                    .map(|(pos, _)| pos)
            } else {
                None
            }
        }).collect();

        let target_positions: Vec<Option<Vec3>> = self.drones.iter().map(|d| {
            d.target_idx.and_then(|idx| {
                self.drones.get(idx).filter(|t| t.is_alive()).map(|t| t.pos)
            })
        }).collect();

        let mut rng = rand::thread_rng();
        for (i, drone) in self.drones.iter_mut().enumerate() {
            if !drone.is_alive() { continue; }
            move_drone(drone, target_positions[i], retreat_targets[i], &mut rng);
        }

        // ── 4. Combat ─────────────────────────────────────────────────────────
        // Collect (attacker_idx, target_idx, damage) to avoid aliased &mut.
        let hits: Vec<(usize, usize, f64)> = self.drones.iter()
            .enumerate()
            .filter_map(|(i, d)| {
                if d.state != DroneState::Attack || !d.is_alive() { return None; }
                let tidx = d.target_idx?;
                let tpos = self.drones.get(tidx)?.pos;
                if d.pos.dist(tpos) <= ATTACK_RANGE {
                    Some((i, tidx, DAMAGE_PER_TICK))
                } else {
                    None
                }
            })
            .collect();

        for (_, tidx, dmg) in hits {
            if let Some(target) = self.drones.get_mut(tidx) {
                target.health = (target.health - dmg).max(0.0);
            }
        }
    }

    // ── Helpers ───────────────────────────────────────────────────────────────

    fn spawn_drones(&mut self) {
        self.drones.clear();
        let mut rng = rand::thread_rng();
        let mut id  = 0usize;

        for _ in 0..self.drones_per_team {
            let pos = Vec3::new(
                -120.0 + rng.gen::<f64>() * 40.0,
                10.0   + rng.gen::<f64>() * 50.0,
                -60.0  + rng.gen::<f64>() * 120.0,
            );
            self.drones.push(Drone::new(id, Team::Alpha, pos));
            id += 1;
        }
        for _ in 0..self.drones_per_team {
            let pos = Vec3::new(
                80.0  + rng.gen::<f64>() * 40.0,
                10.0  + rng.gen::<f64>() * 50.0,
                -60.0 + rng.gen::<f64>() * 120.0,
            );
            self.drones.push(Drone::new(id, Team::Beta, pos));
            id += 1;
        }
    }

    pub fn alive_count(&self, team: Team) -> usize {
        self.drones.iter()
            .filter(|d| d.team == team && d.is_alive())
            .count()
    }

    // ── Snapshot ──────────────────────────────────────────────────────────────

    pub fn snapshot_json(&self) -> String {
        let snap = WorldSnap {
            tick:        self.tick,
            phase:       self.phase,
            alpha_alive: self.alive_count(Team::Alpha),
            beta_alive:  self.alive_count(Team::Beta),
            reset_in:    self.reset_countdown.max(0.0),
            drones:      self.drones.iter().map(DroneSnap::from_drone).collect(),
        };
        serde_json::to_string(&snap).expect("serialisation should never fail")
    }
}

// ── Movement ──────────────────────────────────────────────────────────────────

fn move_drone(
    d:            &mut Drone,
    target_pos:   Option<Vec3>,
    retreat_from: Option<Vec3>,
    rng:          &mut impl Rng,
) {
    let desired = match d.state {
        DroneState::Attack => {
            match target_pos {
                Some(tp) => tp.sub(d.pos).norm().scale(MAX_SPEED),
                None     => Vec3::default(),
            }
        }

        DroneState::Flank => {
            match target_pos {
                Some(tp) => {
                    let to_target = tp.sub(d.pos).norm();
                    let flank     = d.flank_offset;
                    to_target.add(flank.scale(1.2)).norm().scale(MAX_SPEED)
                }
                None => Vec3::default(),
            }
        }

        DroneState::Retreat => {
            match retreat_from {
                Some(enemy_pos) => d.pos.sub(enemy_pos).norm().scale(MAX_SPEED * 1.1),
                None            => Vec3::default(),
            }
        }

        DroneState::Patrol => {
            if d.pos.dist(d.waypoint) < 6.0 {
                d.waypoint = random_waypoint(rng);
            }
            d.waypoint.sub(d.pos).norm().scale(PATROL_SPEED)
        }

        DroneState::Dead => return,
    };

    // Exponential velocity smoothing toward desired
    d.vel = d.vel.lerp(desired, STEER_FACTOR);

    // Speed clamp
    let spd = d.vel.len();
    if spd > MAX_SPEED { d.vel = d.vel.scale(MAX_SPEED / spd); }

    d.pos = d.pos.add(d.vel.scale(TICK_DT));
    clamp_pos(&mut d.pos);
}

fn clamp_pos(v: &mut Vec3) {
    v.x = v.x.clamp(-BOUNDS_HALF, BOUNDS_HALF);
    v.y = v.y.clamp(2.0, BOUNDS_H_MAX);
    v.z = v.z.clamp(-BOUNDS_HALF, BOUNDS_HALF);
}

fn random_waypoint(rng: &mut impl Rng) -> Vec3 {
    Vec3::new(
        -100.0 + rng.gen::<f64>() * 200.0,
        10.0   + rng.gen::<f64>() * 60.0,
        -100.0 + rng.gen::<f64>() * 200.0,
    )
}

// ── Wire format (matches Go's WorldSnap / DroneSnap) ─────────────────────────

#[derive(Serialize)]
struct WorldSnap {
    tick:        u64,
    phase:       Phase,
    #[serde(rename = "alphaAlive")]
    alpha_alive: usize,
    #[serde(rename = "betaAlive")]
    beta_alive:  usize,
    #[serde(rename = "resetIn")]
    reset_in:    f64,
    drones:      Vec<DroneSnap>,
}

#[derive(Serialize)]
struct DroneSnap {
    id:     usize,
    team:   u8,
    x:      f64, y: f64, z: f64,
    vx:     f64, vy: f64, vz: f64,
    health: f64,
    #[serde(rename = "maxHp")]
    max_hp: f64,
    state:  DroneState,
}

impl DroneSnap {
    fn from_drone(d: &Drone) -> Self {
        Self {
            id:     d.id,
            team:   d.team as u8,
            x:      d.pos.x, y: d.pos.y, z: d.pos.z,
            vx:     d.vel.x, vy: d.vel.y, vz: d.vel.z,
            health: d.health.max(0.0),
            max_hp: d.max_health,
            state:  d.state,
        }
    }
}
