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
	ofParameter<float> eventDecayMs;   // 事件衰减时长（毫秒）
	ofParameter<float> eventGainPerHit;// 每次 hit 的振幅（避免太密太响）
	ofParameter<float> reverbAmt;
	ofParameter<float> rootFreq;       // 基频（Hz），默认 110 (A2)
	ofParameter<int>   scaleType;      // 0..SCALE_COUNT-1
	ofParameter<bool>  eventQuantize;  // 是否量化 pitch 到 scale
	ofParameter<float> minMassToFire;  // 低于此质量的碰撞被忽略（防 spam）
	ofParameter<bool>  audioEnabled;   // 总开关

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

	// ─── EventVoices（粒子触发的短促音）───
	// 音频线程本地状态，无需 atomic（生命周期完全在音频线程内）
	struct EventVoice {
		bool  active = false;
		float freq;       // Hz
		float pan;        // 0..1
		float amp;        // 当前振幅（每 sample 衰减）
		float decay;      // 每 sample 衰减系数（接近 1 = 慢）
		float phase;      // 0..1
		float modIndex;   // FM 调制深度（0=纯 sine，>0 = bell-ish）
	};
	static constexpr int NUM_EVENT_VOICES = 32;
	std::array<EventVoice, NUM_EVENT_VOICES> eventVoices;

	// ─── Ring buffer 把碰撞事件从主线程传到音频线程（lock-free SPSC）───
	struct TriggerEvent {
		float freq;
		float pan;
		float amp;
		float decay;
		float modIndex;
	};
	static constexpr int RING_SIZE = 256;   // 必须 2 的幂
	static_assert((RING_SIZE & (RING_SIZE - 1)) == 0, "RING_SIZE must be power of 2");
	std::array<TriggerEvent, RING_SIZE> eventRing;
	std::atomic<int> ringWrite{0};
	std::atomic<int> ringRead{0};

	// ─── ModalReverb（4-tap feedback delay）───
	std::vector<float> delayBuf[4];
	int  delayWrite[4]    = {0, 0, 0, 0};
	int  delayLength[4]   = {0, 0, 0, 0};
	float delayFeedback[4] = {0.65f, 0.62f, 0.58f, 0.55f};

	// ─── helpers ───
	float quantizeToScale(float freq) const;
	float massToFreq(float mass) const;   // 质量 → 频率（接 scale 量化）
	float midiToFreq(float m) const { return 440.0f * powf(2.0f, (m - 69.0f) / 12.0f); }

	// 简单 saw（带状抗混叠 PolyBLEP）
	float polyBlep(float phase, float dt) const;
	float saw(float phase, float dt) const;
};
