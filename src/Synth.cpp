#include "Synth.h"

//==============================================================
//  Setup
//==============================================================
void Synth::setup(int sr, int bs){
	sampleRate = sr;
	bufferSize = bs;

	// 初始化 4 条 hall reverb delay line（长度对应 hall 空间尺度）
	// 用质数 ms 数避免反馈周期对齐；4 条 delay 长度跨度比例约 1:1.25:1.55:1.86
	// → 在 Hadamard 混合下产生密集 ill-correlated 反射
	float delayMs[NUM_REVERB_DELAYS] = {152.0f, 191.0f, 234.0f, 283.0f};
	for (int i = 0; i < NUM_REVERB_DELAYS; i++) {
		int n = std::max(1, (int)(delayMs[i] * 0.001f * sampleRate));
		delayLength[i] = n;
		delayBuf[i].assign(n, 0.0f);
		delayWrite[i] = 0;
		dampLpState[i] = 0.0f;
	}

	// Pre-delay buffer：分配 250ms 容量（覆盖 GUI 范围 0..200ms）
	preDelayBufLen = (int)(0.25f * sampleRate);
	preDelayBuf.assign(preDelayBufLen, 0.0f);
	preDelayWrite = 0;
}

void Synth::buildGui(ofParameterGroup& group){
	group.setName("Synth");
	group.add(audioEnabled.set("audio ON",       true));
	group.add(masterVol.set("masterVol",         0.5f, 0.0f, 1.0f));
	group.add(eventVol.set("eventVol",           0.6f, 0.0f, 1.0f));
	group.add(eventDecayMs.set("decay (ms)",    50.0f, 5.0f, 500.0f));
	group.add(eventAttackMs.set("attack (ms)",   2.0f, 0.1f, 50.0f));
	group.add(eventGainPerHit.set("hit gain",    0.5f, 0.05f, 1.5f));
	group.add(minMassToFire.set("minMass",       0.0f, 0.0f, 50.0f));
	group.add(eventQuantize.set("quantize",      true));
	// ─── Hall Reverb ───
	group.add(reverbAmt.set("reverbAmt",         0.55f, 0.0f, 1.0f));      // 干湿比
	group.add(reverbSize.set("reverbSize",       0.85f, 0.0f, 0.97f));     // 长尾控制（0=无反馈，0.97=极长）
	group.add(reverbDamp.set("reverbDamp",       0.5f,  0.0f, 0.99f));     // HF 衰减
	group.add(reverbPreDelayMs.set("reverb preDelay (ms)", 20.0f, 0.0f, 200.0f));
	group.add(rootFreq.set("rootFreq (Hz)",      110.0f, 55.0f, 440.0f));
	group.add(scaleType.set("scale",             0, 0, int(SCALE_COUNT) - 1));

	// ─── FM 子组（2-op：carrier + modulator）───
	// 一些典型 ratio 玩法：
	//   1.0      → carrier 自我调制，方波感
	//   2.0      → 奇数谐波（clarinet 风）
	//   3.0/4.0  → 整数倍（bright but harmonic）
	//   3.5/4.5  → 非整数倍（bell-like 金属）
	//   7.0/7.5  → 经典"DX7 bell"
	//   0.5      → 低于 carrier，sub-harmonic 感
	fmGroup.setName("FM");
	fmGroup.add(fmRatio.set("FM ratio",          2.0f,  0.5f, 8.0f));    // 自动 snap 到 0.5 倍数
	fmGroup.add(fmIndex.set("FM index",          3.0f,  0.0f, 12.0f));
	fmGroup.add(fmIndexDecayMs.set("FM idxDecay (ms)",  40.0f, 1.0f, 500.0f));
	group.add(fmGroup);

	// ─── Cluster Drone 子组（saw + SVF lowpass + chord 优先 pitch）───
	clusterDroneGroup.setName("ClusterDrone");
	clusterDroneGroup.add(clusterDroneVol.set("vol",          0.5f,   0.0f,   1.0f));
	clusterDroneGroup.add(clusterAttackMs.set("attack (ms)",  800.0f, 50.0f, 4000.0f));
	clusterDroneGroup.add(clusterReleaseMs.set("release (ms)", 1500.0f, 50.0f, 6000.0f));
	clusterDroneGroup.add(clusterDetune.set("detune",         0.008f, 0.0f,  0.03f));
	clusterDroneGroup.add(clusterProximity.set("proximity",   80.0f,  10.0f, 400.0f));
	clusterDroneGroup.add(clusterCutoff.set("cutoff (Hz)",    600.0f, 80.0f, 8000.0f));
	clusterDroneGroup.add(clusterResonance.set("resonance",   0.3f,   0.0f,  0.95f));
	group.add(clusterDroneGroup);
}

