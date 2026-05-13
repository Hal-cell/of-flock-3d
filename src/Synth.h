#pragma once

#include "ofMain.h"
#include "Flock3D.h"
#include <atomic>
#include <array>

/**
 * Synth — 基于 Flock3D 状态实时合成音频
 * ──────────────────────────────────────────────
 * 2 层架构（全局 drone 已删除）：
 *
 *   A. ClusterDrone (NEW: saw + SVF)
 *      每个 cluster 一个 voice，3 detuned saw 经过 state-variable lowpass
 *      pitch 用和声优先级分配（不重复 + chord 关系）
 *      cutoff + resonance 可调 → 经典 polyphonic synth pad
 *
 *   B. EventVoices — 32 个短促触发声部（2-op FM 粒子合成器）
 *      pitch 量化到同 scale，自动和 drone 同调
 *
 *   D. ModalReverb — 4-tap feedback delay network 给空间感
 */
class Synth {

public:
	void setup(int sampleRate, int bufferSize);
	void audioOut(ofSoundBuffer& buffer);

	// 主线程调用：碰撞触发 — 直接把碰撞事件推入 ring buffer
	// 内部会按 GUI 参数计算 freq / pan / decay / modIndex
	void triggerCollision(const Flock3D::CollisionEvent& ev);

	// 主线程调用：传递 cluster 列表 → 驱动 polyphonic drone voice
	void updateClusterVoices(const std::vector<Flock3D::Cluster>& clusters, float worldRadius);

	// 主线程：tail 长度归一化（0..1）影响 FM idxDecay（正相关）
	void setTailInfluence(float v) { a_tailInfluence.store(v); }

	// 主线程：field amp 总和归一化（0..1）驱动风声音量
	void setFieldAmpTotal(float v) { a_fieldAmpTotal.store(v); }

	// HUD 用：当前活跃（attack/sustain/release 中）的 drone voice 数
	int getActiveDroneCount() const;

	// 总池大小（HUD 显示用）
	static constexpr int getMaxDroneVoices() { return NUM_DRONE_VOICES; }

	// 给 Flock3D trail 用：归一化的"音频活跃度"
	// = (event decay + FM ratio + drone cutoff) 三者归一化平均 [0..1]
	// trail 长度随这个值正相关（越亮/越长 → 尾巴越长）
	float getAudioInfluenceForTail() const;

	// 主线程：GUI 控件参数
	void buildGui(ofParameterGroup& group);

	// 音阶切换
	enum Scale {
		SCALE_PENTA_MIN = 0,   // 五声小调 (A C D E G)
		SCALE_PENTA_MAJ,       // 五声大调 (A B C# E F#)
		SCALE_MAJOR,           // 自然大调 (Ionian)
		SCALE_MINOR_NAT,       // 自然小调 (Aeolian)
		SCALE_DORIAN,          // 多利亚（爵士常用）
		SCALE_MIXOLYDIAN,      // 米索利底亚
		SCALE_PHRYGIAN,        // 弗里几亚（西班牙风）
		SCALE_LYDIAN,          // 利底亚（梦幻 +4 度）
		SCALE_BLUES,           // 蓝调音阶
		SCALE_HIRAJOSHI,       // 平调子（日本五声）
		SCALE_WHOLE_TONE,      // 全音音阶（dreamy / 印象派）
		SCALE_HARMONIC,        // 谐波序列（A2 基频）
		SCALE_COUNT
	};

private:
	int sampleRate = 44100;
	int bufferSize = 512;

	// ─── GUI 参数 ───
	ofParameter<float> masterVol;
	ofParameter<float> eventVol;       // 粒子触发器总音量
	ofParameter<float> eventDecayMs;   // P0 衰减时长（毫秒）
	ofParameter<float> eventAttackMs;  // 起音 ramp 时长（毫秒）— 越短越尖锐
	ofParameter<float> eventGainPerHit;// 每次 hit 的振幅
	ofParameter<float> reverbAmt;
	ofParameter<float> rootFreq;       // 基频（Hz），默认 110 (A2)
	ofParameter<int>   scaleType;      // 0..SCALE_COUNT-1
	ofParameter<bool>  eventQuantize;  // pitch 量化到 scale
	ofParameter<float> minMassToFire;  // 低于此质量的碰撞被忽略
	ofParameter<bool>  audioEnabled;
	ofParameter<float> droneGlideMs;   // drone voice 音高变化时的 glide 时长（ms，用于 scale 切换）

