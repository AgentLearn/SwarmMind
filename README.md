# SwarmMind
Platform for testing distribute intelligences.

## Initial Version: Dumb Swarms Arial Compat

Two opposing drone swarms fight in 3D. Each drone runs a **decision tree**
every tick. The server is built with C++23, websocketpp, and standalone Asio.
The frontend is identical to the Go and Rust versions (same JSON wire format).


### 1. Build the C++ server

```bash
cd dronesim-cpp

# Configure (downloads nlohmann/json, asio, websocketpp automatically)
cmake --preset release

# Build
cmake --build --preset release

# The binary is at:  build/release/dronesim
```

### 2. Build the frontend

```bash
cd frontend
npm install
npm run build    # → frontend/dist/
cd ..
```

### 3. Run

```bash
./build/release/dronesim
# With options:
./build/release/dronesim --port 8080 --drones 20
```

Open **http://localhost:8080**

---

## Development mode (hot-reload frontend)

```bash
# Terminal 1
./build/debug/dronesim

# Terminal 2
cd frontend && npm run dev    # → http://localhost:5173, /ws proxied to :8080
```

---

## CLI Flags

| Flag       | Short | Default | Description              |
|------------|-------|---------|--------------------------|
| `--port`   | `-p`  | `8080`  | HTTP / WebSocket port    |
| `--drones` | `-d`  | `15`    | Drones per team (total 2×) |
| `--help`   | `-h`  |         | Print usage              |

---

