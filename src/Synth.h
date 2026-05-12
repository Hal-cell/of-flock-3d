#pragma once

#include "ofMain.h"
#include "Flock3D.h"
#include <atomic>
#include <array>

/**
 * Synth — 基于 Flock3D 状态实时合成音频
 * ──────────────────────────────────────────────
 * 3 层架构（架构 A + B + D 的混合）：
 *
 *   A. DroneLayer  — 持续 drone，由 flock 整体状态调制
 *      4 detuned sine（fundamental + 5th + octave + super 2nd）
 *      LFO 速率 ← meanSpeed
 *      filter cutoff ← spread + aliveRatio
 *      volume ← aliveRatio
 *
 *   B. VoicePool   — 8 个 polyphonic voice
 *      每个 voice 对应一个团簇代表
 *      pitch 量化到五声音阶；mass 大 → 音低
 *      sine + saw 混音由 color hue 控制
 *      envelope：smooth attack/release 跟随团簇出现/消失
 *
 *   D. ModalReverb — feedback delay network
 *      4 条 delay line 反馈频率落在调音 root 倍数 → 自然共鸣
 *      tanh limiter 防爆音
 *
 * 主线程（60Hz）→ Synth.update*() 设参数（用 std::atomic 同步）
 * 音频线程（48kHz/512 block）→ Synth.audioOut() 生成样本
 */
class Synth {

public:
	void setup(int sampleRate, int bufferSize);
	void audioOut(ofSoundBuffer& buffer);

	// 主线程调用：传递 flock 状态
	void updateStats(const Flock3D::Stats& s, float worldRadius);
	void updateClusters(const std::vector<Flock3D::ClusterCandidate>& clusters);

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
	ofParameter<float> voiceVol;
	ofParameter<float> reverbAmt;
	ofParameter<float> rootFreq;       // 基频（Hz），默认 110 (A2)
	ofParameter<int>   scaleType;      // 0..SCALE_COUNT-1
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

	// ─── VoicePool ───
	static constexpr int NUM_VOICES = 8;
	struct Voice {
		std::atomic<bool>  active     {false};   // 主线程标记激活
		std::atomic<float> targetFreq {220.0f};  // 主线程写，音频线程读
		std::atomic<float> targetPan  {0.5f};
		std::atomic<float> targetCut  {1500.0f};
		std::atomic<float> targetVol  {0.0f};
		std::atomic<float> targetMix  {0.5f};    // sine/saw 比例

		// 音频线程本地（无锁）
		float currentFreq = 220.0f;
		float currentPan  = 0.5f;
		float currentCut  = 1500.0f;
		float currentVol  = 0.0f;
		float currentMix  = 0.5f;
		float phaseSine   = 0.0f;
		float phaseSaw    = 0.0f;
		float lpState     = 0.0f;
	};
	std::array<Voice, NUM_VOICES> voices;

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
