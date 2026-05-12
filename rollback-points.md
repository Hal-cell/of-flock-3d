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
