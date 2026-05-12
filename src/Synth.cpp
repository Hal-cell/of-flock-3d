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
	group.add(audioEnabled.set("audio ON",      true));
	group.add(masterVol.set("masterVol",        0.5f, 0.0f, 1.0f));
	group.add(droneVol.set("droneVol",          0.4f, 0.0f, 1.0f));
	group.add(voiceVol.set("voiceVol",          0.6f, 0.0f, 1.0f));
	group.add(reverbAmt.set("reverbAmt",        0.35f, 0.0f, 1.0f));
	group.add(rootFreq.set("rootFreq (Hz)",     110.0f, 55.0f, 440.0f));   // A2 默认
	group.add(scaleType.set("scale",            0, 0, int(SCALE_COUNT) - 1));
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

void Synth::updateClusters(const std::vector<Flock3D::ClusterCandidate>& clusters){
	float wr = a_worldRadius.load();

	int n = (int)clusters.size();
	for (int i = 0; i < NUM_VOICES; i++) {
		auto& v = voices[i];
		if (i < n) {
			const auto& c = clusters[i];
			v.active.store(true);
			v.targetFreq.store(massToFreq(c.mass));

			// pan：x ∈ [-wr, wr] → [0, 1]
			float pan = ofClamp(c.pos.x / (wr * 2.0f) + 0.5f, 0.0f, 1.0f);
			v.targetPan.store(pan);

			// filter cutoff：y 越高音越亮（200..6000 Hz）
			float yNorm = ofClamp(c.pos.y / wr * 0.5f + 0.5f, 0.0f, 1.0f);
			v.targetCut.store(200.0f + yNorm * 5800.0f);

			// 音量 ∝ sqrt(mass)，再线性归一化
			float vol = ofClamp(sqrtf(c.mass) / 10.0f, 0.0f, 1.0f);
			v.targetVol.store(vol);

			// sine ↔ saw 混音：color hue → 混合比
			// HSV hue approximated from RGB max channel
			float maxc = std::max({c.color.r, c.color.g, c.color.b});
			float minc = std::min({c.color.r, c.color.g, c.color.b});
			float chroma = maxc - minc;
			v.targetMix.store(ofClamp(chroma, 0.0f, 1.0f));
		} else {
			// 没足够团簇 → 静音 + 标记非激活（让 envelope 释放）
			v.active.store(false);
			v.targetVol.store(0.0f);
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
		// B. VoicePool
		// ───────────────────────────────
		float voiceSumL = 0, voiceSumR = 0;
		for (int v = 0; v < NUM_VOICES; v++) {
			auto& vc = voices[v];

			// 加载目标 + 平滑
			const float fastA = 0.002f;
			float tf = vc.targetFreq.load();
			float tp = vc.targetPan.load();
			float tc = vc.targetCut.load();
			float tv = vc.targetVol.load();
			float tm = vc.targetMix.load();

			vc.currentFreq += (tf - vc.currentFreq) * fastA;
			vc.currentPan  += (tp - vc.currentPan)  * fastA;
			vc.currentCut  += (tc - vc.currentCut)  * fastA;
			vc.currentVol  += (tv - vc.currentVol)  * 0.0003f;   // 慢点 attack/release
			vc.currentMix  += (tm - vc.currentMix)  * fastA;

			if (vc.currentVol < 0.0001f) continue;   // 太小不算

			// 振荡器
			float dt = vc.currentFreq / sampleRate;
			vc.phaseSine += dt;
			if (vc.phaseSine >= 1.0f) vc.phaseSine -= 1.0f;
			vc.phaseSaw  += dt;
			if (vc.phaseSaw >= 1.0f) vc.phaseSaw -= 1.0f;

			float sine = sinf(vc.phaseSine * TWO_PI);
			float sw   = saw(vc.phaseSaw, dt);
			float sample = sine * (1.0f - vc.currentMix) + sw * vc.currentMix;

			// 单极低通（每 voice 一个）
			float voiceLpCoef = ofClamp(vc.currentCut / (sampleRate * 0.5f), 0.0001f, 0.5f);
			vc.lpState += (sample - vc.lpState) * voiceLpCoef;
			sample = vc.lpState * vc.currentVol;

			// 等功率 pan
			float panL = cosf(vc.currentPan * HALF_PI);
			float panR = sinf(vc.currentPan * HALF_PI);
			voiceSumL += sample * panL;
			voiceSumR += sample * panR;
		}
		voiceSumL *= voiceVol * (1.0f / NUM_VOICES) * 2.0f;
		voiceSumR *= voiceVol * (1.0f / NUM_VOICES) * 2.0f;
		left  += voiceSumL;
		right += voiceSumR;

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
