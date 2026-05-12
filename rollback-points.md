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

## (planned) rp-01 — Audio synth v1 (A + B + D)

DroneLayer + cluster-voice polyphony + modal feedback reverb, all inline C++ with ofSoundStream.
