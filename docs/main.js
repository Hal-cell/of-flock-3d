// =============================================================
// Flock 3D — Web version
// 移植自 openFrameworks 项目 (https://github.com/Hal-cell/of-flock-3d)
// 简化：3 个 field + cohesion + merge + cluster drone + event 音
// 自动适配手机/电脑（粒子数 + 控件大小 + 触控/鼠标）
// =============================================================

import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import GUI from 'https://unpkg.com/lil-gui@0.19/dist/lil-gui.esm.min.js';

// ─── 1. Device detection 自动配置 ─────────────────────────────
const isMobile = window.innerWidth < 768 ||
                 /Android|webOS|iPhone|iPad|iPod|BlackBerry|Mobile/i.test(navigator.userAgent);

const N_PARTICLES = isMobile ? 4000 : 12000;   // 手机 4K，桌面 12K
const WORLD_RADIUS = 250;
const TRAIL_MAX = 0;                            // 暂不渲染 trail（性能 + 简化）

console.log(`[Flock3D] device=${isMobile ? 'mobile' : 'desktop'}, particles=${N_PARTICLES}`);

// ─── 2. Three.js 场景 ────────────────────────────────────────
const scene = new THREE.Scene();
scene.background = new THREE.Color(0x08080c);

const camera = new THREE.PerspectiveCamera(60, window.innerWidth / window.innerHeight, 1, 5000);
camera.position.set(700, -400, 700);
camera.lookAt(0, 0, 0);

const renderer = new THREE.WebGLRenderer({ antialias: !isMobile, alpha: false });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.setSize(window.innerWidth, window.innerHeight);
document.body.appendChild(renderer.domElement);

const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
controls.dampingFactor = 0.06;
controls.rotateSpeed = isMobile ? 0.6 : 0.8;
controls.zoomSpeed = 0.6;

window.addEventListener('resize', () => {
  camera.aspect = window.innerWidth / window.innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(window.innerWidth, window.innerHeight);
});

// ─── 3. Particles data (typed arrays for perf) ───────────────
// 每粒子的状态分散到多个 typed array（cache-friendly）
const positions   = new Float32Array(N_PARTICLES * 3);
const velocities  = new Float32Array(N_PARTICLES * 3);
const colors      = new Float32Array(N_PARTICLES * 3);
const sizes       = new Float32Array(N_PARTICLES);
const masses      = new Float32Array(N_PARTICLES);
const alive       = new Uint8Array(N_PARTICLES);
const flashTimers = new Int16Array(N_PARTICLES);
const fadeInTimers  = new Int16Array(N_PARTICLES);
const fadeOutTimers = new Int16Array(N_PARTICLES);

// HSL → RGB（用于赋粒子颜色）
function hsl2rgb(h, s, l) {
  // h in [0,1], s/l in [0,1]
  const a = s * Math.min(l, 1 - l);
  const f = n => {
    const k = (n + h * 12) % 12;
    return l - a * Math.max(-1, Math.min(k - 3, Math.min(9 - k, 1)));
  };
  return [f(0), f(8), f(4)];
}

function respawn(i) {
  const r = WORLD_RADIUS;
  positions[i*3+0] = (Math.random() - 0.5) * 2 * r;
  positions[i*3+1] = (Math.random() - 0.5) * 2 * r;
  positions[i*3+2] = (Math.random() - 0.5) * 2 * r;
  velocities[i*3+0] = (Math.random() - 0.5) * 60;
  velocities[i*3+1] = (Math.random() - 0.5) * 60;
  velocities[i*3+2] = (Math.random() - 0.5) * 60;

  const h = (params.hueBase + (Math.random() - 0.5) * params.hueRange + 1) % 1;
  const rgb = hsl2rgb(h, 0.75, 0.5);
  colors[i*3+0] = rgb[0] * params.brightness;
  colors[i*3+1] = rgb[1] * params.brightness;
  colors[i*3+2] = rgb[2] * params.brightness;

  sizes[i]  = 3 + Math.random() * 7;          // 3..10
  masses[i] = sizes[i];
  alive[i]  = 1;
  flashTimers[i]  = 0;
  fadeInTimers[i] = 20;
  fadeOutTimers[i] = -1;
}

