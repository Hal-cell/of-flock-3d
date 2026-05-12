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
