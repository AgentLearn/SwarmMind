#include <charconv>
#include <cstdlib>
#include <format>
#include <iostream>
#include <print>
#include <span>
#include <string_view>

#include "sim/Simulation.hpp"
#include "server/Server.hpp"

// ── Argument parsing ──────────────────────────────────────────────────────────
//
// Using std::span<char*> over argv[] demonstrates C++23 range-over-argv idiom.
// std::from_chars is used instead of atoi/stoi for zero-allocation parsing.

struct Config {
    std::uint16_t port   {8080};
    std::size_t   drones {15};
};

[[nodiscard]] Config parse_args(std::span<char*> argv)
{
    Config cfg;

    for (std::size_t i = 1; i < argv.size(); ++i) {
        const std::string_view arg{argv[i]};
        const bool has_next = (i + 1 < argv.size());

        auto parse_uint = [&]<typename T>(T& out) {
            if (!has_next) return;
            const std::string_view val{argv[++i]};
            std::from_chars(val.data(), val.data() + val.size(), out);
        };

        if      (arg == "--port"   || arg == "-p") parse_uint(cfg.port);
        else if (arg == "--drones" || arg == "-d") parse_uint(cfg.drones);
        else if (arg == "--help"   || arg == "-h") {
            std::println("Usage: SwarmMind [--port N] [--drones N]");
            std::println("  --port   N  HTTP/WebSocket port     (default: 8080)");
            std::println("  --drones N  Drones per team (×2)    (default: 15)");
            std::exit(0);
        }
    }
    return cfg;
}

// ── Entry point ───────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    const Config cfg = parse_args(std::span{argv, static_cast<std::size_t>(argc)});

    std::println("🚁  Starting drone swarm simulation");
    std::println("    Drones per team : {}", cfg.drones);
    std::println("    Port            : {}", cfg.port);

    Simulation sim{cfg.drones};
    Server     server{sim};

    server.run(cfg.port);   // blocks
}