//--------------------------------------------------------------
//  主线程：每次碰撞推一个 TriggerEvent 到 ring buffer
//--------------------------------------------------------------
void Synth::triggerCollision(const Flock3D::CollisionEvent& ev){
	if (ev.newMass < minMassToFire) return;

	float wr = a_worldRadius.load();

	// 基频（fundamental P0）
	float freq;
	if (eventQuantize) {
		freq = massToFreq(ev.newMass);
	} else {
		float l = log10f(std::max(ev.newMass, 1.0f));
		float t = ofClamp((l - 0.5f) / 2.0f, 0.0f, 1.0f);
		t = 1.0f - t;
		freq = rootFreq * powf(2.0f, t * 3.0f);
	}

	// Accent: 高八度（freq * 2）+ 振幅小幅 boost，让重音突出
	float gain = eventGainPerHit;
	if (ev.isAccent) {
		freq *= 2.0f;        // +1 octave
		gain *= 1.3f;        // 30% 更响
	}

	// pan：预算成 equal-power 系数
	float panPos = ofClamp(ev.pos.x / (wr * 2.0f) + 0.5f, 0.0f, 1.0f);
	float panL = cosf(panPos * HALF_PI);
	float panR = sinf(panPos * HALF_PI);

	// P0 基础衰减：每 sample 指数衰减系数 = exp(-1 / (T_samples))
	float decaySamples = eventDecayMs * 0.001f * sampleRate;
	float baseDecay = expf(-1.0f / std::max(decaySamples, 1.0f));

	// 亮度（particle 颜色亮度，0..1）
	float brightness = ofClamp((ev.color.r + ev.color.g + ev.color.b) / 3.0f, 0.0f, 1.0f);

	// 起音 ramp（从 GUI ms 转 samples）
	int attackSamples = std::max(1, (int)(eventAttackMs * 0.001f * sampleRate));

	// ─── 主线程预计算 FM 参数 ───
	float nyquist = sampleRate * 0.45f;

	// fmRatio: GUI float → snap 到最近 0.5 倍数
	float rawRatio = fmRatio.get();
	float snappedRatio = roundf(rawRatio * 2.0f) / 2.0f;
	if (snappedRatio < 0.5f) snappedRatio = 0.5f;

	// Carrier
	float carrierFreq = std::min(freq, nyquist);

	// Modulator
	float modFreqRaw = freq * snappedRatio;
	float modFreq = std::min(modFreqRaw, nyquist);

	// modIndex: 用 brightness 缩放（暗色 0.3x，亮色 1.0x → 暗 = 纯 sine，亮 = 金属）
	float modIndexScale = 0.3f + brightness * 0.7f;
	float modIndexInit  = fmIndex.get() * modIndexScale;

	// Accent 命中时也让 modIndex 增加（更明亮的重音）
	if (ev.isAccent) modIndexInit *= 1.5f;

	// modIndex 衰减（独立于 carrier）
	float modIdxDecaySamples = fmIndexDecayMs.get() * 0.001f * sampleRate;
	float modIndexDecay = expf(-1.0f / std::max(modIdxDecaySamples, 1.0f));

	TriggerEvent te;
	te.carrierFreq     = carrierFreq;
	te.modFreq         = modFreq;
	te.carrierAmp      = gain;
	te.carrierDecay    = baseDecay;
	te.modIndex        = modIndexInit;
	te.modIndexDecay   = modIndexDecay;
	te.panL            = panL;
	te.panR            = panR;
	te.attackSamples   = attackSamples;

	int w = ringWrite.load(std::memory_order_relaxed);
	int next = (w + 1) & (RING_SIZE - 1);
	if (next == ringRead.load(std::memory_order_acquire)) {
		return;
	}
	eventRing[w] = te;
	ringWrite.store(next, std::memory_order_release);
}