// ─── 4. GUI 参数 ─────────────────────────────────────────────
const params = {
  // 通用
  hueBase: 0.55,
  hueRange: 0.3,
  brightness: 1.0,
  particleAlpha: 0.7,
  autoRotate: true,
  // Field
  noiseAmp: 25,
  vortexAmp: 80,
  spiralAmp: 0,
  attractorAmp: 0,
  noiseScale: 0.005,
  // Boid
  cohesion: 2.0,
  cohSpeed: 0.02,
  mergeDist: 8,
  // Cluster
  clusterGrid: 5,
  clusterMinFlash: 4,
  showGrid: false,
  // Audio
  masterVol: 0.6,
  droneVol: 0.5,
  eventVol: 0.4,
  reverb: 0.5,
  audioOn: true,
};

// ─── 5. Init particles ───────────────────────────────────────
for (let i = 0; i < N_PARTICLES; i++) respawn(i);

// ─── 6. Geometry + custom shader (additive points with depth fade) ─
const geometry = new THREE.BufferGeometry();
geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3).setUsage(THREE.DynamicDrawUsage));
geometry.setAttribute('color',    new THREE.BufferAttribute(colors, 3).setUsage(THREE.DynamicDrawUsage));
geometry.setAttribute('size',     new THREE.BufferAttribute(sizes, 1).setUsage(THREE.DynamicDrawUsage));

const vertShader = `
attribute float size;
varying vec3 vColor;
varying float vFade;
void main() {
  vColor = color;
  vec4 mvPos = modelViewMatrix * vec4(position, 1.0);
  float dist = max(1.0, length(mvPos.xyz));
  gl_PointSize = size * 1000.0 / dist;
  gl_PointSize = clamp(gl_PointSize, 0.5, 96.0);
  // depth fade
  vFade = clamp(900.0 / dist, 0.2, 1.4);
  gl_Position = projectionMatrix * mvPos;
}
`;

const fragShader = `
varying vec3 vColor;
varying float vFade;
void main() {
  vec2 c = gl_PointCoord - vec2(0.5);
  float d = length(c);
  if (d > 0.5) discard;
  float alpha = smoothstep(0.5, 0.0, d);
  gl_FragColor = vec4(vColor * vFade, alpha);
}
`;

const material = new THREE.ShaderMaterial({
  vertexShader: vertShader,
  fragmentShader: fragShader,
  blending: THREE.AdditiveBlending,
  transparent: true,
  depthTest: true,
  depthWrite: false,
  vertexColors: true,
});

const points = new THREE.Points(geometry, material);
scene.add(points);

// ─── 7. Cluster grid visualization (debug) ───────────────────
const gridGroup = new THREE.Group();
scene.add(gridGroup);

