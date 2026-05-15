#pragma once

#include "ofMain.h"

/**
 * MorphologyConductor — 顶层"形态学指挥"
 * ──────────────────────────────────────────────
 * 实现论文 "Application of Spectromorphology in Abstract Audiovisual
 * Composition" 提出的 Spectromorphological Synchresis 核心机制：
 *
 *   一条共享的数值曲线，同时驱动音频和视觉参数沿"共享能量-运动轨迹"演化。
 *
 * 输出：`value()` 返回 [0..1]，0.5 = baseline，0 = 最低能量，1 = 最高能量。
 *
 * 5 种 motion mode（对应 Smalley 1997 motion typology）：
 *   - FREE         无 conductor，输出恒定 0.5（baseline / no influence）
 *   - ASCENT       0 → 1（单向上升 / unidirectional ascent）
 *   - DESCENT      1 → 0（单向下降）
 *   - OSCILLATION  绕 0.5 振荡（cyclic motion，phase 不增长）
 *   - ASCENT_OSC   ascent 上叠加 osc（nested morphology）
 *   - DESCENT_OSC  descent 上叠加 osc
 *
 * 4 种 curve shape：
 *   - LINEAR       匀速
 *   - EXPONENTIAL  加速（t²）
 *   - LOGARITHMIC  减速（√t）
 *   - SIGMOID      smoothstep（S 曲线）
 *
 * 用法：
 *   每帧 update(dt) → 取 value() → 推给 Flock / Synth 的 setConductorValue()。
 *   每个消费方有自己的 `conductorAmount` 控制影响强度。
 */
class MorphologyConductor {
public:
	enum Mode {
		FREE = 0,
		ASCENT,
		DESCENT,
		OSCILLATION,
		ASCENT_OSC,
		DESCENT_OSC,
		MODE_COUNT
	};

	enum Curve {
		LINEAR = 0,
		EXPONENTIAL,
		LOGARITHMIC,
		SIGMOID,
		CURVE_COUNT
	};

	void setup();
	void buildGui(ofParameterGroup& group);
	void drawImGui();

	// 每帧更新（main 线程）
	void update(float dt);

	// 重新触发当前 phase（从 0 开始）。用户显式重启 = 干净清 bridge
	void trigger();

	// 轻量重启 phase 但保留 bridgeOffset（给 ScorePlayer 切换 event 用，
	// 让 mode 之间靠 bridge 平滑过渡）
	void softRestart();

	// 当前归一化能量值 [0..1]，baseline 0.5
	float value() const { return currentValue; }

	// 当前 phase 内进度 [0..1]，oscillation 模式始终 0
	float phaseProgress() const { return currentPhase; }

	// GUI / debug 用
	std::string getModeName() const;

	// 历史 buffer（给可视化用；P0 暂未画到主画布，先用 ImGui PlotLines）
	static constexpr int HISTORY_SIZE = 240;   // 4 秒 @ 60fps
	const std::vector<float>& getHistory() const { return history; }
	int getHistoryIdx() const { return historyIdx; }

	// GUI 参数
	ofParameter<int>   mode;
	ofParameter<int>   curveShape;
	ofParameter<float> phaseDuration;   // 1..60 秒
	ofParameter<float> oscRate;         // 0.05..4 Hz
	ofParameter<float> oscDepth;        // 0..0.5
	ofParameter<bool>  autoLoop;

private:
	float elapsedTime  = 0.0f;   // 自当前 phase start 经过的秒数
	float currentValue = 0.5f;
	float currentPhase = 0.0f;

	// 累积式振荡相位（避免改 oscRate 时相位整体跳变）
	// 每帧 += 2π × oscRate × dt，mod 2π；rate 改了只影响往后的累积速率
	float oscAccumPhase = 0.0f;

	// Mode 切换的连续性 bridging：
	// 切换瞬间记录 (上一帧实际输出 - 新 mode 当前 raw)，作为 offset
	// 然后每帧乘 decay 衰减到 0 → 平滑过渡，无跳变
	// decay=0.92 @ 60fps → ~1% 残余 after 1 秒（≈ 250ms 主体过渡时间）
	int   lastMode       = -1;
	int   lastCurveShape = -1;
	float bridgeOffset   = 0.0f;
	static constexpr float BRIDGE_DECAY = 0.92f;

	std::vector<float> history;
	int historyIdx = 0;

	// 计算"无 offset / 无 clamp"的 raw value，用 mode + phase + elapsedTime
	float computeRawValue() const;

	// 应用曲线形状到 [0..1] 进度
	float applyCurve(float t) const;
};
