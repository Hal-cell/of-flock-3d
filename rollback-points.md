# of-flock-3d Rollback Points

Git tags marking stable checkpoints. Use `git checkout <tag>` to inspect, or `git reset --hard <tag>` to revert.

## rp-00 — Visual baseline (no audio)

**Commit**: `git tag rp-00-visual-only`

**What's working**:
- Pure 3D flock particle system (no shape morph)
- 6 macro fields blend-able (noise / vortex / spiral / curl / attractor / repeller)
- Boid forces (separation, cohesion + cohesion speed)
- Particle merging + lifecycle + smooth fade in/out
- Auto-save/load GUI settings on Cmd+Q / startup
- Cluster detection scaffold (`getTopByMass`, `getStats`, `CollisionEvent` queue) — but no audio consumer yet

**Use this checkpoint to**:
- Reset audio experiment to start over with a different DSP approach
- Compare audio-on vs audio-off performance
- Branch off for parallel sound design directions

## rp-01 — Audio synth v1 (A + B + D)

**Commit**: `git tag rp-01-synth-v1` → `536d3fd`

**What's working**:
- 3-layer synthesizer in pure C++ (no ofxMaxim / Max / external)
  - **A. DroneLayer** — 4 detuned sines modulated by flock stats
    - aliveRatio → volume / meanSpeed → LFO rate / spread → lowpass cutoff
  - **B. VoicePool** (8 voices) — one per top-K-by-mass cluster
    - mass → pitch (quantized to scale) / x → pan / y → cutoff / color → sine/saw mix
  - **D. ModalReverb** — 4-tap feedback delay network + tanh limiter
- New `Flock3D::getStats() / getTopByMass(K)` interface
- Thread-safe via `std::atomic` (main 60Hz writes, audio 44.1kHz reads)
- 4 scales: pentatonic minor / Lydian / wholetone / harmonic series
- Synth GUI panel with master/drone/voice/reverb + root freq + scale selector
- Settings split: `flock_settings.xml` + `synth_settings.xml`

**Use this checkpoint to**:
- Reset to this v1 if you want to try a fundamentally different DSP approach
  (e.g., switch to granular synthesis, FM, or external Max/MSP via OSC)
- Compare different audio engines while keeping flock visual constant

## rp-02 — Particle synthesizer (collision-triggered events)

**Commit**: `git tag rp-02-particle-synth` → `a578f97`

**What changed vs rp-01**:
- Removed cluster-voice polyphony (B layer)
- Replaced with **EventVoices** — 32 voice pool, each carbon-copies a damped
  sine triggered by one particle merge
- Lock-free SPSC ring buffer (256 slots) for collision events
- Voice stealing: when all 32 busy, replace voice with lowest amplitude
- FM brightness modulation: color brightness → modulation index
- Decay time GUI-adjustable (5..500 ms; default 50 ms)
- Min-mass threshold to suppress micro-mergers (防 spam)

**Sonic character**:
- Drone底层依旧持续
- 每次 merge = 一个短促 ping/bell
- 大量 merge 时变成 granular cloud
- Reverb tail 把短促 hit 拉成空间感

**Use this checkpoint to**:
- Reset here if exploring DIFFERENT particle-trigger DSP
  (FM, Karplus-Strong pluck, filtered noise burst, granular slice...)
- Compare different transient designs while keeping drone + flock 不变

## rp-03 — Additive synthesis event voices (better timbre)

**Commit**: `git tag rp-03-additive-event` → `2c4d1dc`

**What changed vs rp-02**:
- 替换 FM-on-single-sine 为 **4-partial 加性合成**（更接近真实打击乐）
- Inharmonic partial ratios: {1.0, 2.06, 3.18, 4.34}（tubular bell 风格）
- 每分音独立衰减：高分音衰减比 P0 快 ~2x / 3x / 4.5x
- 2ms attack envelope 防起音 click
- 各分音错开初相位，避免触发瞬间 in-phase 堆积
- Brightness (粒子颜色) 调节高分音振幅（暗 → 纯 sine；亮 → 全分音）
- Anti-alias cap：partial freq ≤ 0.45 × sampleRate
- 预算 pan 系数（性能）
- 降低 reverb 反馈（避免遮盖短音细节）