function rebuildGridViz(bboxMin, bboxMax, gridRes, cellCounts, threshold) {
  // 清空旧的
  while (gridGroup.children.length) {
    const c = gridGroup.children.pop();
    c.geometry?.dispose();
    c.material?.dispose();
  }
  if (!params.showGrid || !cellCounts) return;

  const sx = bboxMax.x - bboxMin.x;
  const sy = bboxMax.y - bboxMin.y;
  const sz = bboxMax.z - bboxMin.z;

  // 外框
  const outerGeom = new THREE.BoxGeometry(sx, sy, sz);
  const outerEdges = new THREE.EdgesGeometry(outerGeom);
  const outerMat = new THREE.LineBasicMaterial({ color: 0xffffff, transparent: true, opacity: 0.3 });
  const outerLine = new THREE.LineSegments(outerEdges, outerMat);
  outerLine.position.set((bboxMin.x + bboxMax.x) * 0.5, (bboxMin.y + bboxMax.y) * 0.5, (bboxMin.z + bboxMax.z) * 0.5);
  gridGroup.add(outerLine);
  outerGeom.dispose();

  // 每个 non-empty cell
  const csx = sx / gridRes, csy = sy / gridRes, csz = sz / gridRes;
  for (let idx = 0; idx < cellCounts.length; idx++) {
    const cnt = cellCounts[idx];
    if (cnt <= 0) continue;
    const cz = Math.floor(idx / (gridRes * gridRes));
    const cy = Math.floor((idx / gridRes) % gridRes);
    const cx = idx % gridRes;
    const cx_w = bboxMin.x + csx * (cx + 0.5);
    const cy_w = bboxMin.y + csy * (cy + 0.5);
    const cz_w = bboxMin.z + csz * (cz + 0.5);

    const isDense = cnt >= threshold;
    const cellGeom = new THREE.BoxGeometry(csx * 0.95, csy * 0.95, csz * 0.95);
    const edges = new THREE.EdgesGeometry(cellGeom);
    const mat = new THREE.LineBasicMaterial({
      color: isDense ? 0xff5050 : 0x6080ff,
      transparent: true,
      opacity: isDense ? 0.9 : 0.4,
    });
    const line = new THREE.LineSegments(edges, mat);
    line.position.set(cx_w, cy_w, cz_w);
    gridGroup.add(line);
    cellGeom.dispose();
  }
}

// ─── 8. AUDIO: Tone.js synths ─────────────────────────────────
let audioReady = false;
let droneSynth, droneFilter, droneVolNode;
let eventSynth, eventVolNode;
let reverb, masterVol;

async function initAudio() {
  await Tone.start();

  masterVol = new Tone.Volume(-6).toDestination();
  reverb = new Tone.Reverb({ decay: 4.5, wet: 0.5 }).connect(masterVol);
  await reverb.generate();

  // Drone：多声部 saw + filter
  droneVolNode = new Tone.Volume(-8).connect(reverb);
  droneFilter  = new Tone.Filter({ type: 'lowpass', frequency: 700, Q: 1.5 }).connect(droneVolNode);
  droneSynth = new Tone.PolySynth(Tone.Synth, {
    oscillator: { type: 'sawtooth' },
    envelope: { attack: 1.2, decay: 0.0, sustain: 1.0, release: 1.8 }
  }).connect(droneFilter);

  // Event sound：金属铃声
  eventVolNode = new Tone.Volume(-10).connect(reverb);
  eventSynth = new Tone.PolySynth(Tone.MetalSynth, {
    envelope: { attack: 0.001, decay: 0.1, release: 0.05 },
    harmonicity: 3.1,
    modulationIndex: 12,
    resonance: 2000,
    octaves: 1.2,
  });
  eventSynth.volume.value = -4;
  eventSynth.connect(eventVolNode);

  audioReady = true;
  console.log('[Audio] ready');
}

// 五声音阶（A minor pentatonic 跨多个八度）
const PENTA_NOTES = ['A2','C3','D3','E3','G3','A3','C4','D4','E4','G4','A4','C5','D5','E5','G5','A5'];

let activeDroneNotes = new Set();
let lastDroneUpdate = 0;

function updateDroneFromClusters(clusters) {
  if (!audioReady) return;
  const now = Tone.now();

  // 收集本帧"应有"的 notes（cluster 数对应 voice 数）
  const desiredNotes = new Set();
  for (let i = 0; i < Math.min(clusters.length, 4); i++) {
    // 按 cluster index 选音（chord priority）
    const idx = [0, 7, 12, 4][i] || 0;   // root, 5th, octave, M3
    // 量化到五声音阶
    const noteIdx = Math.min(PENTA_NOTES.length - 1, idx % PENTA_NOTES.length);
    desiredNotes.add(PENTA_NOTES[noteIdx]);
  }

  // 启动新的（in desired but not in active）
  for (const note of desiredNotes) {
    if (!activeDroneNotes.has(note)) {
      droneSynth.triggerAttack(note, now);
      activeDroneNotes.add(note);
    }
  }
  // 释放旧的（in active but not in desired）
  for (const note of activeDroneNotes) {
    if (!desiredNotes.has(note)) {
      droneSynth.triggerRelease(note, now);
      activeDroneNotes.delete(note);
    }
  }

  // 更新音量 (drone 比例 = clusters / 4)
  if (droneVolNode) {
    const ratio = clusters.length / 4;
    const dbTarget = -36 + ratio * 30;   // -36 到 -6 dB
    droneVolNode.volume.rampTo(dbTarget, 0.5);
  }
}

