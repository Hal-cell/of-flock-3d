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

## rp-31 — ofxImGui GUI（单窗口 tabbed）

**Commit**: `git tag rp-31-imgui-gui`

**What changed**:
- GUI 从 ofxGui 迁到 ofxImGui — 单 panel + tabbed (Visual / Synth / Help) + 暗蓝圆角主题
- 折叠 sections（Flock 8 组 / Synth 6 组）
- Scale 选择改成命名下拉框（12 个 scale）
- ofxPanel 保留隐藏，只做 XML save/load 持久化
- 新文件 `src/ImGuiHelpers.h`：ofParameter ↔ ImGui widget 桥接器

**Local addon patches** (不在 repo)：
- `imconfig.h` 加 `#include "ofConstants.h"`
- `BaseEngine.cpp` 加 `#include "ofUtils.h"`

## rp-32 — Dual-window GUI

**Commit**: `git tag rp-32-dual-window`

**What changed vs rp-31**:
- GUI 拆到独立 OS 窗口（440×900）— 主窗口纯 flock 渲染
- `main.cpp` 创建两个 `ofGLFWWindow`，gui window 用 `shareContextWith` 共享 GL
- `ofApp::drawGui(ofEventArgs&)` 订阅 gui window 的 draw 事件
- ImGui 在 drawGui 第一次触发时初始化（确保 listener 绑到 gui window 的 events）
- 主窗口键盘移除 `h`（不再需要切换 GUI）

**Use this checkpoint to**:
- 拖 GUI 到副屏 / 演出环境分离控制
- 全屏 flock 时 GUI 不会盖住视觉

## rp-44 — Event vol 再压低 / Wind 失真修 / Chord 触发改 audio energy

**Commit**: `git tag rp-44-three-fixes`

**1. Event vol floor 0.6 → 0.35**
   - 用户希望低能段事件再轻一些
   - `evtVolStaged = blendRange(energy, eventVol × 0.35, eventVol, ca)`

**2. Wind descent 失真**
   - 根因：wndVol 是 per-buffer 计算（512 samples 一次），conductor descent
     时每 buffer 阶跃下降 → buffer 边界产生 ~86Hz aliased 杂音 → "卡壳"
   - 修：per-sample 1-pole 平滑 `windVolSmooth += (target - smooth) × 0.001`
     时常数 ≈ 16ms @ 44.1kHz → 阶跃被涂抹，听感平滑
   - 新加 audio-thread-local `float windVolSmooth = 0.4f` 在 Synth.h

**3. Chord 不触发**
   - 根因：之前用 `a_conductorValue.load()` 作触发源 — 用户在 FREE mode /
     conductor amount=0 时这个值永远 0.5，永远跨不过 0.7 阈值
   - 修：触发源换成 `a_audioEnergyMeasured.load()` —— 实际播放的 audio
     能量，无论 conductor mode 都在变
   - 阈值默认改：high 0.7→0.55 / low 0.3→0.25 / interval 5s→4s
   - 加**手动 trigger 按钮**（ImGui "Trigger chord change now"）
   - GUI 实时显示：current audio energy / chord idx / peak state /
     since last change → 用户能直接看到为什么没触发

新加 atomic `chordManualTriggerPending` (Synth.h)。

## rp-43 — Drone Chord Progression（peak→calm 触发，voice-leading 衔接）

**Commit**: `git tag rp-43-chord-progression`

**用户需求**：drone 不应该永远停在一个和弦。每次能量激增之后的平静片段
应该切换到下一个 chord，且 chord 之间要有 voice-leading 的连接关系。

**实施**：

1. **4 个 chord template**（手工设计的 voice-leading 序列）：
   - I:    [0, 7, 12, 19]   root, 5th, oct, 5+oct
   - I-2:  [0, 7, 14, 19]   voice 2 上移 +2（小变化）
   - IV:   [0, 5, 14, 19]   voice 1 下移 -2（→ 4 度）
   - VI:   [3, 5, 14, 19]   voice 0 上移 +3（→ b3）
   - 每次切换只动 1 个 voice 1-3 半音 → 自然 voice leading
   - 实际播放时 quantizeToScale 贴回当前 scale

