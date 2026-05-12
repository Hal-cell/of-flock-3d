#pragma once

#include "ofMain.h"
#include "Flock3D.h"
#include <atomic>
#include <array>

/**
 * Synth — 基于 Flock3D 状态实时合成音频
 * ──────────────────────────────────────────────
 * 3 层架构：
 *
 *   A. DroneLayer  — 持续 drone，由 flock 整体状态调制
 *      4 detuned sine（fundamental + 5th + octave + super 2nd）
 *      LFO 速率 ← meanSpeed
 *      filter cutoff ← spread + aliveRatio
 *      volume ← aliveRatio
 *
 *   B. EventVoices — 32 个短促触发声部（粒子合成器）
 *      每次碰撞抢占一个空闲 voice 触发 damped sine + FM brightness
 *      pitch 量化到 scale；衰减时间可调（默认 ~50ms）
 *      lock-free ring buffer 从主线程接收触发事件
 *
 *   D. ModalReverb — feedback delay network
 *      4 条 delay line → 给 EventVoices 拖尾空间感
 *      tanh limiter 防爆音
 */
class Synth {

public:
	void setup(int sampleRate, int bufferSize);
	void audioOut(ofSoundBuffer& buffer);

	// 主线程调用：传递 flock 状态（drone 用）
	void updateStats(const Flock3D::Stats& s, float worldRadius);

	// 主线程调用：碰撞触发 — 直接把碰撞事件推入 ring buffer
	// 内部会按 GUI 参数计算 freq / pan / decay / modIndex
	void triggerCollision(const Flock3D::CollisionEvent& ev);

	// 主线程调用：传递 cluster 列表 → 驱动 polyphonic drone voice
	void updateClusterVoices(const std::vector<Flock3D::Cluster>& clusters, float worldRadius);

	// 主线程：GUI 控件参数
	void buildGui(ofParameterGroup& group);

	// 音阶切换
	enum Scale {
		SCALE_PENTA_MIN = 0,   // A minor pentatonic
		SCALE_LYDIAN,          // C Lydian
		SCALE_WHOLE_TONE,      // 6 等分
		SCALE_HARMONIC,        // 谐波序列（A2 基频）
		SCALE_COUNT
	};

private:
	int sampleRate = 44100;
	int bufferSize = 512;

	// ─── GUI 参数（主线程写，音频读取）───
	ofParameter<float> masterVol;
	ofParameter<float> droneVol;
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

	// ─── FM 参数（2-op：carrier + modulator）───
	// fmRatio 在 GUI 是连续 float，使用时 snap 到 0.5 倍数
	// fmIndex 调制深度（0 = 纯 sine，10+ = 极度金属）
	// fmIndexDecayMs 控制亮度衰减时长（独立于 carrier 衰减 → 自然 bell envelope）
	ofParameterGroup   fmGroup;
	ofParameter<float> fmRatio;        // 0.5..8.0，自动 snap 到最近 0.5
	ofParameter<float> fmIndex;        // 调制深度
	ofParameter<float> fmIndexDecayMs; // modIndex 衰减时长（ms）

	// ─── Cluster Drone 参数 ───
	ofParameterGroup   clusterDroneGroup;
	ofParameter<float> clusterDroneVol;     // 总音量
	ofParameter<float> clusterAttackMs;     // fade in 时长（ms）
	ofParameter<float> clusterReleaseMs;    // fade out 时长（ms）
	ofParameter<float> clusterDetune;       // 3 sine 的 detune 量（0.001..0.02）
	ofParameter<float> clusterProximity;    // 空间邻近阈值（多近算同一 cluster）

	// ─── Cluster Drone Voice（polyphonic drone 池）───
	struct DroneVoice {
		std::atomic<bool>  active     {false};
		std::atomic<float> targetVol  {0.0f};
		std::atomic<float> targetFreq {110.0f};
		std::atomic<float> targetPan  {0.5f};

		// 音频线程本地（无锁）
		float currentVol  = 0.0f;
		float currentFreq = 110.0f;
		float currentPan  = 0.5f;
		float phase[3]    = {0.0f, 0.333f, 0.666f};   // 3 detuned sines
	};
	static constexpr int NUM_DRONE_VOICES = 8;
	std::array<DroneVoice, NUM_DRONE_VOICES> droneVoices;

	// 主线程跟踪（不跨线程 — 只主线程访问）
	struct DroneTracking {
		bool      active = false;
		glm::vec3 trackedPos{0};
		int       fadeoutFrames = 0;   // 0 = 不在 fadeout；>0 = 倒计时到完全淡出
	};
	std::array<DroneTracking, NUM_DRONE_VOICES> droneTracking;

	// ─── DroneLayer 状态（atomic，跨线程）───
	std::atomic<float> a_aliveRatio   {0.0f};
	std::atomic<float> a_meanSpeed    {0.0f};
	std::atomic<float> a_spread       {0.0f};
	std::atomic<float> a_fieldNoise   {0.0f};
	std::atomic<float> a_fieldVortex  {0.0f};
	std::atomic<float> a_fieldSpiral  {0.0f};
	std::atomic<float> a_fieldCurl    {0.0f};
	std::atomic<float> a_fieldAttract {0.0f};
	std::atomic<float> a_fieldRepel   {0.0f};
	std::atomic<float> a_worldRadius  {250.0f};

	// 音频线程本地状态（drone）
	float dronePhase[4]   = {0, 0.25f, 0.5f, 0.75f};
	float lfoPhase        = 0.0f;
	// 一阶低通滤波器 state（drone filter）
	float droneLpState    = 0.0f;
	// 平滑后的 drone 控制量（避免突变）
	float smAliveRatio    = 0.0f;
	float smMeanSpeed     = 0.0f;
	float smSpread        = 0.0f;

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

	// ─── ModalReverb（4-tap feedback delay）───
	// 反馈系数调低 → 中等长度 tail 不掩盖短音细节
	std::vector<float> delayBuf[4];
	int  delayWrite[4]    = {0, 0, 0, 0};
	int  delayLength[4]   = {0, 0, 0, 0};
	float delayFeedback[4] = {0.55f, 0.52f, 0.48f, 0.45f};

	// ─── helpers ───
	float quantizeToScale(float freq) const;
	float massToFreq(float mass) const;   // 质量 → 频率（接 scale 量化）
	float midiToFreq(float m) const { return 440.0f * powf(2.0f, (m - 69.0f) / 12.0f); }

	// 简单 saw（带状抗混叠 PolyBLEP）
	float polyBlep(float phase, float dt) const;
	float saw(float phase, float dt) const;
};
