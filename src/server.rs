/// WebSocket hub using axum + tokio::sync::broadcast.
///
/// # Architecture note
/// The simulation runs in its own dedicated tokio task and **owns** the
/// `Simulation` — no locking needed on the hot path.  It publishes JSON
/// snapshots via a `broadcast::Sender<String>`.  Each WebSocket connection
/// subscribes with `tx.subscribe()` and forwards messages to its client.
///
/// This means the simulation task never blocks on slow clients; lagging
/// receivers simply miss frames (they call `recv()` which returns
/// `RecvError::Lagged`), which is the correct behaviour for a real-time sim.
use std::{net::SocketAddr, sync::Arc, time::Duration};

use axum::{
    extract::{
        ws::{Message, WebSocket, WebSocketUpgrade},
        State,
    },
    response::IntoResponse,
    routing::get,
    Router,
};
use tokio::sync::broadcast;
use tower_http::services::ServeDir;
use tracing::{info, warn};

use crate::sim::simulation::Simulation;

const BROADCAST_CAPACITY: usize = 4; // keep only the latest few snapshots

// ── Shared state ──────────────────────────────────────────────────────────────

#[derive(Clone)]
pub struct AppState {
    /// Clients subscribe to this to receive world snapshots.
    pub tx: broadcast::Sender<String>,
}

// ── Entry point ───────────────────────────────────────────────────────────────

pub async fn run(addr: SocketAddr, drones_per_team: usize) {
    let (tx, _rx) = broadcast::channel::<String>(BROADCAST_CAPACITY);

    // Spawn the simulation loop — it owns `Simulation`, no Arc<Mutex> needed.
    let tx_sim = tx.clone();
    tokio::spawn(async move {
        sim_loop(tx_sim, drones_per_team).await;
    });

    let state = AppState { tx };

    let app = Router::new()
        .route("/ws", get(ws_handler))
        // Serve compiled frontend from ./frontend/dist
        .nest_service("/", ServeDir::new("frontend/dist"))
        .with_state(state);

    info!("🚁  Drone sim listening on http://{addr}");

    let listener = tokio::net::TcpListener::bind(addr).await.unwrap();
    axum::serve(listener, app.into_make_service_with_connect_info::<SocketAddr>())
        .await
        .unwrap();
}

// ── Simulation loop ───────────────────────────────────────────────────────────

async fn sim_loop(tx: broadcast::Sender<String>, drones_per_team: usize) {
    let mut interval = tokio::time::interval(Duration::from_millis(50)); // 20 TPS
    let mut sim      = Simulation::new(drones_per_team);

    loop {
        interval.tick().await;
        sim.step();

        // Only serialise if anyone is listening — avoids work with zero clients.
        if tx.receiver_count() > 0 {
            let snap = sim.snapshot_json();
            // Ignore SendError (no receivers) — that's fine.
            let _ = tx.send(snap);
        }
    }
}

// ── WebSocket handler ─────────────────────────────────────────────────────────

async fn ws_handler(
    ws:    WebSocketUpgrade,
    State(state): State<AppState>,
) -> impl IntoResponse {
    ws.on_upgrade(|socket| handle_socket(socket, state))
}

async fn handle_socket(mut socket: WebSocket, state: AppState) {
    let mut rx = state.tx.subscribe();

    // Immediately drain any stale snapshot so the client sees data on connect.
    // (The sim loop may have ticked since the last broadcast.)
    info!("WebSocket client connected");

    loop {
        match rx.recv().await {
            Ok(snap) => {
                if socket.send(Message::Text(snap.into())).await.is_err() {
                    // Client disconnected — exit cleanly.
                    break;
                }
            }
            Err(broadcast::error::RecvError::Lagged(n)) => {
                // Client is too slow; it missed `n` frames — just continue.
                warn!("WS client lagged by {n} frames");
            }
            Err(broadcast::error::RecvError::Closed) => break,
        }
    }

    info!("WebSocket client disconnected");
}