2. **Peak-then-calm 检测**（在 updateClusterVoices 内）：
   ```
   if (energy > chordChangeHigh): peakWasAbove = true
   else if (peakWasAbove && energy < chordChangeLow && enough_time_passed):
       advance chord
   ```
   - 默认阈值：高 0.7 / 低 0.3
   - 默认最小间隔 5 秒（防 oscillation 模式过密切换）

3. **新 chord 应用**：所有 active voice 的 `targetFreq` 同时更新到新 chord
   位置 → 利用现有 `droneGlideMs`（默认 600ms）平滑过渡，不会硬切

4. **新 voice 启动**：当 chord progression 启用时，新进入的 cluster
   按 voice 槽位获取 chord 中对应位置的音；否则保持原 `pickFreshSemitone()`

**新 GUI 控件**（Cluster Drone section 底部）：
- `chord progression` checkbox（默认 OFF，向后兼容）
- `chord trigger high` / `chord trigger low` 阈值
- `chord min interval (s)` 防过密切换
- 显示 `current chord: N / 4`

**测试**：
- 开 conductor amount=1 + score "Figure 2 Arc"（30s ascent → osc → descent）
- 开 chord progression
- ascent 阶段能量爬到 0.7 后触发标记，descent 落到 0.3 时切换到下一个 chord
- 听 drone 从 chord 1 → 2 → 3 → 4 → 1 循环（voice 平滑滑动）

## rp-42 — Event vol 轻微 staging（60% floor）

**Commit**: `git tag rp-42-event-vol-floor`

**用户需求微调**：rp-40 锁定 event vol 不动；rp-41 修了"听不见"问题。
现在用户希望 event vol 在能量平缓时**稍轻**（更柔和的环境），但**不消失**。

**实施**：
- 新 stage `stageEvtVol {0.0, 1.0, sigmoid}`
- `evtVolStaged = blendRange(energy, eventVol × 0.6, eventVol, ca)`
  - 低能：60% × user vol
  - 高能：100% × user vol
  - amount=0：永远 = user vol（向后兼容）
- audioOut 内 event sum 归一化用 evtVolStaged 替代 evtVol

跟 modIndex floor (0.5x) + ratio 永远等用户值 一起，event 在低能段是
"轻、暗、harmonic" 的 bell；高能段是"满、亮、metallic"的 bell。

## rp-41 — Event 永远可听见（撤回 ratio staging + modIndex 加 floor）

**Commit**: `git tag rp-41-event-audibility`

**用户反馈**：rp-40 之后，能量平缓时事件听不见。诊断：rp-40 把 ratio
锚到 1.0（low energy），rp-37 同时把 modIndex 锚到 0（low energy stageFM
0.5-1.0）。两者叠加 → modulator 输出恒为 0 → 最终 voiceSample 是
**纯 carrier sine** → 被 wind 噪声完全掩盖。

**修法**：

1. **撤回 rp-40 的 ratio staging** —— FM ratio 永远等于用户 slider 值
   （之前的"金属化"靠这个，现在让 wave fold + modIndex 改变去做）
2. **modIndex 改用 blendRange + 全程 stage**：
   - `stageFM` 改成 `{0.0, 1.0, sigmoid}`（全程响应）
   - `blend → blendRange(energy, modIndex × 0.5, modIndex, ca)`
   - 低能 floor = 用户 modIndex 的 50%（仍有 bell 音色）
   - 高能 = 用户 modIndex 100%

**结果**：events **永远有 FM bell 音色**，volume 始终等于用户值。
Conductor 在能量轴上调的只有 modIndex 50%-100% + event fold（0-100%）。

**保留**：event volume 永远不被 conductor 调（rp-40 的核心承诺）。

## rp-40 — Event 音色受 conductor 影响（不动音量）

**Commit**: `git tag rp-40-event-timbre`

