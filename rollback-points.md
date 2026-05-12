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