**Sonic character**:
- 短促打击声更接近真实 bell / marimba
- 起音瞬态自然（无电子 click）
- 暗色 / 亮色粒子产生不同音质
- 高频先消失、低频温暖余韵 → 类真实声学

**Use this checkpoint to**:
- 基准最佳音质，从这里探索更深入的变种
  （加 Karplus-Strong pluck / noise burst / 颗粒采样回放等）

## rp-04 — Visual flash on collision

**Commit**: `git tag rp-04-merge-flash` → `78eeb54`

**What's new**:
- Winner 粒子 merge 后**短暂高亮**
  - 颜色 lerp 向白（HDR 感）
  - Size pulse 1.0x → 2.5x → 1.0x
  - Alpha 提升
- 与已有 fadeIn / fadeOut 自然叠加
- Particle 新增 `flashTimer` 字段；merge 时设值，update 每帧 -1
- GUI 新增 `flash frames`（默认 12 ≈ 200ms）+ `flash intensity`（默认 1.0）
- 闪烁 = 听觉事件触发同时的视觉对应物 → 视听同步

**Use this checkpoint to**:
- 试验不同 flash 风格（彩色闪烁 / 仅 size pulse / 仅 alpha 闪烁）
- 在视觉密度上有差异化展示

## rp-05 — Accent merges (chance-driven octave + larger flash)

**Commit**: `git tag rp-05-accent-octave` → `77576d7`

**What's new**:
- 每次 merge 抛一次骰子 → 决定本次是否为"accent" 重音事件
- 一次抛骰子结果同步影响视觉 + 音频，保证 audio-visual 严格对齐
- **视觉端**：accent flash 更大（默认 2.5x size）+ 更久（2x 时长）
- **音频端**：accent event 基频 ×2（+1 octave）+ 振幅 1.3x
- 新增 Particle.flashScale + CollisionEvent.isAccent flag

**GUI 新参数**:
  - `accent chance` (0..1, 默认 0.1 = 10% 概率)
  - `accent size`   (1..5, 默认 2.5x 普通 flash)

**Sonic + visual character**:
  - 在持续的颗粒流中偶有"重音"突起 — 听觉上更亮的"叮"
  - 视觉上同时是更大的白闪 → 视听强同步
  - 调高 `accent chance` 到 0.3-0.5 → 节奏感更强烈
  - 调到 1.0 → 全部 accent，相当于把 root pitch 整体 +1 octave

**Use this checkpoint to**:
- 试验不同 accent 设计（更高 octave、不同音色、彩色闪烁等）

## rp-06 — Tunable event synth (per-partial GUI control)

**Commit**: `git tag rp-06-tunable-synth` → `ca44972`

**What's new**:
- 把硬编码的 partial 结构（ratio / amp / decay）完全暴露到 GUI
- 13 个新参数全部归入 `partials` 子组：
  - `p0..p3 ratio`  (频率倍率，0.25..12.0)
  - `p0..p3 amp`    (振幅，0..1.5)
  - `p0..p3 decay`  (衰减 ratio，0.05..2.0)
- `attack (ms)`     起音 ramp（独立暴露）
- 架构改进：partial 计算完全在主线程 → TriggerEvent 完整携带 →
  音频线程零成本 copy，无 ofParameter 读取，线程更安全

**Defaults 仍是 tubular bell**（{1.0, 2.06, 3.18, 4.34}），向后兼容。

**调音能力**:
| 想要 | 怎么调 |
|---|---|
| 纯 sine（"ping") | p1/p2/p3 amp 全调 0 |
| 锯齿和谐音 | p1/p2/p3 ratio = 2/3/4, amp 阶梯递减 |
| 编钟 / gong | ratio 调成不和谐（如 1, 2.51, 5.43, 7.87） |
| 木琴 marimba | ratio = 1, 4.0, 10.0, 17; 高分音衰减极快 |
| 长 chime | decay = 1.0 全分音，attack 5-10ms |