//--------------------------------------------------------------
//  HUD 用：当前活跃的 drone voice 数
//  - 包括 attack/sustain/release 各阶段
//  - 主线程读 atomic（音频侧设置）
//--------------------------------------------------------------
int Synth::getActiveDroneCount() const {
	int count = 0;
	for (int i = 0; i < NUM_DRONE_VOICES; i++) {
		if (droneVoices[i].active.load()) count++;
	}
	return count;
}

//==============================================================
//  Cluster Drone — 主线程：和声优先级 + 不重复的 pitch 分配
//  - chord 优先级（按典型 chord 排序，先 root/octave/5th，再 3rd 等）
//  - 优先级中跳过被任一活 voice 占用的半音
//  - 拿到候选半音后再 quantize 到当前 scale（与 events 共调）
//==============================================================
int Synth::pickFreshSemitone() const {
	// chord-priority 半音偏移（从 rootFreq 起算）
	// 经典 chord：root, 8va, 5th, 8va+5th, 2 octs, M3, m3, 11, 7th, M6...
	// 含负值（去低于 root 的位置）增加变化
	static const int priority[] = {
		0, 12, 7, 24, 19,                  // root + 5th 系
		4, 16, 3, 15,                      // M3 / m3 + 8va
		11, 23, 10, 22,                    // M7 / m7
		9, 21, 2, 14,                      // M6 / M2
		-12, -5, 5, 17,                    // 8va 下、4th 系
		36, -24                            // 极端高低（fallback）
	};
	const int nPriority = sizeof(priority) / sizeof(priority[0]);

	// 收集已用半音
	std::vector<int> used;
	used.reserve(NUM_DRONE_VOICES);
	for (int i = 0; i < NUM_DRONE_VOICES; i++) {
		if (droneTracking[i].active) {
			used.push_back(droneTracking[i].semitone);
		}
	}

	for (int k = 0; k < nPriority; k++) {
		int candidate = priority[k];

		// 量化到当前 scale（与 event 同调）
		float root = rootFreq;
		float candFreq = root * powf(2.0f, candidate / 12.0f);
		float quantFreq = quantizeToScale(candFreq);
		int quantSemi = (int)roundf(12.0f * log2f(quantFreq / root));

		bool isUsed = false;
		for (int u : used) {
			if (u == quantSemi) { isUsed = true; break; }
		}
		if (!isUsed) return quantSemi;
	}

	// 全占满（极少见）→ 找一个还没用的高位
	int maxUsed = -100;
	for (int u : used) if (u > maxUsed) maxUsed = u;
	return maxUsed + 12;
}

