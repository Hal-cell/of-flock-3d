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
	group.add(eventGainPerHit.set("hit gain",    0.5f, 0.05f, 1.5f));
	group.add(minMassToFire.set("minMass",       0.0f, 0.0f, 50.0f));
	group.add(eventQuantize.set("quantize",      true));
	group.add(reverbAmt.set("reverbAmt",         0.45f, 0.0f, 1.0f));
	group.add(rootFreq.set("rootFreq (Hz)",      110.0f, 55.0f, 440.0f));
	group.add(scaleType.set("scale",             0, 0, int(SCALE_COUNT) - 1));
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

	// pan：预算成 equal-power 系数（每 sample 用，不调三角函数）
	float panPos = ofClamp(ev.pos.x / (wr * 2.0f) + 0.5f, 0.0f, 1.0f);
	float panL = cosf(panPos * HALF_PI);
	float panR = sinf(panPos * HALF_PI);

	// 基础衰减：把 ms 转成每 sample 的指数衰减系数
	float decaySamples = eventDecayMs * 0.001f * sampleRate;
	float baseDecay = expf(-1.0f / std::max(decaySamples, 1.0f));

	// 亮度：用整体颜色亮度（也可以用其他映射）
	float brightness = ofClamp((ev.color.r + ev.color.g + ev.color.b) / 3.0f, 0.0f, 1.0f);

	// 起音 ramp：约 2ms（防 click），轻微起音的"瞬态"质感
	int attackSamples = (int)(0.002f * sampleRate);

	int w = ringWrite.load(std::memory_order_relaxed);
	int next = (w + 1) & (RING_SIZE - 1);
	if (next == ringRead.load(std::memory_order_acquire)) {
		return;   // ring 满 → 丢
	}
	eventRing[w] = {freq, panL, panR, gain, baseDecay, brightness, attackSamples};
	ringWrite.store(next, std::memory_order_release);
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

	// ─── Inharmonic partial 比例（类编钟 / marimba，给打击乐自然金属感）───
	// {1.0, 2.756, 5.404, 8.933} 是 tubular bell ratios — 稍微调整给柔和一点
	static const float partialRatios[NUM_PARTIALS] = { 1.0f, 2.06f, 3.18f, 4.34f };
	// 各分音的振幅基础值（fundamental 最响，高分音渐弱）
	static const float partialAmps[NUM_PARTIALS]   = { 1.0f, 0.55f, 0.30f, 0.18f };
	// 各分音衰减时长 ratio（< 1 = 比 P0 衰减快 → 高频先消失，温暖余韵）
	static const float partialDecayRatio[NUM_PARTIALS] = { 1.0f, 0.55f, 0.35f, 0.22f };

	// ─── 把 ring buffer 里的 trigger event 分配给空闲 event voices ───
	int r = ringRead.load(std::memory_order_relaxed);
	int wEnd = ringWrite.load(std::memory_order_acquire);
	while (r != wEnd) {
		const TriggerEvent& te = eventRing[r];

		// 找空闲 voice，或偷振幅最小的（P0 amp 作为代表）
		int targetIdx = -1;
		float minAmp = 1e9;
		for (int i = 0; i < NUM_EVENT_VOICES; i++) {
			if (!eventVoices[i].active) { targetIdx = i; break; }
			if (eventVoices[i].partials[0].amp < minAmp) {
				minAmp = eventVoices[i].partials[0].amp;
				targetIdx = i;
			}
		}
		if (targetIdx >= 0) {
			auto& v = eventVoices[targetIdx];
			v.active        = true;
			v.attackCounter = 0;
			v.attackSamples = te.attackSamples;
			v.panL          = te.panL;
			v.panR          = te.panR;

			for (int p = 0; p < NUM_PARTIALS; p++) {
				float pFreq = te.fundamental * partialRatios[p];
				// 限制 partial 频率不超过 Nyquist 一半（避免高音 alias）
				if (pFreq > sampleRate * 0.45f) pFreq = sampleRate * 0.45f;
				v.partials[p].freq  = pFreq;

				// 高分音振幅按 brightness 加权（暗色 → 只有 P0；亮色 → 全分音）
				float ampScale = (p == 0) ? 1.0f : te.brightness;
				v.partials[p].amp   = te.gain * partialAmps[p] * ampScale;

				// 高分音衰减比 P0 快
				// 把"每 sample 衰减系数 baseDecay"加快：decay_p = baseDecay^(1/ratio)
				// 等价于把衰减时长除以 ratio
				v.partials[p].decay = powf(te.baseDecay, 1.0f / partialDecayRatio[p]);

				// phase 错开避免堆积 click（每个分音不同初相位）
				v.partials[p].phase = (float)p * 0.123f;
			}
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
		// B. EventVoices — 短促粒子触发声（加性合成 4 inharmonic partials）
		//   - attack envelope (~2ms) 防起音 click
		//   - 各分音独立衰减：高分音快，低分音慢 → 自然"亮起音→温暖余韵"
		// ───────────────────────────────
		float eventSumL = 0, eventSumR = 0;
		for (int v = 0; v < NUM_EVENT_VOICES; v++) {
			auto& vc = eventVoices[v];
			if (!vc.active) continue;

			// Attack envelope：线性 ramp 起音
			float envAttack = 1.0f;
			if (vc.attackCounter < vc.attackSamples) {
				envAttack = (float)vc.attackCounter / (float)vc.attackSamples;
				vc.attackCounter++;
			}

			// 累加各分音
			float voiceSample = 0;
			bool anyAlive = false;
			for (int p = 0; p < NUM_PARTIALS; p++) {
				auto& pt = vc.partials[p];
				if (pt.amp < 0.00001f) continue;

				voiceSample += sinf(pt.phase * TWO_PI) * pt.amp;

				pt.phase += pt.freq / sampleRate;
				if (pt.phase >= 1.0f) pt.phase -= 1.0f;
				pt.amp *= pt.decay;
				anyAlive = true;
			}

			if (!anyAlive) {
				vc.active = false;
				continue;
			}

			voiceSample *= envAttack;

			// 预算的 pan 系数（无三角函数 / sample）
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