**Use this checkpoint to**:
- 调出自己心爱的音色后保存 settings.xml 当 preset
- 探索极端参数空间不破坏稳定基线

## rp-07 — 2-op FM event synth (replacing additive)

**Commit**: `git tag rp-07-fm-synth` → `4c18dd5`

**What changed**:
- 替换 4-partial 加性合成为经典 **2-op FM**（Chowning 1973）
  ```
  output = sin(2π·f_c·t + modIndex·sin(2π·f_m·t))
  ```
- GUI 移除 13 个 partial 参数；加 `FM` 子组：
  - `FM ratio`: 0.5..8.0，自动 snap 到最近 0.5 倍数
  - `FM index`: 0..12，调制深度（0=纯 sine，10+=极金属）
  - `FM idxDecay (ms)`: modIndex 衰减时长（独立于 carrier）
- Brightness（粒子颜色）线性缩放 modIndex（0.3..1.0）
- Accent 再 × 1.5 modIndex → 更亮的重音

**调音速查**：
| FM ratio | 听感 |
|---|---|
| 0.5  | sub-harmonic，低沉粗厚 |
| 1.0  | 自我调制，类方波 |
| 1.5  | 5th 上方调制，柔和 |
| 2.0  | 奇数谐波，clarinet |
| 3.0  | 整数倍，brass-y |
| 3.5  | 不和谐，bell-like |
| 4.0  | 高谐波，bright EP |
| 7.0  | 经典 DX7 bell |
| 7.5  | 极不和谐，gong/anvil |

**Use this checkpoint to**:
- 切回此基线重新挑选音色
- 加更多 op（3-op / 4-op FM 算法）
- 试验 modIndex 调制函数（如非线性映射）

## rp-08 — Polyphonic cluster drones

**Commit**: `git tag rp-08-cluster-drones` → `85cc443`

**What's new**:
- 实时检测粒子团簇 → 每个 cluster 对应一个 drone voice
- Cluster 出现 → 对应 drone 淡入；cluster 消散 → drone 淡出
- 所有 drone + event sound 共享 scale 量化 → 自然和声关系

**Flock3D 侧**:
- 3D spatial grid hash（默认 12³ cells）
- 每帧把活粒子塞进 cells；密度 + 总质量超阈值的 cell = cluster
- `getClusters(maxK)` 返回质量降序 top-K
- 新 GUI：`cluster grid` / `cluster minMass` / `cluster minCount`

**Synth 侧**:
- 8-voice polyphonic drone pool
- 每 voice = 3 个 detuned sine
- Pitch 用 `massToFreq()` 量化到当前 scale（与 event 同调）
- Voice 分配按空间邻近：cluster 离已激活 voice 近 → 复用；否则分配空槽
- Voice 没匹配上 → 启动 fadeout 倒计时（按 release ms）
- 完全淡出 → 槽位释放
- Lock-free：atomic targets 跨线程，currentVol 音频本地

**GUI 新参数（ClusterDrone 子组）**:
| 参数 | 范围 | 默认 |
|---|---|---|
| `vol` | 0..1 | 0.5 |
| `attack (ms)` | 50..4000 | 800 |
| `release (ms)` | 50..6000 | 1500 |
| `detune` | 0..0.02 | 0.005 |
| `proximity` | 10..400 | 80 |

**HUD**: 显示当前检测到的 cluster 数（= drone voice 数）

**Use this checkpoint to**:
- 探索更智能的音高分配（避免重复音高、preferred 间隔等）
- 试验 cluster 跟踪算法（DBSCAN、Hungarian assignment）
- 让每个 cluster 用不同合成方法（不只 sine drone）

## rp-09 — Cluster drone overhaul (saw+SVF, BFS, chord pitches)

