#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include "sim/Simulation.hpp"

/// Async WebSocket server built on websocketpp + standalone Asio.
///
/// # Design
/// - The simulation runs on a dedicated `std::jthread` (RAII, joins on
///   destruction) and calls `broadcast()` after every tick.
/// - WebSocket connections are managed by websocketpp's `asio_transport`
///   policy on its own io_context thread.
/// - `broadcast()` posts a lambda onto the websocketpp io_context so the
///   send happens on the correct thread without any manual locking on the
///   connection set.
class Server {
public:
    explicit Server(Simulation& sim);
    ~Server();   // joins jthread, stops io_context

    /// Blocks the calling thread serving HTTP + WebSocket connections.
    void run(std::uint16_t port);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