**用户反馈**：event sound（merge 触发的 FM 钟声）的**音量**不应该被 conductor
影响（因为视觉端每次 merge 都会有 flash，声音对应被压低会不同步）。但**音色**
可以受 conductor 影响 —— 让高能段事件听起来更金属 / 更密集。

**实施**：

| 修饰 | 怎么做 | 效果 |
|---|---|---|
| **FM ratio** 跟能量耦合 | `triggerCollision` 内用 stageRatio (0.3-1.0 sigmoid) 把 ratio 在 `[1.0, userRatio]` 间插值 | 低能：和谐自我调制；高能：用户设的不和谐金属 |
| **Event wave fold** | 新参数 `eventFoldAmount` 0..1，audioOut 算 effEventFold = stageFold.blend(...)，然后 `sinf(eventSum × drive)` | 高能段事件更金属化（独立于 drone fold） |

**未动**：
- `eventVol` / `eventGainPerHit` / `carrierAmp` 都没接 conductor → 音量永远等于
  用户 slider 值。merge flash 跟钟声音量永远 1:1 同步。
- modIndex 仍按 rp-37 的 stageFM 调（视为音色而非音量）。

**新 GUI 控件**：FM (Event Tone) section 多一个 `event fold` slider。

## rp-39 — Score Mode（论文 Figure 2 一键演奏）

**Commit**: `git tag rp-39-score-mode`

**背景**：原 conductor 只能手动选 mode。要复现论文 Figure 2 的完整作品
（ascent → osc+ascent → descent 30秒一气呵成），用户得手动按时间切换。
ScorePlayer 把这个序列**预设化** —— 选 score 一键播放，自动按时间切 events。

**新文件**：
- `src/Score.h/cpp` — Score 数据结构 + ScorePlayer 类

**Score 结构**：
```cpp
struct ScoreEvent {
    float startTime, duration;
    int   mode, curveShape;
    float oscRate, oscDepth;
};
struct Score {
    std::string name;
    std::vector<ScoreEvent> events;
    float totalDuration;
};
```

**内置 3 个 demo score**：
| Score | 时长 | 内容 |
|---|---|---|
| **Figure 2 Arc** | 30s | ascent 8s → ascent_osc 12s → descent 10s |
| **Storm Cycle** | 25s | ascent 3s → oscillation 12s → descent_osc 10s |
| **Quiet Breath** | 45s | 长慢吸气 → 高点呼吸 → 长慢呼气 |

**MorphologyConductor 新增** `softRestart()` 方法 — 只重置 phase，**保留
bridgeOffset** 让 score event 切换走 rp-35 的平滑过渡机制。

**ofApp 接入**：
- update() 头部 `scorePlayer.update(dt, conductor)`（在 conductor.update 之前）
- 写 conductor.mode / curveShape / phaseDuration / oscRate / oscDepth
- 然后 conductor.update 检测 mode change → 捕获 bridge → 平滑过渡
- HUD 顶部多一行显示 "score: ▶ <name> X.Xs"
- Morphology tab 有 Score Playback section（下拉选 score + Play/Stop 按钮）
- 持久化：`score_settings.xml`（记 scoreIdx + looping）

**典型用法**：
- 选 "Figure 2 Arc"，开 loop → 系统永远循环播放论文经典曲线
- 配 conductorAmount=1 (Flock + Synth)，听到 / 看到完整声画 arc

## rp-38 — 视觉 EnergyStage 补齐（声画响应对等）

**Commit**: `git tag rp-38-visual-stages`

**背景**：rp-37 给 Synth 接了 5 个 staged 参数，但 Flock 视觉侧只有 field
force 一个 conductor 目标 —— 声画响应严重不对等，违反论文 trans-modal
要求。这一版补齐两个最直观的视觉参数。

**新增 Flock 视觉响应**：

| 参数 | 窗口 | 曲线 | 范围 | 说明 |
|---|---|---|---|---|
| `particle size mult` | 0.4 – 1.0 | exp | 0.5× – 1.5× | 高能段放大 |
| `matBrightness`      | 0.2 – 1.0 | linear | 0 – userVal | 早段就开始提亮 |