**Commit**: `git tag rp-09-saw-drone-bfs` → `35b19c7`

**Three big improvements**:

### 1) Cluster 检测：BFS 连通区域
- 旧版每个密集 cell 独立成 cluster（一个大团被切成几个）
- 新版用 BFS 把相邻密集 cell 合并 → 一个 cluster 反映真实的"粒子团"
- 拆分参数：
  - `cellDensity`：单 cell 算"密集"的最小粒子数（种子 + 扩展）
  - `minCount` / `minMass`：合并后整体 cluster 的总量门槛
- 现在大 flock 团会正确识别为 1 个 cluster

### 2) 全局 Drone 删除
- 删 DroneLayer（4 detuned sine + LFO + lowpass）
- 删 Synth.updateStats、所有 a_aliveRatio 等 atomic
- ofApp 不再调用 updateStats
- 所有 drone 音都来自 per-cluster voice 池

### 3) Cluster Drone DSP：saw + SVF + 和声优先 pitch
- 每 voice = 3 detuned saw（PolyBLEP 抗混叠） → state-variable lowpass
- 新 GUI：`cutoff (Hz)` (80..8000) + `resonance` (0..0.95)
- 经典模拟合成器 pad 质感

**pickFreshSemitone()**：
- 优先级表：root → 8va → 5th → 2 oct → 5th+oct → M3 → M3+oct → m3 → ...
- 跳过任一已激活 voice 占用的半音
- 候选值量化到当前 scale → 与 event sound 同调
- 结果：drones 自动形成干净 chord，无重复音高

**Use this checkpoint to**:
- 接 LFO 调制 cutoff（呼吸感）
- 多滤波器类型（HP / BP / notch）
- envelope follower → cutoff（声音明亮跟着粒子动）
- 给 saw 加 PWM 或更复杂波形

## rp-10 — Cap drone polyphony at 4

**Commit**: `git tag rp-10-drone-cap-4` → `0d54bbf`

**Behavior spec**:
- 0 clusters → 所有 drone 释放到静音
- 1 cluster → 1 drone attack → sustain
- N clusters (1..4) → N drones attack → sustain，pitch 自动 chord 分配
- cluster 消失 → 对应 drone 启动 release
- cluster 在 release 中复现近邻 → 同一 voice rebounds 回 sustain
- **最多 4 个 drone 同时发声**（cluster > 4 时只发声 top-4 by mass）

**Changes from rp-09**:
- NUM_DRONE_VOICES: 8 → 4
- HUD 显示 'clusters: N   drones active: M / 4'

ADSR 行为已实现（rp-09），本次只是精简池容量到用户指定的 4。

**Use this checkpoint to**:
- 试不同 cap（如 6 或 8）— 改 NUM_DRONE_VOICES 常量
- 加 voice stealing（cluster > 4 时偷小 voice 给大 cluster）
- 改 release rebound 行为

## rp-11 — Fix ADSR cliff (linear envelope + state reset)

**Commit**: `git tag rp-11-fix-envelope-cliff` → `9d00192`

**Bug**: Drone 偶尔突然出现 / 突然消失，绕过 attack / release。

**根因（两个叠加）**:
1. **指数包络在 release 中途被掐**：旧版用 `current += (target-current)*coef`，
   coef = `1 - exp(-1/τ)`。τ 时长后只到 0.37，不是 0。但 fadeoutFrames 倒计时
   按 releaseMs 算 → audio 还在 0.37 时 voice 被 active=false → cliff drop
2. **复用 voice 时 currentVol 残留**：上次释放结束时 currentVol 卡在 0.37
   左右，新 voice 分配时没重置 → attack 从 0.37 起步 → "突然出现"

**修复**:
1. **线性包络**：`perSample = 1 / (envMs * 0.001 * sr)`，时间精准到点严格归零/满值
2. **新 voice 激活强制重置**：currentVol=0、phase、currentFreq、SVF state 全清
3. **时间精准倒计时**：用 `ofGetLastFrameTime()` 而不是假设 60fps
4. **5% 安全余量**：fadeout 倒计时比 audio release 多 5%，确保 envelope 到 0
   后才释放 voice 槽

