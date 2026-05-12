#include "Synth.h"

//==============================================================
//  Setup
//==============================================================
void Synth::setup(int sr, int bs){
	sampleRate = sr;
	bufferSize = bs;

	// 初始化 4 条 delay line（ModalReverb）
	// 长度为 prime 数（避免反馈周期对齐）
	int lens[4] = {1499, 1789, 2089, 2531};
	for (int i = 0; i < 4; i++) {
		delayLength[i] = lens[i];
		delayBuf[i].assign(lens[i], 0.0f);
		delayWrite[i] = 0;
	}
}

void Synth::buildGui(ofParameterGroup& group){
	group.setName("Synth");
	group.add(audioEnabled.set("audio ON",       true));
	group.add(masterVol.set("masterVol",         0.5f, 0.0f, 1.0f));
	group.add(droneVol.set("droneVol",           0.4f, 0.0f, 1.0f));
	group.add(eventVol.set("eventVol",           0.6f, 0.0f, 1.0f));
	group.add(eventDecayMs.set("decay (ms)",    50.0f, 5.0f, 500.0f));
	group.add(eventAttackMs.set("attack (ms)",   2.0f, 0.1f, 50.0f));
	group.add(eventGainPerHit.set("hit gain",    0.5f, 0.05f, 1.5f));
	group.add(minMassToFire.set("minMass",       0.0f, 0.0f, 50.0f));
	group.add(eventQuantize.set("quantize",      true));
	group.add(reverbAmt.set("reverbAmt",         0.45f, 0.0f, 1.0f));
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

	// ─── Cluster Drone 子组 ───
	clusterDroneGroup.setName("ClusterDrone");
	clusterDroneGroup.add(clusterDroneVol.set("vol",          0.5f,   0.0f,   1.0f));
	clusterDroneGroup.add(clusterAttackMs.set("attack (ms)",  800.0f, 50.0f, 4000.0f));
	clusterDroneGroup.add(clusterReleaseMs.set("release (ms)", 1500.0f, 50.0f, 6000.0f));
	clusterDroneGroup.add(clusterDetune.set("detune",         0.005f, 0.0f,  0.02f));
	clusterDroneGroup.add(clusterProximity.set("proximity",   80.0f,  10.0f, 400.0f));
	group.add(clusterDroneGroup);
}

