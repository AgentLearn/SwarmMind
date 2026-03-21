import * as THREE from 'three'
import { OrbitControls } from 'three/addons/controls/OrbitControls.js'

// ── Types (mirror Go's WorldSnap / DroneSnap) ─────────────────────────────────

interface DroneSnap {
  id: number
  team: number   // 0 = Alpha, 1 = Beta
  x: number; y: number; z: number
  vx: number; vy: number; vz: number
  health: number
  maxHp: number
  state: 'patrol' | 'attack' | 'flank' | 'retreat' | 'dead'
}

interface WorldSnap {
  tick: number
  phase: 'running' | 'resetting'
  alphaAlive: number
  betaAlive: number
  resetIn: number
  drones: DroneSnap[]
}

// ── Scene setup ───────────────────────────────────────────────────────────────

const scene = new THREE.Scene()
scene.background = new THREE.Color(0x060612)
scene.fog = new THREE.FogExp2(0x060612, 0.0028)

const camera = new THREE.PerspectiveCamera(55, innerWidth / innerHeight, 0.5, 1200)
camera.position.set(0, 160, 260)
camera.lookAt(0, 30, 0)

const renderer = new THREE.WebGLRenderer({ antialias: true })
renderer.setPixelRatio(devicePixelRatio)
renderer.setSize(innerWidth, innerHeight)
renderer.shadowMap.enabled = true
renderer.shadowMap.type = THREE.PCFSoftShadowMap
document.body.appendChild(renderer.domElement)

const controls = new OrbitControls(camera, renderer.domElement)
controls.enableDamping = true
controls.dampingFactor = 0.06
controls.minDistance = 40
controls.maxDistance = 600
controls.target.set(0, 30, 0)

window.addEventListener('resize', () => {
  camera.aspect = innerWidth / innerHeight
  camera.updateProjectionMatrix()
  renderer.setSize(innerWidth, innerHeight)
})

// ── Lighting ──────────────────────────────────────────────────────────────────

scene.add(new THREE.AmbientLight(0x223355, 1.8))

const sun = new THREE.DirectionalLight(0xffffff, 2.0)
sun.position.set(80, 200, 120)
sun.castShadow = true
sun.shadow.mapSize.set(2048, 2048)
sun.shadow.camera.near = 1
sun.shadow.camera.far = 600
sun.shadow.camera.left = sun.shadow.camera.bottom = -200
sun.shadow.camera.right = sun.shadow.camera.top = 200
scene.add(sun)

const fill = new THREE.DirectionalLight(0x334466, 0.6)
fill.position.set(-80, 60, -120)
scene.add(fill)

// ── Ground & Grid ─────────────────────────────────────────────────────────────

const groundGeo = new THREE.PlaneGeometry(300, 300)
const groundMat = new THREE.MeshStandardMaterial({ color: 0x0a0e1a, roughness: 1 })
const ground = new THREE.Mesh(groundGeo, groundMat)
ground.rotation.x = -Math.PI / 2
ground.receiveShadow = true
scene.add(ground)

scene.add(new THREE.GridHelper(300, 30, 0x111833, 0x0c1225))

// Boundary wireframe box
{
  const geo = new THREE.BoxGeometry(300, 100, 300)
  const edges = new THREE.EdgesGeometry(geo)
  const mat = new THREE.LineBasicMaterial({ color: 0x1a2a44, transparent: true, opacity: 0.4 })
  const box = new THREE.LineSegments(edges, mat)
  box.position.y = 50
  scene.add(box)
}

// ── Drone geometry ─────────────────────────────────────────────────────────────
// Body: tapered octahedron-ish. We use a ConeGeometry facing forward (+Z).

const bodyGeo = new THREE.ConeGeometry(1.4, 4.5, 8)
bodyGeo.rotateX(Math.PI / 2)  // nose points forward (+Z)

// Small rotor arms
const rotorGeo = new THREE.BoxGeometry(6, 0.3, 0.4)

// Team base colors
const TEAM_COLOR = [0x3366ff, 0xff3333] as const

// State emissive tint (multiplied on top of team color)
const STATE_EMISSIVE: Record<string, THREE.Color> = {
  attack:  new THREE.Color(0xff4400),
  flank:   new THREE.Color(0x00ddaa),
  retreat: new THREE.Color(0xffdd00),
  patrol:  new THREE.Color(0x000000),
  dead:    new THREE.Color(0x000000),
}