**Use this checkpoint to**:
- 实验更复杂的包络曲线（指数+线性混合、自定义曲线）
- 加 decay/sustain 阶段（变 ADSR 为完整 4-stage）

## rp-12 — Hall reverb (Hadamard FDN + HF damping + pre-delay)

**Commit**: `git tag rp-12-hall-reverb` → `0de67b5`

升级简单 FDN 为真正的"大空间"hall 混响。

**架构变化**:
- Delay 长度 ×3：34..57ms → **152..283ms**（cathedral 级别的空间尺度）
- **Hadamard 4×4 矩阵**：每个 sample 把 4 条 delay 输出旋转 mix → 全对全 cross-feedback
  → 几个 sample 内就密集扩散，没有 flutter echo
- **HF damping**（每条 delay 一个 1-pole LP）：高频比低频衰减快 → 自然空间感
- **Pre-delay** (0..200ms)：reverb 前的"空气间隙" → 距离感
- **立体声 split 输出**：d[0]+d[2] → L, d[1]+d[3] → R → reverb tail 自然立体扩散

**新 GUI**:
| 参数 | 范围 | 默认 |
|---|---|---|
| `reverbSize` | 0..0.97 | 0.85 (~4 sec RT60) |
| `reverbDamp` | 0..0.99 | 0.5 (中度衰减) |
| `reverb preDelay (ms)` | 0..200 | 20 |

**音色变化**:
- 旧：短促小房间 ping-pong（~50ms tail）
- 新：宽阔大厅 wash（数秒 tail）+ 高频自然消逝

**Use this checkpoint to**:
- 加 8-delay FDN 更密集
- 加 modulated delays（chorused reverb）
- 实验 plate / cathedral / spring 等不同空间预设

## rp-13 — Particle trails (audio-correlated light beams)

**Commit**: `git tag rp-13-particle-trails` → `a32f814`

**What's new**: 每个粒子拖一条光束尾巴，长度与音频参数正相关。

**轨迹来源**:
- 每个 Particle 内嵌 24 位环形 buffer 存最近位置
- 每帧 update 末尾 push 当前 pos
- Trail 自然展示粒子在 field velocity 下走过的路径
  （velocity 由 6 个 field 决定，所以 trail 形态 = field 流场轨迹）

**Audio 正相关公式**:
```
audio_influence = avg([
  eventDecayMs   (50..500)   → 0..1,
  fmRatio        (0.5..8)    → 0..1,
  clusterCutoff  (80..8000)  → 0..1
])
effective_len = base_len × (0.5 + audio_influence × sensitivity × 1.5)
```
- 音频静（影响=0）→ 0.5x base
- 音频满 + sens=1 → 2.0x base

**渲染**:
- ofMesh OF_PRIMITIVE_LINES，逐段加 alpha fade（oldest=透明 → newest=不透明）
- 与粒子同在 additive blend pass → 自然光束 glow 感

**新 GUI**:
| 参数 | 范围 | 默认 |
|---|---|---|
| `tail length` | 0..24 | 8 |
| `tail audio sens` | 0..2 | 1.0 |
| `tail alpha` | 0..1 | 0.45 |

**性能**: 20K 粒子 × 24 vec3 ≈ 5.5MB 内存；线段最多 20K×23×2 = 920K vertices/frame，
现代 GPU 60fps 没问题

**Use this checkpoint to**:
- 加 thickness（geometry shader / quad-billboard）让线条更"实"
- color gradient（沿 trail 改变颜色，eg field 类型染色）
- 加 velocity-vector display 调试用

## rp-14 — Trail rendering performance (5x faster)

**Commit**: `git tag rp-14-trail-perf` → `bb1fe00`

**问题**: tail length 调长后明显卡顿（~1.8M push_back/frame）。