	// ─── FM 参数（2-op：carrier + modulator）───
	// fmRatio 在 GUI 是连续 float，使用时 snap 到 0.5 倍数
	// fmIndex 调制深度（0 = 纯 sine，10+ = 极度金属）
	// fmIndexDecayMs 控制亮度衰减时长（独立于 carrier 衰减 → 自然 bell envelope）
	ofParameterGroup   fmGroup;
	ofParameter<float> fmRatio;        // 0.5..8.0，自动 snap 到最近 0.5
	ofParameter<float> fmIndex;        // 调制深度
	ofParameter<float> fmIndexDecayMs; // modIndex 衰减时长（ms）
	ofParameter<float> tailToIdxDecayDepth; // tail 长度对 fmIdxDecay 的调制深度（0..1）

	// ─── Wind 层参数（持续滤波噪声，模拟风声）───
	// 音量手动控制（vol slider）；field amp 总和 → 调制 cutoff（field 越强 → 风越亮锐）
	// SVF lowpass + 立体声独立噪声 → 自然空间感
	// 信号在 hall reverb 之前混入，会被 reverb 处理
	ofParameterGroup   windGroup;
	ofParameter<float> windVol;         // 总音量（与 field amp 无关）
	ofParameter<float> windCutoff;      // base cutoff (Hz)
	ofParameter<float> windResonance;   // 0..0.95，high = "whistle" 风口哨
	ofParameter<float> windAmpToCutoff; // field amp 对 cutoff 的调制深度
	ofParameter<float> windLfoRate;     // gust 风阵 LFO 速率 (Hz)
	ofParameter<float> windLfoDepth;    // gust 风阵 LFO 深度（0..1，调制 cutoff）

	// 风声音频线程本地状态
	// 左右独立 SVF state → 立体声
	float windSvfLowL = 0, windSvfBandL = 0;
	float windSvfLowR = 0, windSvfBandR = 0;
	float windLfoPhase = 0.0f;

	// ─── Cluster Drone 参数 ───
	ofParameterGroup   clusterDroneGroup;
	ofParameter<float> clusterDroneVol;     // 总音量
	ofParameter<float> clusterAttackMs;     // fade in 时长（ms）
	ofParameter<float> clusterReleaseMs;    // fade out 时长（ms）
	ofParameter<float> clusterDetune;       // 3 saw 的 detune 量
	ofParameter<float> clusterProximity;    // 空间邻近阈值（voice 跟踪用）
	ofParameter<float> clusterCutoff;       // SVF lowpass cutoff（Hz）
	ofParameter<float> clusterResonance;    // SVF resonance（0..0.95）

	// ─── Cluster Drone Voice（polyphonic drone 池，saw + SVF lowpass）───
	struct DroneVoice {
		std::atomic<bool>  active     {false};
		std::atomic<float> targetVol  {0.0f};
		std::atomic<float> targetFreq {110.0f};
		std::atomic<float> targetPan  {0.5f};

		// 音频线程本地
		float currentVol  = 0.0f;
		float currentFreq = 110.0f;
		float currentPan  = 0.5f;
		float phase[3]    = {0.0f, 0.333f, 0.666f};   // 3 detuned saws

		// SVF lowpass state（per-voice 滤波）
		float svfLow  = 0.0f;
		float svfBand = 0.0f;
	};
	// 最多 4 个 drone voice 同时发声（cluster 数 > 4 时只发声 top-4 by mass）
	static constexpr int NUM_DRONE_VOICES = 4;
	std::array<DroneVoice, NUM_DRONE_VOICES> droneVoices;

	// 主线程跟踪（不跨线程 — 只主线程访问）
	struct DroneTracking {
		bool      active = false;
		glm::vec3 trackedPos{0};
		float     fadeoutSec = 0.0f;   // 剩余 release 时间（秒），按真实 frame time 倒计时
		int       semitone = 0;        // 半音偏移（从 rootFreq 起），用于 pitch 不重复
	};
	std::array<DroneTracking, NUM_DRONE_VOICES> droneTracking;