//==============================================================
//  Cluster Drone — 主线程：根据 cluster 列表分配 / 匹配 / 释放 voice
//  - 匹配规则：cluster 与"已激活 voice 的 trackedPos" 邻近 → 复用
//  - 没匹配：分配空闲 voice，新计算 pitch（quantize 到 scale）
//  - 没新 cluster 跟踪的 voice → 启动 fadeout 倒计时
//  - fadeout 倒计时完 → 释放 voice 槽
//==============================================================
void Synth::updateClusterVoices(const std::vector<Flock3D::Cluster>& clusters, float wr){
	a_worldRadius.store(wr);

	std::array<bool, NUM_DRONE_VOICES> matched{};
	matched.fill(false);

	float proximity = clusterProximity.get();
	float proxSq = proximity * proximity;

	// ─── Pass 1: 给每个 cluster 找 voice ───
	for (const auto& c : clusters) {
		// (a) 找邻近已激活 voice
		int bestIdx = -1;
		float bestDistSq = proxSq;
		for (int i = 0; i < NUM_DRONE_VOICES; i++) {
			if (matched[i]) continue;
			if (!droneTracking[i].active) continue;
			glm::vec3 d = droneTracking[i].trackedPos - c.centroid;
			float distSq = glm::dot(d, d);
			if (distSq < bestDistSq) {
				bestDistSq = distSq;
				bestIdx = i;
			}
		}

		// (b) 没邻近 → 分配空闲槽（活但不需是当前正激活的）
		bool isNewVoice = false;
		if (bestIdx < 0) {
			for (int i = 0; i < NUM_DRONE_VOICES; i++) {
				if (!matched[i] && !droneTracking[i].active) {
					bestIdx = i;
					isNewVoice = true;
					break;
				}
			}
		}

		if (bestIdx < 0) continue;   // 全部 voice 被占 → 这个 cluster 暂时没声音

		auto& tr = droneTracking[bestIdx];
		auto& v  = droneVoices[bestIdx];

		if (isNewVoice) {
			// 新 voice：用和声优先级分配未占用的半音
			tr.active = true;
			int semi = pickFreshSemitone();
			tr.semitone = semi;
			float freq = rootFreq * powf(2.0f, semi / 12.0f);

			// CRITICAL：重置音频线程本地状态，避免上次 voice 残留导致 attack 从中间值开始
			// 此时 active 还是 false（audio thread 不会读这些字段），安全写入
			v.currentVol  = 0.0f;       // attack 必须从 0 起
			v.currentFreq = freq;       // 不要 glide，直接到目标频率
			v.currentPan  = 0.5f;       // 等下面 targetPan 设置后会平滑过渡
			v.phase[0]    = 0.0f;
			v.phase[1]    = 0.333f;
			v.phase[2]    = 0.666f;
			v.svfLow      = 0.0f;
			v.svfBand     = 0.0f;

			v.targetFreq.store(freq);
			// 最后才设 active=true，确保音频线程读到的是完整初始化的 voice
			v.active.store(true);
		}
		// 更新跟踪位置 + 目标音量 + pan
		tr.trackedPos = c.centroid;
		tr.fadeoutSec = 0.0f;   // 重置 fadeout（如果之前在淡出，rebound 回 sustain）
		v.targetVol.store(1.0f);
		float pan = ofClamp(c.centroid.x / (wr * 2.0f) + 0.5f, 0.0f, 1.0f);
		v.targetPan.store(pan);
		matched[bestIdx] = true;
	}

	// ─── Pass 2: 没匹配上的活 voice → fadeout（时间精准）───
	// 用实际 frame time 而不是假设 60fps，避免帧率波动让 audio 还没到 0 就被切断
	float dt = ofGetLastFrameTime();
	if (dt > 0.1f) dt = 0.1f;
	float releaseSec = clusterReleaseMs.get() * 0.001f;
	// 多给 5% 余量，确保音频侧 envelope 真的到 0 后才释放 voice
	float releaseWithMargin = releaseSec * 1.05f;

	for (int i = 0; i < NUM_DRONE_VOICES; i++) {
		if (matched[i]) continue;
		if (!droneTracking[i].active) continue;

		if (droneTracking[i].fadeoutSec <= 0.0f) {
			// 启动 fadeout
			droneVoices[i].targetVol.store(0.0f);
			droneTracking[i].fadeoutSec = releaseWithMargin;
		} else {
			droneTracking[i].fadeoutSec -= dt;
			if (droneTracking[i].fadeoutSec <= 0.0f) {
				// 完全淡出 → 释放（此时 audio envelope 已严格归零，无 cliff）
				droneTracking[i].active = false;
				droneVoices[i].active.store(false);
			}
		}
	}
}