//==============================================================
//  主线程：更新 drone / clusters
//==============================================================
void Synth::updateStats(const Flock3D::Stats& s, float worldRadius){
	a_aliveRatio.store(s.aliveRatio);
	a_meanSpeed.store(s.meanSpeed);
	a_spread.store(s.spread);
	a_fieldNoise.store(s.fieldNoise);
	a_fieldVortex.store(s.fieldVortex);
	a_fieldSpiral.store(s.fieldSpiral);
	a_fieldCurl.store(s.fieldCurl);
	a_fieldAttract.store(s.fieldAttractor);
	a_fieldRepel.store(s.fieldRepeller);
	a_worldRadius.store(worldRadius);
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

//==============================================================
//  Cluster Drone — 主线程：根据 cluster 列表分配 / 匹配 / 释放 voice
//  - 匹配规则：cluster 与"已激活 voice 的 trackedPos" 邻近 → 复用
//  - 没匹配：分配空闲 voice，新计算 pitch（quantize 到 scale）
//  - 没新 cluster 跟踪的 voice → 启动 fadeout 倒计时
//  - fadeout 倒计时完 → 释放 voice 槽
//==============================================================
void Synth::updateClusterVoices(const std::vector<Flock3D::Cluster>& clusters, float wr){
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
			// 新 voice：选音高（quantize 到 scale，基于 mass）
			tr.active = true;
			v.active.store(true);
			v.targetFreq.store(massToFreq(c.totalMass));
		}
		// 更新跟踪位置 + 目标音量 + pan
		tr.trackedPos = c.centroid;
		tr.fadeoutFrames = 0;   // 重置 fadeout（如果之前在淡出）
		v.targetVol.store(1.0f);
		float pan = ofClamp(c.centroid.x / (wr * 2.0f) + 0.5f, 0.0f, 1.0f);
		v.targetPan.store(pan);
		matched[bestIdx] = true;
	}

	// ─── Pass 2: 没匹配上的活 voice → fadeout ───
	int releaseFrames = (int)(clusterReleaseMs.get() * 0.001f * 60.0f);   // 假设 60fps
	if (releaseFrames < 1) releaseFrames = 1;

	for (int i = 0; i < NUM_DRONE_VOICES; i++) {
		if (matched[i]) continue;
		if (!droneTracking[i].active) continue;

		if (droneTracking[i].fadeoutFrames == 0) {
			// 启动 fadeout
			droneVoices[i].targetVol.store(0.0f);
			droneTracking[i].fadeoutFrames = releaseFrames;
		} else {
			droneTracking[i].fadeoutFrames--;
			if (droneTracking[i].fadeoutFrames <= 0) {
				// 完全淡出 → 释放
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

	// 读 atomic 参数到本地（一次性，避免每个 sample 都 load）
	float aliveRatio = a_aliveRatio.load();
	float meanSpeed  = a_meanSpeed.load();
	float spread     = a_spread.load();
	float worldRadius = a_worldRadius.load();
	float root = rootFreq;

	// 平滑 drone 控制量（一阶 IIR）
	const float smoothA = 0.001f;   // ~22ms 时间常数
	smAliveRatio += (aliveRatio - smAliveRatio) * smoothA * n;
	smMeanSpeed  += (meanSpeed - smMeanSpeed) * smoothA * n;
	smSpread     += (spread - smSpread) * smoothA * n;

	// drone 频率：root + 调谐（小幅 detune）
	float droneFreqs[4] = {
		root,                          // fundamental
		root * 1.4983f,                // perfect 5th
		root * 2.0f * 1.003f,          // octave (slight detune for chorus)
		root * 2.6667f,                // octave + major 6th
	};
	float droneVolL = droneVol * smAliveRatio;

	// drone filter cutoff (Hz)：spread 大 → 滤波器开
	float spreadNorm = ofClamp(smSpread / (worldRadius * 1.5f), 0.0f, 1.0f);
	float droneCut = 200.0f + spreadNorm * 2500.0f;
	float lpCoef   = ofClamp(droneCut / (sampleRate * 0.5f), 0.0001f, 0.5f);

	// LFO 速率：meanSpeed 大 → 颤抖更快
	float lfoRate = 0.2f + ofClamp(smMeanSpeed * 0.02f, 0.0f, 3.0f);   // 0.2 ~ 3.2 Hz

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

	for (int i = 0; i < n; i++) {
		float left = 0, right = 0;

		// ───────────────────────────────
		// A. DroneLayer — 4 detuned sines
		// ───────────────────────────────
		float droneSum = 0;
		for (int d = 0; d < 4; d++) {
			float dt = droneFreqs[d] / sampleRate;
			dronePhase[d] += dt;
			if (dronePhase[d] >= 1.0f) dronePhase[d] -= 1.0f;
			droneSum += sinf(dronePhase[d] * TWO_PI);
		}
		droneSum *= 0.25f;   // 4 个加起来归一化

		// LFO 调制振幅（轻微）
		lfoPhase += lfoRate / sampleRate;
		if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
		float lfo = 0.5f + 0.5f * sinf(lfoPhase * TWO_PI);   // 0..1
		droneSum *= (0.7f + 0.3f * lfo);

		// 一阶低通滤波器
		droneLpState += (droneSum - droneLpState) * lpCoef;
		float droneOut = droneLpState * droneVolL;

		// drone 居中 pan
		left  += droneOut * 0.5f;
		right += droneOut * 0.5f;

		// ───────────────────────────────
		// A.5  Cluster Drone — 多声部 drone（每个 cluster 一个 voice）
		//      平滑 currentVol → targetVol（attack/release）
		//      每 voice = 3 detuned sine 叠加
		// ───────────────────────────────
		{
			float cdrVol = clusterDroneVol;
			float attackTau = ofClamp(clusterAttackMs.get(),  10.0f, 10000.0f) * 0.001f * sampleRate;
			float releaseTau = ofClamp(clusterReleaseMs.get(), 10.0f, 10000.0f) * 0.001f * sampleRate;
			float attackCoef  = 1.0f - expf(-1.0f / attackTau);
			float releaseCoef = 1.0f - expf(-1.0f / releaseTau);
			float detune = clusterDetune;
			float detuneRatios[3] = {1.0f, 1.0f + detune, 1.0f - detune};

			float cdrSumL = 0, cdrSumR = 0;
			for (int v = 0; v < NUM_DRONE_VOICES; v++) {
				auto& dv = droneVoices[v];
				if (!dv.active.load()) continue;

				float tVol  = dv.targetVol.load();
				float tFreq = dv.targetFreq.load();
				float tPan  = dv.targetPan.load();

				// 平滑（attack vs release 分开系数）
				float coef = (tVol > dv.currentVol) ? attackCoef : releaseCoef;
				dv.currentVol  += (tVol - dv.currentVol)  * coef;
				dv.currentFreq += (tFreq - dv.currentFreq) * 0.0005f;
				dv.currentPan  += (tPan - dv.currentPan)  * 0.001f;

				if (dv.currentVol < 0.00001f && tVol < 0.0001f) continue;

				// 3 detuned sine 叠加
				float sample = 0;
				for (int s = 0; s < 3; s++) {
					float f = dv.currentFreq * detuneRatios[s];
					if (f > sampleRate * 0.45f) f = sampleRate * 0.45f;
					sample += sinf(dv.phase[s] * TWO_PI);
					dv.phase[s] += f / sampleRate;
					if (dv.phase[s] >= 1.0f) dv.phase[s] -= 1.0f;
				}
				sample *= dv.currentVol * (1.0f / 3.0f);   // 3 sines 归一化

				// 等功率 pan
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
		// D. ModalReverb (4-tap FDN)
		// ───────────────────────────────
		if (verbAmt > 0.001f) {
			float inMix = (left + right) * 0.5f * verbAmt;
			float verbSum = 0;
			for (int d = 0; d < 4; d++) {
				float& s = delayBuf[d][delayWrite[d]];
				verbSum += s;
				// 反馈写回
				delayBuf[d][delayWrite[d]] = inMix + s * delayFeedback[d];
				delayWrite[d] = (delayWrite[d] + 1) % delayLength[d];
			}
			verbSum *= 0.25f;
			left  += verbSum * verbAmt;
			right += verbSum * verbAmt;
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