	// helpers
	int pickFreshSemitone() const;  // 找一个未被占用的、和声优先级靠前的半音

	// 仅保留 worldRadius（cluster pan 需要）
	std::atomic<float> a_worldRadius  {250.0f};

	// 来自 Flock3D：当前 tail 长度归一化（0..1），驱动 FM idxDecay 调制
	std::atomic<float> a_tailInfluence{0.0f};

	// 来自 Flock3D：6 个 field amp 总和归一化（0..1），驱动风声音量
	std::atomic<float> a_fieldAmpTotal{0.0f};

	// 检测 scale 切换（主线程）：记上次值，变化时让所有活 voice 重新对齐到新 scale
	int lastScaleType = -1;

	// ─── EventVoices（粒子触发的短促音，2-op FM 合成）───
	// 经典 Chowning FM：carrier + modulator
	//   output = sin(2π·f_c·t + modIndex·sin(2π·f_m·t))
	// modIndex 随时间衰减 → 自然 bell-like "亮起音 → 温暖尾音"
	struct EventVoice {
		bool   active = false;
		int    attackCounter = 0;
		int    attackSamples = 0;
		float  panL = 0.7f, panR = 0.7f;

		// Carrier（基频，承载振幅包络）
		float  carrierFreq  = 220.0f;
		float  carrierPhase = 0.0f;
		float  carrierAmp   = 0.0f;
		float  carrierDecay = 0.999f;

		// Modulator（调制 carrier 的相位）
		float  modFreq        = 220.0f;
		float  modPhase       = 0.0f;
		float  modIndex       = 0.0f;     // 当前调制深度（衰减）
		float  modIndexDecay  = 0.999f;
	};
	static constexpr int NUM_EVENT_VOICES = 32;
	std::array<EventVoice, NUM_EVENT_VOICES> eventVoices;

	// ─── Ring buffer 把碰撞事件从主线程传到音频线程（lock-free SPSC）───
	// FM 参数全部在主线程预计算好；音频线程只 copy
	struct TriggerEvent {
		float carrierFreq, modFreq;
		float carrierAmp,  carrierDecay;
		float modIndex,    modIndexDecay;
		float panL, panR;
		int   attackSamples;
	};
	static constexpr int RING_SIZE = 256;   // 必须 2 的幂
	static_assert((RING_SIZE & (RING_SIZE - 1)) == 0, "RING_SIZE must be power of 2");
	std::array<TriggerEvent, RING_SIZE> eventRing;
	std::atomic<int> ringWrite{0};
	std::atomic<int> ringRead{0};

	// ─── Hall Reverb（FDN: 4 delays + Hadamard mix + HF damping + pre-delay）───
	// 大空间 hall：长 delay tail (RT60 可达数秒) + 高频自然衰减
	static constexpr int NUM_REVERB_DELAYS = 4;
	std::vector<float> delayBuf[NUM_REVERB_DELAYS];
	int   delayWrite[NUM_REVERB_DELAYS]  = {0, 0, 0, 0};
	int   delayLength[NUM_REVERB_DELAYS] = {0, 0, 0, 0};
	float dampLpState[NUM_REVERB_DELAYS] = {0, 0, 0, 0};   // 每条 delay 的 HF damping LP state

	// Pre-delay buffer（在 reverb 前的"空气间隙"，给距离感）
	std::vector<float> preDelayBuf;
	int   preDelayWrite  = 0;
	int   preDelayBufLen = 0;

	// GUI: hall reverb 参数
	ofParameter<float> reverbSize;       // 反馈量（0..0.97，决定 RT60 长度）
	ofParameter<float> reverbDamp;       // HF damping（0=不衰减亮；1=深度衰减暗）
	ofParameter<float> reverbPreDelayMs; // 前置延迟（ms）

	// ─── helpers ───
	float quantizeToScale(float freq) const;
	float massToFreq(float mass) const;   // 质量 → 频率（接 scale 量化）
	float midiToFreq(float m) const { return 440.0f * powf(2.0f, (m - 69.0f) / 12.0f); }

	// 简单 saw（带状抗混叠 PolyBLEP）
	float polyBlep(float phase, float dt) const;
	float saw(float phase, float dt) const;
};