//==============================================================
//  音频线程：生成样本
//==============================================================
void Synth::audioOut(ofSoundBuffer& buffer){
	int n = buffer.getNumFrames();
	int ch = buffer.getNumChannels();
	if (ch < 2) return;

	if (!audioEnabled) {
		// 输出静音
		for (int i = 0; i < n * ch; i++) buffer[i] = 0.0f;
		return;
	}

	float master = masterVol;
	float verbAmt = reverbAmt;
	float evtVol = eventVol;

	// ─── 把 ring buffer 里的 trigger event 分配给空闲 event voices ───
	int r = ringRead.load(std::memory_order_relaxed);
	int wEnd = ringWrite.load(std::memory_order_acquire);
	while (r != wEnd) {
		const TriggerEvent& te = eventRing[r];

		// 找空闲 voice，或偷 carrier amp 最小的
		int targetIdx = -1;
		float minAmp = 1e9;
		for (int i = 0; i < NUM_EVENT_VOICES; i++) {
			if (!eventVoices[i].active) { targetIdx = i; break; }
			if (eventVoices[i].carrierAmp < minAmp) {
				minAmp = eventVoices[i].carrierAmp;
				targetIdx = i;
			}
		}
		if (targetIdx >= 0) {
			auto& v = eventVoices[targetIdx];
			v.active         = true;
			v.attackCounter  = 0;
			v.attackSamples  = te.attackSamples;
			v.panL           = te.panL;
			v.panR           = te.panR;

			v.carrierFreq    = te.carrierFreq;
			v.carrierPhase   = 0.0f;
			v.carrierAmp     = te.carrierAmp;
			v.carrierDecay   = te.carrierDecay;

			v.modFreq        = te.modFreq;
			v.modPhase       = 0.123f;     // 错开避免 click
			v.modIndex       = te.modIndex;
			v.modIndexDecay  = te.modIndexDecay;
		}
		r = (r + 1) & (RING_SIZE - 1);
	}
	ringRead.store(r, std::memory_order_release);

	// ─── 预算 SVF cutoff / resonance 系数（per-buffer，每 sample 不变）───
	float cutoff = ofClamp(clusterCutoff.get(), 20.0f, sampleRate * 0.4f);
	float svfFc = 2.0f * sinf(PI * cutoff / sampleRate);
	if (svfFc > 0.99f) svfFc = 0.99f;
	float svfQ  = 1.0f - ofClamp(clusterResonance.get(), 0.0f, 0.95f);
	if (svfQ < 0.05f) svfQ = 0.05f;

	// 线性包络：每 sample 增量 = 1 / (attackMs * sampleRate / 1000)
	// 精准时长：经过 attackMs 后 currentVol 严格 = 1.0；release 同理严格 = 0.0
	// 避免指数包络在固定时间内只到 0.37 → 被 fadeoutFrames 掐断的 cliff
	float attackSamples  = std::max(1.0f, clusterAttackMs.get()  * 0.001f * sampleRate);
	float releaseSamples = std::max(1.0f, clusterReleaseMs.get() * 0.001f * sampleRate);
	float attackPerSample  = 1.0f / attackSamples;
	float releasePerSample = 1.0f / releaseSamples;
	float detune = clusterDetune;
	float detuneRatios[3] = {1.0f, 1.0f + detune, 1.0f - detune};

	for (int i = 0; i < n; i++) {
		float left = 0, right = 0;

		// ───────────────────────────────
		// A. Cluster Drone — 多声部，每 voice = 3 detuned saw + SVF lowpass
		// ───────────────────────────────
		{
			float cdrVol = clusterDroneVol;
			float cdrSumL = 0, cdrSumR = 0;
			for (int v = 0; v < NUM_DRONE_VOICES; v++) {
				auto& dv = droneVoices[v];
				if (!dv.active.load()) continue;

				float tVol  = dv.targetVol.load();
				float tFreq = dv.targetFreq.load();
				float tPan  = dv.targetPan.load();

				// 线性包络（精准时长，到点严格归零/满值）
				if (tVol > dv.currentVol) {
					dv.currentVol += attackPerSample;
					if (dv.currentVol > tVol) dv.currentVol = tVol;
				} else if (tVol < dv.currentVol) {
					dv.currentVol -= releasePerSample;
					if (dv.currentVol < tVol) dv.currentVol = tVol;
				}

				dv.currentFreq += (tFreq - dv.currentFreq) * 0.0005f;
				dv.currentPan  += (tPan - dv.currentPan)  * 0.001f;

				if (dv.currentVol <= 0.0f && tVol <= 0.0f) continue;

				// 3 detuned saws 叠加（PolyBLEP 抗混叠）
				float rawSample = 0;
				for (int s = 0; s < 3; s++) {
					float f = dv.currentFreq * detuneRatios[s];
					if (f > sampleRate * 0.45f) f = sampleRate * 0.45f;
					float dt = f / sampleRate;
					rawSample += saw(dv.phase[s], dt);
					dv.phase[s] += dt;
					if (dv.phase[s] >= 1.0f) dv.phase[s] -= 1.0f;
				}
				rawSample *= (1.0f / 3.0f);   // 3 saws 归一化

				// SVF state-variable lowpass（per-voice 独立滤波）
				dv.svfLow  += svfFc * dv.svfBand;
				float svfHigh = rawSample - dv.svfLow - svfQ * dv.svfBand;
				dv.svfBand += svfFc * svfHigh;
				float filtered = dv.svfLow;

				float sample = filtered * dv.currentVol;

				float panL = cosf(dv.currentPan * HALF_PI);
				float panR = sinf(dv.currentPan * HALF_PI);
				cdrSumL += sample * panL;
				cdrSumR += sample * panR;
			}
			cdrSumL *= cdrVol * (2.0f / NUM_DRONE_VOICES);
			cdrSumR *= cdrVol * (2.0f / NUM_DRONE_VOICES);
			left  += cdrSumL;
			right += cdrSumR;
		}

		// ───────────────────────────────
		// B. EventVoices — 短促粒子触发声（2-op FM 合成）
		//   carrier 承载振幅包络，modulator 调制 carrier 相位
		//   modIndex 独立衰减 → 自然 bell envelope（亮起音 → 温暖尾音）
		// ───────────────────────────────
		float eventSumL = 0, eventSumR = 0;
		for (int v = 0; v < NUM_EVENT_VOICES; v++) {
			auto& vc = eventVoices[v];
			if (!vc.active) continue;

			// Attack envelope（防起音 click）
			float envAttack = 1.0f;
			if (vc.attackCounter < vc.attackSamples) {
				envAttack = (float)vc.attackCounter / (float)vc.attackSamples;
				vc.attackCounter++;
			}

			// Modulator
			float modSample = sinf(vc.modPhase * TWO_PI) * vc.modIndex;
			vc.modPhase += vc.modFreq / sampleRate;
			if (vc.modPhase >= 1.0f) vc.modPhase -= 1.0f;
			vc.modIndex *= vc.modIndexDecay;

			// Carrier with phase modulation
			float voiceSample = sinf(vc.carrierPhase * TWO_PI + modSample) * vc.carrierAmp;
			vc.carrierPhase += vc.carrierFreq / sampleRate;
			if (vc.carrierPhase >= 1.0f) vc.carrierPhase -= 1.0f;
			vc.carrierAmp *= vc.carrierDecay;

			if (vc.carrierAmp < 0.00001f) {
				vc.active = false;
				continue;
			}

			voiceSample *= envAttack;

			eventSumL += voiceSample * vc.panL;
			eventSumR += voiceSample * vc.panR;
		}
		// 归一化：除以 voice 数量
		eventSumL *= evtVol * (2.0f / NUM_EVENT_VOICES);
		eventSumR *= evtVol * (2.0f / NUM_EVENT_VOICES);
		left  += eventSumL;
		right += eventSumR;

		// ───────────────────────────────
		// D. Hall Reverb — Hadamard FDN + HF damping + pre-delay
		// ───────────────────────────────
		if (verbAmt > 0.001f) {
			// 干信号（pre-delay 前）
			float inMix = (left + right) * 0.5f;

			// Pre-delay：input 先延迟，再喂给 reverb（"距离感"）
			int preDelaySamples = (int)(reverbPreDelayMs.get() * 0.001f * sampleRate);
			if (preDelaySamples < 0) preDelaySamples = 0;
			if (preDelaySamples >= preDelayBufLen) preDelaySamples = preDelayBufLen - 1;

			preDelayBuf[preDelayWrite] = inMix;
			int preReadIdx = preDelayWrite - preDelaySamples;
			if (preReadIdx < 0) preReadIdx += preDelayBufLen;
			float reverbInput = preDelayBuf[preReadIdx];
			preDelayWrite = (preDelayWrite + 1) % preDelayBufLen;

			// 读 4 条 delay 当前输出
			float d[NUM_REVERB_DELAYS];
			for (int k = 0; k < NUM_REVERB_DELAYS; k++) {
				d[k] = delayBuf[k][delayWrite[k]];
			}

			// HF damping（每条 delay 自己的 1-pole lowpass）
			// damp=0 → 系数=1（无衰减，bright）；damp=1 → 系数=0（深度衰减，dark）
			float dampCoef = 1.0f - reverbDamp.get();
			if (dampCoef < 0.01f) dampCoef = 0.01f;
			float damped[NUM_REVERB_DELAYS];
			for (int k = 0; k < NUM_REVERB_DELAYS; k++) {
				dampLpState[k] += dampCoef * (d[k] - dampLpState[k]);
				damped[k] = dampLpState[k];
			}

			// Hadamard 4×4 mix（能量守恒的"旋转"，把 4 条 delay 互相散开）
			// h0 =  d0 + d1 + d2 + d3 } 都 *0.5
			// h1 =  d0 - d1 + d2 - d3
			// h2 =  d0 + d1 - d2 - d3
			// h3 =  d0 - d1 - d2 + d3
			float h[4];
			h[0] = (damped[0] + damped[1] + damped[2] + damped[3]) * 0.5f;
			h[1] = (damped[0] - damped[1] + damped[2] - damped[3]) * 0.5f;
			h[2] = (damped[0] + damped[1] - damped[2] - damped[3]) * 0.5f;
			h[3] = (damped[0] - damped[1] - damped[2] + damped[3]) * 0.5f;

			// 写回 delay buffer：input + feedback × mixed
			float feedback = reverbSize.get();
			if (feedback > 0.97f) feedback = 0.97f;   // 防爆
			for (int k = 0; k < NUM_REVERB_DELAYS; k++) {
				delayBuf[k][delayWrite[k]] = reverbInput + h[k] * feedback;
				delayWrite[k] = (delayWrite[k] + 1) % delayLength[k];
			}

			// 立体声 spread：d0+d2 → L, d1+d3 → R
			float verbL = (d[0] + d[2]) * 0.5f;
			float verbR = (d[1] + d[3]) * 0.5f;

			left  += verbL * verbAmt;
			right += verbR * verbAmt;
		}

		// ───────────────────────────────
		// Master limiter + 输出
		// ───────────────────────────────
		left  = tanhf(left  * master);
		right = tanhf(right * master);

		buffer[i * ch + 0] = left;
		buffer[i * ch + 1] = right;
	}
}