// ── Drone visual objects ───────────────────────────────────────────────────────

interface DroneVisual {
  root:    THREE.Group
  body:    THREE.Mesh
  rotors:  THREE.Mesh[]
  hpBarFg: THREE.Mesh  // health bar foreground
  light:   THREE.PointLight
}

const droneVisuals = new Map<number, DroneVisual>()

function makeDroneVisual(team: number): DroneVisual {
  const root = new THREE.Group()

  // Body
  const bodyMat = new THREE.MeshStandardMaterial({
    color: TEAM_COLOR[team] ?? 0xffffff,
    emissive: new THREE.Color(0x000000),
    metalness: 0.6,
    roughness: 0.3,
  })
  const body = new THREE.Mesh(bodyGeo, bodyMat)
  body.castShadow = true
  root.add(body)

  // 4 rotor arms
  const rotors: THREE.Mesh[] = []
  const rotorMat = new THREE.MeshStandardMaterial({ color: 0x334455, metalness: 0.8, roughness: 0.2 })
  const armAngles = [0, 90, 180, 270]
  for (const angle of armAngles) {
    const arm = new THREE.Mesh(rotorGeo, rotorMat)
    arm.rotation.y = THREE.MathUtils.degToRad(angle + 45)
    arm.position.y = 0.3
    root.add(arm)
    rotors.push(arm)
  }

  // Health bar: background + foreground
  const hpBgMesh = new THREE.Mesh(
    new THREE.PlaneGeometry(4.5, 0.55),
    new THREE.MeshBasicMaterial({ color: 0x111111, side: THREE.DoubleSide, transparent: true, opacity: 0.7 })
  )
  hpBgMesh.position.y = 5
  root.add(hpBgMesh)

  const hpFgMat = new THREE.MeshBasicMaterial({ color: 0x00ff66, side: THREE.DoubleSide })
  const hpFgMesh = new THREE.Mesh(new THREE.PlaneGeometry(4.5, 0.55), hpFgMat)
  hpFgMesh.position.y = 5
  hpFgMesh.position.z = 0.01
  root.add(hpFgMesh)

  // Soft point light — dims as health falls
  const lightColor = team === 0 ? 0x4466ff : 0xff4422
  const light = new THREE.PointLight(lightColor, 1.5, 22)
  light.position.y = 1
  root.add(light)

  scene.add(root)
  return { root, body, rotors, hpBarFg: hpFgMesh, light }
}

function getOrCreateVisual(id: number, team: number): DroneVisual {
  let v = droneVisuals.get(id)
  if (!v) {
    v = makeDroneVisual(team)
    droneVisuals.set(id, v)
  }
  return v
}

// ── Update drones from snapshot ───────────────────────────────────────────────

const _dir = new THREE.Vector3()
const _up  = new THREE.Vector3(0, 1, 0)
const _q   = new THREE.Quaternion()

function applySnap(snap: DroneSnap): void {
  const v = getOrCreateVisual(snap.id, snap.team)
  const hpPct = snap.health / snap.maxHp

  // Position
  v.root.position.set(snap.x, snap.y, snap.z)

  // Orient nose toward velocity
  const speed = Math.sqrt(snap.vx ** 2 + snap.vy ** 2 + snap.vz ** 2)
  if (speed > 0.5) {
    _dir.set(snap.vx, snap.vy, snap.vz).normalize()
    _q.setFromUnitVectors(new THREE.Vector3(0, 0, 1), _dir)
    v.root.quaternion.slerp(_q, 0.12)
  }

  // Health bar: scale X, recolour
  if (snap.state === 'dead') {
    v.hpBarFg.visible = false
  } else {
    v.hpBarFg.visible = true;
    (v.hpBarFg.material as THREE.MeshBasicMaterial).color.setHSL(hpPct * 0.33, 1.0, 0.5)
    v.hpBarFg.scale.x = Math.max(0.01, hpPct)
    // Billboard toward camera
    v.hpBarFg.lookAt(camera.position)
    v.root.children
      .filter(c => c !== v.hpBarFg && c.position.y === 5)
      .forEach(c => (c as THREE.Mesh).lookAt(camera.position))
  }

  // Emissive glow by state
  const emissive = STATE_EMISSIVE[snap.state] ?? STATE_EMISSIVE.patrol;
  (v.body.material as THREE.MeshStandardMaterial).emissive.copy(emissive).multiplyScalar(0.45)

  // Dead drones: fade out
  if (snap.state === 'dead') {
    v.root.visible = false  // hide entirely
    v.light.intensity = 0
  } else {
    v.root.visible = true
    v.root.scale.setScalar(0.7 + 0.3 * hpPct)
    v.light.intensity = 0.5 + 1.2 * hpPct
  }
}