function triggerEvent(pos, mass) {
  if (!audioReady) return;
  // 用 mass 选音高（log scale 反向：mass 大 → 音低）
  const logMass = Math.log10(Math.max(1, mass));
  const t = Math.max(0, Math.min(1, 1 - (logMass - 0.5) / 2));
  const noteIdx = Math.floor(t * (PENTA_NOTES.length - 1));
  const note = PENTA_NOTES[noteIdx];
  try {
    eventSynth.triggerAttackRelease(note, '32n');
  } catch (e) { /* voice steal exception OK */ }
}

// ─── 9. Cluster detection 用 GUI 参数 ─────────────────────────
let lastBboxMin = new THREE.Vector3();
let lastBboxMax = new THREE.Vector3();
let lastCellCounts = [];
let lastGridRes = 5;

function detectClusters() {
  const gridRes = Math.max(2, Math.min(10, Math.floor(params.clusterGrid)));
  const totalCells = gridRes ** 3;

  // 1. 算 alive 粒子 bbox
  let minx = Infinity, miny = Infinity, minz = Infinity;
  let maxx = -Infinity, maxy = -Infinity, maxz = -Infinity;
  let anyAlive = false;
  for (let i = 0; i < N_PARTICLES; i++) {
    if (!alive[i]) continue;
    if (fadeOutTimers[i] >= 0) continue;
    const x = positions[i*3], y = positions[i*3+1], z = positions[i*3+2];
    if (x < minx) minx = x; if (x > maxx) maxx = x;
    if (y < miny) miny = y; if (y > maxy) maxy = y;
    if (z < minz) minz = z; if (z > maxz) maxz = z;
    anyAlive = true;
  }
  if (!anyAlive) return [];

  // 边缘 padding
  minx -= 2; miny -= 2; minz -= 2;
  maxx += 2; maxy += 2; maxz += 2;
  let sx = maxx - minx, sy = maxy - miny, sz = maxz - minz;
  if (sx < 1) sx = 1; if (sy < 1) sy = 1; if (sz < 1) sz = 1;

  const invSx = gridRes / sx, invSy = gridRes / sy, invSz = gridRes / sz;

  // 2. 只数闪烁粒子 → cells
  const counts = new Int32Array(totalCells);
  const posSumX = new Float32Array(totalCells);
  const posSumY = new Float32Array(totalCells);
  const posSumZ = new Float32Array(totalCells);
  const massSum = new Float32Array(totalCells);

  for (let i = 0; i < N_PARTICLES; i++) {
    if (!alive[i]) continue;
    if (fadeOutTimers[i] >= 0) continue;
    if (flashTimers[i] <= 0) continue;   // 只数闪烁

    const x = positions[i*3], y = positions[i*3+1], z = positions[i*3+2];
    let ix = Math.floor((x - minx) * invSx);
    let iy = Math.floor((y - miny) * invSy);
    let iz = Math.floor((z - minz) * invSz);
    if (ix < 0) ix = 0; if (ix >= gridRes) ix = gridRes - 1;
    if (iy < 0) iy = 0; if (iy >= gridRes) iy = gridRes - 1;
    if (iz < 0) iz = 0; if (iz >= gridRes) iz = gridRes - 1;
    const idx = ix + iy * gridRes + iz * gridRes * gridRes;
    counts[idx]++;
    posSumX[idx] += x; posSumY[idx] += y; posSumZ[idx] += z;
    massSum[idx] += masses[i];
  }

  // 3. 缓存可视化
  lastBboxMin.set(minx, miny, minz);
  lastBboxMax.set(maxx, maxy, maxz);
  lastGridRes = gridRes;
  lastCellCounts = counts;

  // 4. 找超阈值 cell
  const thresh = Math.max(1, Math.floor(params.clusterMinFlash));
  const clusters = [];
  for (let idx = 0; idx < totalCells; idx++) {
    if (counts[idx] < thresh) continue;
    const n = counts[idx];
    clusters.push({
      centroid: new THREE.Vector3(posSumX[idx] / n, posSumY[idx] / n, posSumZ[idx] / n),
      count: n,
      totalMass: massSum[idx],
    });
  }
  clusters.sort((a, b) => b.totalMass - a.totalMass);
  return clusters.slice(0, 4);
}