**保留**：field force scalar 沿用 rp-37 之前的 `1 + (cv-0.5)·2·ca` 公式
（multiplier 性质，跟 value-性质参数不一样，没必要重新框）。

**用户体验**：开 conductorAmount=1 跑 ascent：
- 能量 0：粒子小（×0.5）+ 暗（brightness=0）+ field 0
- 能量 0.3：brightness 起步（~0.2），其余还压着
- 能量 0.7：size 起步，brightness 70%，field 0.4×
- 能量 1.0：size 1.5×，brightness 满，field 2×
配合 Synth 端：风声 → drone → cutoff → fold → FM 同步五阶进场
→ 声画**编排同步**完整呈现

**跳过 hue shift**：要写 GLSL HSV 旋转矩阵 + 新 uniform。可作 rp-38.1 或更后。

## rp-37 — Per-parameter EnergyStage（编排式响应，错峰进场）

**Commit**: `git tag rp-37-energy-stages`

**论文背景**：之前 rp-35/36 用**统一 scalar** 喷所有 audio 参数。这是粗暴的，
论文 Spectromorphology 要求每个参数有自己的响应曲线 —— 风声铺底要早进，
drone vol 中段进，FM 金属感留高能段。

**新文件**：
- `src/EnergyStage.h` —— 共享 helper 类。

```cpp
struct EnergyStage {
    float activationStart, activationEnd;  // 激活窗口
    int   curve;                            // 0=linear, 1=exp, 2=log, 3=sigmoid
    float stageOf(energy)        → 0..1 进场强度
    float blend(energy, userVal, ca) → userVal × (1 - ca + ca × stage)
};
```

**Synth 编排表**：
| 参数 | 窗口 | 曲线 | 进场时机 |
|---|---|---|---|
| wind vol  | 0.0 – 0.5 | sigmoid | 最早，铺底 |
| drone vol | 0.3 – 0.7 | sigmoid | 中段，主旋律 |
| cutoff    | 0.2 – 0.9 | log     | 慢慢开亮 |
| FM modIndex | 0.5 – 1.0 | exp   | 晚进，高能段金属感 |
| drone fold  | 0.5 – 1.0 | exp   | 晚进，西海岸 noise |

**向后兼容**：`conductorAmount = 0`（默认）→ 输出永远 = userValue，行为完全
等于 rp-36。`conductorAmount = 1` 才完全用 stage 编排。中间值平滑过渡。

**用户体验改动**：开启 conductorAmount=1 后，conductor=0.5（baseline）不再
是"用户全部 slider 值"，而是"中段编排"（wind 满 + drone 50% + cutoff 75%
+ FM 0% + fold 0%）。这是有意的，是"编排"的代价。

## rp-36 — Synchresis 自感知 + 周期 cadence（系统的"agency"）

**Commit**: `git tag rp-36-synchresis`

**论文背景**：实现 Battey "Fluid Audiovisual Counterpoint" —— 声画大部分时间
独立演化（counterpoint），关键时刻收束到同一轨迹（synchresis cadence）。
这是用户 PhD "negotiation with computational system" 主题的算法化具体：
系统**对自身的状态有感知**，**自主**决定何时强制声画对齐。

**新文件**：
- `src/Synchresis.h/cpp`：自感知 + 周期补偿控制器

**新增的"系统自感知"测量**：
- `Synth::getAudioEnergyMeasured()` — 在 audioOut() 算 RMS，归一化到 [0..1]，
  1-pole smoothing 写入 atomic float；主线程读取
- `Flock3D::getVisualEnergyMeasured()` — 主线程算 density × meanSpeed × meanBrightness
  复合 (0.4 / 0.4 / 0.2 加权)

**Synchresis 控制器**：
- 每帧吃 `(target, audioE, visualE)`，输出 `audioCorrection / visualCorrection`
- Gaussian 脉冲 cadence：每 syncPeriod 秒（默认 30s）一次脉冲，宽度 syncDuration
  （默认 5s），峰值时 sync strength=1
- Sync strength 大 → 用 P-control `nudge = strength × power × (target - measured)`
  把声画往 target 拉