// ── HUD elements ──────────────────────────────────────────────────────────────

const tickEl         = document.getElementById('tick')!
const alphaCountEl   = document.getElementById('alpha-count')!
const betaCountEl    = document.getElementById('beta-count')!
const alphaAliveBar  = document.getElementById('alpha-alive-bar') as HTMLElement
const betaAliveBar   = document.getElementById('beta-alive-bar')  as HTMLElement
const alphaHpBar     = document.getElementById('alpha-hp-bar')    as HTMLElement
const betaHpBar      = document.getElementById('beta-hp-bar')     as HTMLElement
const connStatus     = document.getElementById('conn-status')!
const phaseBanner    = document.getElementById('phase-banner')!

let totalAlpha = 0  // set on first snap

function updateHUD(snap: WorldSnap): void {
  tickEl.textContent = snap.tick.toString()

  // Derive initial totals from first snapshot
  if (totalAlpha === 0 && snap.alphaAlive > 0) totalAlpha = snap.alphaAlive

  alphaCountEl.textContent = snap.alphaAlive.toString()
  betaCountEl.textContent  = snap.betaAlive.toString()

  const alivePct = totalAlpha > 0 ? snap.alphaAlive / totalAlpha : 1
  const blivePct = totalAlpha > 0 ? snap.betaAlive  / totalAlpha : 1
  alphaAliveBar.style.width = `${alivePct * 100}%`
  betaAliveBar.style.width  = `${blivePct * 100}%`

  // Average health bars
  const alphaDrones = snap.drones.filter(d => d.team === 0 && d.state !== 'dead')
  const betaDrones  = snap.drones.filter(d => d.team === 1 && d.state !== 'dead')

  const avgHP = (arr: DroneSnap[]) =>
    arr.length ? arr.reduce((s, d) => s + d.health / d.maxHp, 0) / arr.length : 0

  alphaHpBar.style.width = `${avgHP(alphaDrones) * 100}%`
  betaHpBar.style.width  = `${avgHP(betaDrones)  * 100}%`

  // Phase banner
  if (snap.phase === 'resetting') {
    const winner = snap.alphaAlive > 0 ? '▲ ALPHA WINS' : snap.betaAlive > 0 ? '▼ BETA WINS' : 'DRAW'
    phaseBanner.style.display = 'block'
    phaseBanner.innerHTML = `${winner}<br><span style="font-size:12px;color:#998;letter-spacing:2px">RESETTING IN ${snap.resetIn.toFixed(1)}s</span>`
    // Reset totals so next battle recalibrates bars
    totalAlpha = 0
  } else {
    phaseBanner.style.display = 'none'
  }
}

// ── WebSocket ─────────────────────────────────────────────────────────────────

function connect(): void {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws'
  const ws = new WebSocket(`${proto}://${location.host}/ws`)

  ws.onopen = () => {
    connStatus.textContent = 'ONLINE'
    connStatus.style.color = '#44ff88'
  }

  ws.onmessage = ({ data }) => {
    const snap: WorldSnap = JSON.parse(data as string)
    snap.drones.forEach(applySnap)
    updateHUD(snap)
  }

  ws.onclose = () => {
    connStatus.textContent = 'RECONNECTING'
    connStatus.style.color = '#ff8833'
    setTimeout(connect, 2000)
  }

  ws.onerror = () => ws.close()
}

connect()

// ── Render loop ───────────────────────────────────────────────────────────────

let t = 0
function animate(now: number): void {
  requestAnimationFrame(animate)
  t = now * 0.001
  controls.update()

  // Spin rotors on living drones
  droneVisuals.forEach(v => {
    if (!v.root.visible) return
    v.rotors.forEach((arm, i) => {
      arm.rotation.y += (i % 2 === 0 ? 0.18 : -0.18)
    })
  })

  renderer.render(scene, camera)
}

requestAnimationFrame(animate)