**优化** (5 处)：
1. **缓存 ofMesh**：trailMesh 成为成员变量，clear() 保留 vector capacity
   → 避免每帧重新分配 ~5MB 内存
2. **Reserve capacity**：算上限先 reserve，跳过 vector 自动 grow 的 realloc
3. **直接 push_back**：用 `getVertices().push_back()` 而不是 `addVertex()` wrapper
4. **去掉 modulo**：内循环用增量 + `if (idx >= MAX) idx -= MAX`
   （modulo 约 10-20 cycles，增量 1-2 cycles）
5. **Stride sampling**：长尾巴自动跳点采样
   - effLen ≤ 12 → 满采样
   - effLen 13..18 → 隔点（step 2）
   - effLen 19..24 → 三分之一（step 3）
   - 长尾看起来一样平滑，因为视距远 → 跳点 invisible

综合 3-5x 性能提升。`tail length = 24` 也能流畅。

**Use this checkpoint to**:
- 进一步优化：用 ofVbo + GL_LINES + 持久映射 buffer
- 用 geometry shader 扩展 lines 成 quad（更厚的光束）
- LOD 系统（远处粒子只画粒子不画 trail）

## rp-15 — Tail length drives FM ratio + event decay (reverse direction)

**Commit**: `git tag rp-15-tail-drives-audio` → `265df20`

**Change**: tail length 现在反向驱动 FM ratio 和 event decay。

**避免反馈循环的拆分**:
| 方向 | 涉及参数 |
|---|---|
| audio → visual | cluster cutoff 影响 tail length |
| visual → audio | tail length 影响 FM ratio + event decay |
| 解耦 | cutoff 不被 tail 驱动；FM/decay 不参与 audio→tail |

三个正相关关系（tail ↔ cutoff, tail ↔ FM, tail ↔ decay）全保留，但因果链无循环。

**调制公式**:
```
effFmRatio = baseFmRatio + tailInfluence × tailToFmDepth × 4.0
effDecayMs = baseDecayMs + tailInfluence × tailToDecayDepth × 400ms
```
其中 `tailInfluence = tail length GUI 值 / TRAIL_MAX (0..1)`

**新 GUI（FM 子组下）**:
- `tail → FM` (0..1, 默认 0.5)
- `tail → decay` (0..1, 默认 0.5)

**Use this checkpoint to**:
- 加更多 visual → audio 映射（速度 → 颤音、密度 → 滤波、color → 音色）
- 把 tail → audio 也设成可正可负（双向调制）

## rp-16 — Fix cluster over-detection (relative density threshold)

**Commit**: `git tag rp-16-cluster-density-ratio` → `2b94f4f`

**Bug**: 没有真正聚集时也总检测到一个 cluster。

**根因**: 旧版 `cellDensity = 6` 是绝对阈值；20K 粒子 / 12³ cell 平均每 cell 11.6 个
→ 几乎所有 cell 都过阈值 → BFS 合并所有 cell 成一个大 cluster。

**修复**: 改成相对阈值
```
effectiveCellDensity = max(2, avgDensity × densityRatio)
其中 avgDensity = alive_count / total_cells
```

**新 GUI**:
| 参数 | 范围 | 默认 |
|---|---|---|
| `cluster density ratio` | 1.5..10 | 3.0 (cell 必须比均匀 3 倍密才算"聚集") |
| `cluster minCount` | 10..1000 | 80 (整团总粒子数下限) |
| `cluster minMass` | 10..5000 | 200 (整团总质量下限) |

**统计学保证**: ratio=3.0 时，Poisson 随机分布产生 cell ≥ 35 的概率 ≈ 1e-9
→ 实际上不可能被随机波动触发，**只有真正的 flock 聚集**才被检测到

**Use this checkpoint to**:
- 加 cluster 跟踪稳定性（Hungarian algorithm 匹配前后帧）
- 加 cluster 持续时间过滤（瞬时 cluster 不算数）
- DBSCAN 替换 BFS（更稳健的密度聚类）
