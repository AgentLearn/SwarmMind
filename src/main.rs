mod sim;
mod server;

use clap::Parser;
use std::net::SocketAddr;

/// 🚁 Drone Swarm Simulation — Go-equivalent starter in Rust
#[derive(Parser, Debug)]
#[command(author, version, about)]
struct Args {
    /// HTTP / WebSocket listen address
    #[arg(long, default_value = "0.0.0.0:8080")]
    addr: SocketAddr,

    /// Number of drones per team (total = 2×)
    #[arg(long, default_value_t = 15)]
    drones: usize,
}

#[tokio::main]
async fn main() {
    // Initialise tracing — RUST_LOG=info (default) or RUST_LOG=debug
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "dronesim=info,tower_http=info".into()),
        )
        .init();

    let args = Args::parse();

    tracing::info!("🚁  Starting drone swarm simulation");
    tracing::info!("    Drones per team : {}", args.drones);
    tracing::info!("    Address         : http://localhost:{}", args.addr.port());

    server::run(args.addr, args.drones).await;
}
