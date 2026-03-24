# SwarmMind
Platform for testing distribute intelligences.

## Initial Version: Dumb Swarms Arial Compat

Two opposing drone swarms fight in 3D. Each drone runs a **decision tree**
every tick. The server is built with C++23, websockets, and streams 
world state over WebSockets to a Three.js frontend.


### 1. Build the C++ server

```bash
cd SwarmMind-cpp

# Configure (downloads glaze, asio, websocketpp automatically)
cmake --preset release

# Build
cmake --build --preset release

# The binary is at:  build/release/SwarmMind
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
./build/release/SwarmMind
# With options:
./build/release/SwarmMind --port 8080 --drones 20
```

Open **http://localhost:8080**

---

## Development mode (hot-reload frontend)

```bash
# Terminal 1
./build/debug/SwarmMind

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