- Sync strength 小（脉冲间）→ nudge≈0，声画自由 drift = counterpoint
- driftTolerance：偏离小于该值时不补偿（避免追噪声）

**ofApp 串接**：
```
conductor.update(dt)     → target
read measured audio/visual energy
synchresis.update(dt, target, audioE, visualE)
audioTarget  = clamp(target + audioCorrection,  0, 1)
visualTarget = clamp(target + visualCorrection, 0, 1)
synth.setConductorValue(audioTarget)
flock.setConductorValue(visualTarget)
```

**GUI**：Morphology tab 下方加 Synchresis section，4 路 PlotLines 显示
target / audio / visual / syncStrength 轨迹（10s history）。HUD 顶部加
sync 状态行 + 颜色高亮（counterpoint 灰 / cadence 暖金）。Help tab 加
所有实测数值。

**默认 OFF**（向后兼容 rp-35）：用户切换 `enabled` 才启用。开启后实测
audio energy = 0.6 但 conductor target = 0.3 时，sync 脉冲到来会把
audioTarget 暂时压到 -0.3 范围 → wind/drone vol 降下来，把实际 audio
energy 拉到 target 附近。脉冲过后又松开。

**Use this checkpoint to**:
- 加更复杂的"系统判断"逻辑（思路 4：surprise injection）
- 训练 co-performer 模型（思路 2）—— history buffer 已经在记录所有信号
- 把 cadence 由"周期触发"改成"事件触发"（如 morphology phase 到 0.5 触发）

## rp-35 — Morphology Conductor（论文 Spectromorphological Synchresis 落地）

**Commit**: `git tag rp-35-morphology-conductor`

**论文背景**：基于 Hal 的论文 *Application of Spectromorphology in Abstract
Audiovisual Composition*，核心概念 **Spectromorphological Synchresis** 主张
"声画沿共享 energy-motion trajectory 协同演化"。这一概念落地实现需要顶层"指挥"
模块产出共享曲线，**同时驱动**音频和视觉参数。

**新增文件**：
- `src/MorphologyConductor.h/cpp`：独立 controller 类

**Conductor 类**：
- 5 种 motion mode：FREE / ASCENT / DESCENT / OSCILLATION / ASCENT_OSC /
  DESCENT_OSC（最后两种是 Smalley 1997 描述的 nested morphology — osc 嵌入
  ascent/descent 内）
- 4 种 curve shape：LINEAR / EXPONENTIAL / LOGARITHMIC / SIGMOID
- 输出 `value()` ∈ [0..1]，0.5 = baseline
- 历史 ring buffer 240 帧（给 ImGui PlotLines 显示 trajectory）
- 触发模式：手动 trigger（按钮重启 phase）+ auto loop

**Nested 实现细节**（论文 page 6 Figure 2 — Hal 的 nested.png 校准）：
ASCENT_OSC 的振荡幅度**随 phase 同步增长**（peak1 < peak2 < ... < peakN），
不是恒定振幅叠在 ramp 上。公式：
```
pcurve   = applyCurve(phase)
effDepth = oscDepth × pcurve         (振幅 0 → max)
center   = pcurve × (1 - effDepth)   (保证 phase=1 peak 触顶 1)
v        = center + effDepth × sin(2π·oscRate·t)
```
边界：phase=0 时 v=0 平静起步；phase=1 时 peak=1 / trough=1-2·depth。
DESCENT_OSC 镜像：振幅 max → 0，结尾平静收场。

**连续性 bridging**：mode 切换 / curve 切换 / auto-loop 循环边界都
有跳变。每次跳变捕获 `bridgeOffset = preValue - newRawValue`，
然后 exp decay（系数 0.92 @ 60fps，~250ms 主体过渡，~1s 残余 1%）
→ 永远无跳变，论文 Figure 2 三段平滑衔接成立。
trigger() 显式重启时清零 bridgeOffset。

**接入 — conductor 缩放的目标参数**：
- **Flock 侧**：`computeFieldForce()` 末尾把 total *= conductorScalar
  （6 个 field force 同时缩放：noise / vortex / spiral / curl / attractor / repeller）
