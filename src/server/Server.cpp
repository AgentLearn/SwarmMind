#include "Server.hpp"

// ── websocketpp configuration ─────────────────────────────────────────────────
// Must be defined before including websocketpp headers.
#define ASIO_STANDALONE
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>

using WsServer   = websocketpp::server<websocketpp::config::asio>;
using ConnHandle = websocketpp::connection_hdl;
namespace fs     = std::filesystem;

// ── MIME types ────────────────────────────────────────────────────────────────

namespace {

const std::unordered_map<std::string, std::string> kMimeTypes{
    {".html", "text/html"},
    {".js",   "application/javascript"},
    {".mjs",  "application/javascript"},
    {".css",  "text/css"},
    {".svg",  "image/svg+xml"},
    {".ico",  "image/x-icon"},
    {".png",  "image/png"},
    {".woff2","font/woff2"},
};

std::string mime_for(const fs::path& p) {
    const std::string ext = p.extension().string();
    const auto it = kMimeTypes.find(ext);
    return (it != kMimeTypes.end()) ? it->second : "application/octet-stream";
}

std::string read_file(const fs::path& p) {
    std::ifstream f{p, std::ios::binary};
    return {std::istreambuf_iterator<char>{f}, {}};
}

std::string http_response(int code,
                           const std::string& reason,
                           const std::string& content_type,
                           const std::string& body)
{
    return std::format(
        "HTTP/1.1 {} {}\r\n"
        "Content-Type: {}\r\n"
        "Content-Length: {}\r\n"
        "Connection: keep-alive\r\n"
        "\r\n"
        "{}",
        code, reason, content_type, body.size(), body);
}

} // anonymous namespace

// ── Server::Impl ─────────────────────────────────────────────────────────────

struct Server::Impl {
    Simulation& sim;
    WsServer    ws;
    std::mutex  conn_mu;
    std::set<ConnHandle, std::owner_less<ConnHandle>> connections;
    std::jthread sim_thread;      // RAII — joins in destructor
    fs::path     frontend_root{"frontend/dist"};

    explicit Impl(Simulation& s) : sim{s} {}

    // ── Broadcast a string to all connected WebSocket clients ────────────────

    void broadcast(const std::string& msg) {
        std::scoped_lock lock{conn_mu};
        for (const auto& hdl : connections) {
            try {
                ws.send(hdl, msg, websocketpp::frame::opcode::text);
            } catch (const std::exception& e) {
                // Stale handle — will be cleaned up in on_close
                std::cerr << std::format("[ws] send error: {}\n", e.what());
            }
        }
    }

    // ── Serve a static file for an HTTP (non-WS) request ────────────────────

    void serve_static(ConnHandle hdl, const std::string& uri) {
        // Map "/" → "/index.html"
        std::string path_str = uri;
        if (path_str == "/" || path_str.empty()) path_str = "/index.html";

        const fs::path target = frontend_root / path_str.substr(1);

        std::error_code ec;
        if (!fs::exists(target, ec) || fs::is_directory(target, ec)) {
            // Try index.html for SPA fallback
            const fs::path fallback = frontend_root / "index.html";
            if (!fs::exists(fallback, ec)) {
                auto conn = ws.get_con_from_hdl(hdl);
                conn->set_status(websocketpp::http::status_code::not_found);
                conn->set_body("404 Not Found");
                return;
            }
            auto conn = ws.get_con_from_hdl(hdl);
            conn->set_status(websocketpp::http::status_code::ok);
            conn->append_header("Content-Type", "text/html");
            conn->set_body(read_file(fallback));
            return;
        }

        auto conn = ws.get_con_from_hdl(hdl);
        conn->set_status(websocketpp::http::status_code::ok);
        conn->append_header("Content-Type", mime_for(target));
        conn->set_body(read_file(target));
    }

    // ── Simulation loop (runs on sim_thread) ─────────────────────────────────

    void sim_loop(std::stop_token stop) {
        using namespace std::chrono;
        constexpr auto kTickInterval = duration_cast<nanoseconds>(50ms); // 20 TPS

        auto next_tick = steady_clock::now() + kTickInterval;
        while (!stop.stop_requested()) {
            std::this_thread::sleep_until(next_tick);
            next_tick += kTickInterval;

            sim.step();
            broadcast(sim.snapshot_json());
        }
    }
};

// ── Server public interface ───────────────────────────────────────────────────

Server::Server(Simulation& sim)
    : impl_{std::make_unique<Impl>(sim)}
{}

Server::~Server() = default;  // unique_ptr destructor joins jthread

void Server::run(std::uint16_t port)
{
    auto& ws = impl_->ws;

    // Suppress websocketpp's default stdout logging
    ws.clear_access_channels(websocketpp::log::alevel::all);
    ws.clear_error_channels(websocketpp::log::elevel::all);

    ws.init_asio();
    ws.set_reuse_addr(true);

    // ── WebSocket callbacks ────────────────────────────────────────────────

    ws.set_open_handler([this](ConnHandle hdl) {
        std::scoped_lock lock{impl_->conn_mu};
        impl_->connections.insert(hdl);
        std::cout << std::format("[ws] client connected  (total: {})\n",
                                  impl_->connections.size());
        // Send current snapshot immediately so the client isn't blank.
        try {
            impl_->ws.send(hdl, impl_->sim.snapshot_json(),
                           websocketpp::frame::opcode::text);
        } catch (...) {}
    });

    ws.set_close_handler([this](ConnHandle hdl) {
        std::scoped_lock lock{impl_->conn_mu};
        impl_->connections.erase(hdl);
        std::cout << std::format("[ws] client disconnected (total: {})\n",
                                  impl_->connections.size());
    });

    ws.set_fail_handler([this](ConnHandle hdl) {
        std::scoped_lock lock{impl_->conn_mu};
        impl_->connections.erase(hdl);
    });

    // ── HTTP handler (serves frontend static files) ────────────────────────

    ws.set_http_handler([this](ConnHandle hdl) {
        auto conn = impl_->ws.get_con_from_hdl(hdl);
        impl_->serve_static(hdl, conn->get_resource());
    });

    // ── Listen ────────────────────────────────────────────────────────────

    ws.listen(port);
    ws.start_accept();

    // ── Start simulation loop on its own jthread ───────────────────────────

    impl_->sim_thread = std::jthread{
        [this](std::stop_token st) { impl_->sim_loop(st); }
    };

    std::cout << std::format(
        "🚁  Drone sim server listening on http://localhost:{}\n", port);

    ws.run();  // blocks until ws.stop() is called
}