// ─── 10. Update loop (physics + audio dispatch + render) ─────
let lastClusterCount = 0;
let frameCount = 0;
let fpsAccum = 0;
let lastTime = performance.now();

function update(dt) {
  // 限制 dt（避免大帧 spike）
  if (dt > 0.05) dt = 0.05;

  const noiseAmp     = params.noiseAmp;
  const vortexAmp    = params.vortexAmp;
  const spiralAmp    = params.spiralAmp;
  const attractorAmp = params.attractorAmp;
  const ns           = params.noiseScale;
  const coh          = params.cohesion;
  const cohSpeed     = params.cohSpeed;
  const mergeD       = params.mergeDist;
  const mergeSq      = mergeD * mergeD;
  const maxDist      = WORLD_RADIUS * 2.5;
  const maxDistSq    = maxDist * maxDist;
  const damping      = 0.95;
  const K            = 6;  // 邻居采样数（性能：从 C++ 的 10 降到 6）
  const time         = performance.now() * 0.0001;

  // 收集本帧 merge 事件用于音频触发
  const mergeEvents = [];

  for (let i = 0; i < N_PARTICLES; i++) {
    if (!alive[i]) continue;

    let px = positions[i*3], py = positions[i*3+1], pz = positions[i*3+2];

    // ─── Field forces ───
    let fx = 0, fy = 0, fz = 0;

    // Noise field (using time-based pseudo-perlin via sin)
    if (noiseAmp > 0.1) {
      const s = noiseAmp * 4;
      fx += s * (Math.sin(px * ns + time) - 0.5);
      fy += s * (Math.sin(py * ns * 1.3 + time + 1.7) - 0.5);
      fz += s * (Math.sin(pz * ns * 1.7 + time + 3.3) - 0.5);
    }

    // Vortex (around Y axis)
    if (vortexAmp > 0.1) {
      const r2 = px * px + pz * pz;
      if (r2 > 1) {
        const invR = 1.0 / Math.sqrt(r2);
        const s = vortexAmp * 4;
        fx += -pz * invR * s;
        fz +=  px * invR * s;
      }
    }

    // Spiral (vortex + Y drift)
    if (spiralAmp > 0.1) {
      const s = spiralAmp * 4;
      const r2 = px * px + pz * pz;
      if (r2 > 1) {
        const invR = 1.0 / Math.sqrt(r2);
        fx += (-pz * invR - px * invR * 0.3) * s;
        fy += s * 0.6;
        fz += ( px * invR - pz * invR * 0.3) * s;
      } else {
        fy += s * 0.5;
      }
    }

    // Attractor (向原点吸引)
    if (attractorAmp > 0.1) {
      const s = attractorAmp * 4 * 0.02;
      fx -= px * s;
      fy -= py * s;
      fz -= pz * s;
    }

    // ─── Boid: K-random neighbor sample for cohesion + merge ───
    let cohX = 0, cohY = 0, cohZ = 0;
    let cohN = 0;

    for (let k = 0; k < K; k++) {
      const j = Math.floor(Math.random() * N_PARTICLES);
      if (j === i || !alive[j]) continue;
      const dx = positions[j*3]   - px;
      const dy = positions[j*3+1] - py;
      const dz = positions[j*3+2] - pz;
      const dsq = dx*dx + dy*dy + dz*dz;

      // Merge
      if (dsq < mergeSq && flashTimers[i] === 0 && flashTimers[j] === 0) {
        // 大的吞小的
        if (sizes[i] >= sizes[j]) {
          // i 吞 j
          masses[i] += masses[j];
          sizes[i] = Math.min(30, Math.pow(masses[i], 1/3) * 2);
          // color blend
          const wi = masses[i] / (masses[i] + masses[j]);
          colors[i*3]   = colors[i*3]   * wi + colors[j*3]   * (1-wi);
          colors[i*3+1] = colors[i*3+1] * wi + colors[j*3+1] * (1-wi);
          colors[i*3+2] = colors[i*3+2] * wi + colors[j*3+2] * (1-wi);
          flashTimers[i] = 14;
          // j 启动 fadeOut
          fadeOutTimers[j] = 20;
          mergeEvents.push({ x: px, y: py, z: pz, mass: masses[i] });
        } else {
          // j 吞 i
          masses[j] += masses[i];
          sizes[j] = Math.min(30, Math.pow(masses[j], 1/3) * 2);
          flashTimers[j] = 14;
          fadeOutTimers[i] = 20;
          mergeEvents.push({ x: px, y: py, z: pz, mass: masses[j] });
          break;   // i 被吞，停止处理
        }
        continue;
      }

      // Cohesion (within neighbor range)
      if (dsq < 80 * 80 && dsq > 0.01) {
        cohX += positions[j*3];
        cohY += positions[j*3+1];
        cohZ += positions[j*3+2];
        cohN++;
      }
    }

    if (!alive[i]) continue;

    if (cohN > 0) {
      cohX /= cohN; cohY /= cohN; cohZ /= cohN;
      const cohF = coh * cohSpeed;
      fx += (cohX - px) * cohF;
      fy += (cohY - py) * cohF;
      fz += (cohZ - pz) * cohF;
    }

    // 边界拉力
    const distSq = px*px + py*py + pz*pz;
    if (distSq > maxDistSq) {
      const dist = Math.sqrt(distSq);
      const pull = (dist - maxDist) * 0.4;
      fx -= px / dist * pull;
      fy -= py / dist * pull;
      fz -= pz / dist * pull;
    }

    // 积分
    velocities[i*3]   = (velocities[i*3]   + fx * dt) * damping;
    velocities[i*3+1] = (velocities[i*3+1] + fy * dt) * damping;
    velocities[i*3+2] = (velocities[i*3+2] + fz * dt) * damping;
    positions[i*3]   += velocities[i*3]   * dt;
    positions[i*3+1] += velocities[i*3+1] * dt;
    positions[i*3+2] += velocities[i*3+2] * dt;

    // Timers
    if (flashTimers[i] > 0) flashTimers[i]--;
    if (fadeInTimers[i] > 0) fadeInTimers[i]--;
    if (fadeOutTimers[i] > 0) {
      fadeOutTimers[i]--;
      if (fadeOutTimers[i] === 0) {
        alive[i] = 0;
      }
    }
  }

  // Spawn dead particles back (rate-limited)
  let spawned = 0;
  const spawnsPerFrame = Math.floor(N_PARTICLES / 30);  // 每帧最多复活 1/30
  for (let i = 0; i < N_PARTICLES && spawned < spawnsPerFrame; i++) {
    if (!alive[i]) {
      respawn(i);
      spawned++;
    }
  }

  // ─── Audio events ───
  // 限速：每帧最多触发 4 个 event 声（防止音频过载）
  const maxEvents = isMobile ? 2 : 4;
  for (let i = 0; i < Math.min(mergeEvents.length, maxEvents); i++) {
    const ev = mergeEvents[i];
    triggerEvent(ev, ev.mass);
  }

  // ─── Cluster detection (每 6 帧跑一次省 CPU) ───
  if (frameCount % 6 === 0) {
    const clusters = detectClusters();
    lastClusterCount = clusters.length;
    updateDroneFromClusters(clusters);
    rebuildGridViz(lastBboxMin, lastBboxMax, lastGridRes, lastCellCounts, params.clusterMinFlash);
  }
}