- **Synth 侧（per-buffer 计算一次 audioCondScalar）**：
  - `wndVol = windVol * audioCondScalar`（**wind 音量** — 持续层最直接听感）
  - `cdrVol = clusterDroneVol * audioCondScalar`（**drone 音量** — cluster 在时直接听到）
  - `cutoff = baseCutoff * audioCondScalar`（drone SVF cutoff 亮度）
  - `triggerCollision()` 内 `modIndexInit *= conductorScalar`（FM 事件亮度）

`conductorScalar = 1 + (cv - 0.5) * 2 * conductorAmount`
- amount=0 → scalar 恒为 1（不影响，rp-34 兼容）
- amount=1, cv=1 → scalar=2（双倍）
- amount=1, cv=0 → scalar=0（归零 — DESCENT 时声音 / field 完全停）
- `ofApp::update()` 每帧 `conductor.update(dt)` → 把 value 推给 flock + synth
- 新 Morphology tab 在 ImGui 中（独立窗口里），加 PlotLines trajectory 可视化
- 顶部 HUD 加一行显示 morphology mode + value + phase
- 新文件 `morphology_settings.xml` 持久化 conductor 状态

**向后兼容**：`conductorAmount` 默认 0（不影响任何参数）→ build 完跑起来感觉
跟 rp-34 完全一样。要启用 conductor，调 Flock 和 Synth 的 conductor amount
slider 到 > 0，再选一个 motion mode（如 ascent）。

**Use this checkpoint to**:
- 用 Score Mode / counterpoint 等 P1+ 功能往下扩
- 把 conductor 拓展为多通道（分别给 Flock 和 Synth 不同的 trajectory，
  实现论文 audiovisual counterpoint 节）

## rp-34 — ImGui widget ID collision fix

**Commit**: `git tag rp-34-imgui-id-fix`

**Bug**: 改 wind vol 时 cluster drone vol 也跟着变，反之亦然。原因是
ImGui 默认用 widget label 计算 widget ID。多个 ofParameter 同名
（"vol"、"attack (ms)"、"resonance"）→ widget ID 碰撞 → 拖一个
带动另一个。XML 持久化没事（ofParameter 实例独立），纯 ImGui 显示
层 bug。

**Fix**: `ImGuiHelpers.h` 里所有 widget helper 用 `ofParameter`
实例的内存地址做 ID 命名空间（`ImGui::PushID((void*)&p)`）。每个
ofParameter 地址唯一 → ID 唯一。display label 保持原状，drawImGui
调用代码完全不动。

## rp-33 — 粒子软边 + 性能优化

**Commit**: `git tag rp-33-aa-perf`

**视觉质量（毛边消除）**:
- 根因：`GL_POINTS` 不受 MSAA 影响，硬 `discard` 产生像素级锯齿
- Fragment shader 用 `fwidth(d)` 做屏幕空间软边（自适应 zoom，1-2 像素渐变）
- Sphere → halo 用 `smoothstep` 软化（去掉可见的"圈"）
- 无分支 sphere shading（mask 软化代替 `if`），GPU pipeline 更稳定
- `clamp(fwidth, 0.001, 0.08)` 防止小粒子（<4px）被过度吞掉

**性能（无质量损失）**:
- XorShift32 PRNG 替代 `std::rand()` — boid loop 200K calls/frame，
  std::rand ~30ns（libc mutex + LCG）→ XorShift ~1ns，省 ~5ms/frame
- Trail push 用 `if (++idx >= MAX) idx=0` 替代整数 modulo
- PRNG state seeded in `setup()` 用 `ofGetElapsedTimeMicros()`

**Use this checkpoint to**:
- 任何"粒子有锯齿/毛边"复发的回滚基线
- 性能调优实验起点（如果还有更多 perf 工作要做）







rp-34（ImGui ID fix）论文前版本

想做一个spectromorphology sequencer 能够衔接自选的8个morphology并且loop
以及你需要判断morphology是否可以衔接，