//==============================================================
//  音阶 / 频率映射
//==============================================================
float Synth::quantizeToScale(float freq) const {
	// 把任意频率量化到当前 scale 的最近一个音
	// 各 scale 用半音 offset 表示（相对 root）
	static const std::vector<std::vector<int>> scales = {
		{0, 3, 5, 7, 10},        // pentatonic minor: A C D E G
		{0, 2, 4, 6, 7, 9, 11},  // Lydian
		{0, 2, 4, 6, 8, 10},     // whole tone
		{0, 12, 19, 24, 28, 31}, // harmonic series intervals (oct, 5th, oct, maj3, 5th)
	};
	int sIdx = ofClamp((int)scaleType, 0, (int)scales.size() - 1);
	const auto& scale = scales[sIdx];

	float root = rootFreq;
	float semis = 12.0f * log2f(freq / root);   // 当前频率离 root 多少半音
	int octave = (int)floorf(semis / 12.0f);
	float semInOct = semis - octave * 12;

	// 找最近的 scale step
	int bestStep = scale[0];
	float bestDist = 100.0f;
	for (int step : scale) {
		float d = fabsf(step - semInOct);
		if (d < bestDist) { bestDist = d; bestStep = step; }
	}

	float quantized = octave * 12 + bestStep;
	return root * powf(2.0f, quantized / 12.0f);
}

float Synth::massToFreq(float mass) const {
	// 质量 3..hundreds → 把对数映射到 [低频, 高频]
	// log10(3)=0.48, log10(300)=2.48
	float l = log10f(std::max(mass, 1.0f));
	float t = ofClamp((l - 0.5f) / 2.0f, 0.0f, 1.0f);   // 0..1
	// 反向：mass 大 → 低音
	t = 1.0f - t;
	float root = rootFreq;
	float octaves = 3.0f;
	float raw = root * powf(2.0f, t * octaves);
	return quantizeToScale(raw);
}

//==============================================================
//  PolyBLEP saw（抗混叠）
//==============================================================
float Synth::polyBlep(float t, float dt) const {
	if (t < dt) {
		t /= dt;
		return t + t - t * t - 1.0f;
	} else if (t > 1.0f - dt) {
		t = (t - 1.0f) / dt;
		return t * t + t + t + 1.0f;
	}
	return 0.0f;
}

float Synth::saw(float phase, float dt) const {
	float v = 2.0f * phase - 1.0f;
	v -= polyBlep(phase, dt);
	return v;
}