// ─── 11. GUI 设置 ────────────────────────────────────────────
const gui = new GUI({ title: 'Flock 3D' });
if (isMobile) gui.close();   // 手机默认折叠节省屏幕

const fVisual = gui.addFolder('Visual');
fVisual.add(params, 'hueBase', 0, 1, 0.01);
fVisual.add(params, 'hueRange', 0, 1, 0.01);
fVisual.add(params, 'brightness', 0, 1.5, 0.01);
fVisual.add(params, 'autoRotate');

const fField = gui.addFolder('Fields');
fField.add(params, 'noiseAmp', 0, 200, 1);
fField.add(params, 'vortexAmp', 0, 200, 1);
fField.add(params, 'spiralAmp', 0, 200, 1);
fField.add(params, 'attractorAmp', 0, 200, 1);
fField.add(params, 'noiseScale', 0.0005, 0.02, 0.0005);

const fFlock = gui.addFolder('Flock');
fFlock.add(params, 'cohesion', 0, 10, 0.1);
fFlock.add(params, 'cohSpeed', 0, 0.2, 0.005);
fFlock.add(params, 'mergeDist', 2, 30, 1);

const fCluster = gui.addFolder('Cluster');
fCluster.add(params, 'clusterGrid', 3, 10, 1);
fCluster.add(params, 'clusterMinFlash', 1, 50, 1);
fCluster.add(params, 'showGrid');

const fAudio = gui.addFolder('Audio');
fAudio.add(params, 'masterVol', 0, 1, 0.01).onChange(v => {
  if (masterVol) masterVol.volume.rampTo(20 * Math.log10(Math.max(0.001, v)) , 0.1);
});
fAudio.add(params, 'reverb', 0, 1, 0.01).onChange(v => { if (reverb) reverb.wet.value = v; });
fAudio.add(params, 'audioOn').onChange(v => { if (masterVol) masterVol.mute = !v; });

// ─── 12. HUD ─────────────────────────────────────────────────
const hud = document.getElementById('hud');
let aliveCountForHUD = N_PARTICLES;

// 按 H 隐藏 GUI（电脑）
window.addEventListener('keydown', (e) => {
  if (e.key === 'h' || e.key === 'H') {
    if (gui._closed) gui.open(); else gui.close();
  }
});

// ─── 13. Main loop ───────────────────────────────────────────
function animate() {
  requestAnimationFrame(animate);

  const now = performance.now();
  const dt = Math.min(0.05, (now - lastTime) * 0.001);
  lastTime = now;
  frameCount++;
  fpsAccum = fpsAccum * 0.95 + (1 / Math.max(dt, 0.001)) * 0.05;

  if (audioReady) {
    update(dt);
  }

  // Auto-rotate camera
  if (params.autoRotate) {
    const angle = now * 0.0001;
    const r = 1100;
    camera.position.x = Math.cos(angle) * r;
    camera.position.z = Math.sin(angle) * r;
    camera.position.y = -400;
    camera.lookAt(0, 0, 0);
  } else {
    controls.update();
  }

  // 更新 buffer
  geometry.attributes.position.needsUpdate = true;
  geometry.attributes.color.needsUpdate = true;
  geometry.attributes.size.needsUpdate = true;

  renderer.render(scene, camera);

  // HUD update (每 10 帧)
  if (frameCount % 10 === 0) {
    if (frameCount % 30 === 0) {
      aliveCountForHUD = 0;
      for (let i = 0; i < N_PARTICLES; i++) if (alive[i]) aliveCountForHUD++;
    }
    hud.textContent = `fps: ${fpsAccum.toFixed(0)}\nparticles: ${aliveCountForHUD}/${N_PARTICLES}\nclusters: ${lastClusterCount}`;
  }
}

// ─── 14. Start button: 用户手势后启动 Tone.js ────────────────
const startBtn = document.getElementById('start-btn');
const overlay  = document.getElementById('start-overlay');
const help     = document.getElementById('help');

startBtn.addEventListener('click', async () => {
  await initAudio();
  overlay.classList.add('hidden');
  setTimeout(() => overlay.remove(), 700);
  if (!isMobile) help.style.display = 'block';
  animate();
});
