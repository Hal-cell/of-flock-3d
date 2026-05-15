#pragma once

#include "ofMain.h"

/**
 * Synchresis — Self-aware audiovisual coupler
 * ──────────────────────────────────────────────
 * 实现 Battey (2024) "Fluid Audiovisual Counterpoint"：声画大部分时间独立
 * 演化（counterpoint），关键时刻收束到同一轨迹（synchresis cadence）。
 *
 * 关键概念 — 系统对自身的感知：
 *   - audioEnergy  = Synth 实测能量（RMS 归一化）
 *   - visualEnergy = Flock 实测能量（density × speed × brightness 复合）
 *   - target       = MorphologyConductor 输出的目标能量
 *
 * 工作流程：
 *   每帧 update(dt, target, audio, visual) →
 *     1. 算 audioErr / visualErr 跟 target 的偏离
 *     2. 周期性 cadence pulse（每 syncPeriod 秒一个 Gaussian 脉冲）
 *     3. 在 pulse 高峰附近 → 用 P-control 把 nudge 推到补偿方向
 *     4. 非 pulse 时段 → nudge=0（让声画自由 drift = counterpoint）
 *
 * 输出 nudge 给 ofApp，叠加到 conductor.value() 后送给 Synth / Flock。
 *
 * 不同于"统一 scalar 强制对齐"，这是周期性松紧呼吸：
 *   松（counterpoint）→ 紧（cadence）→ 松 → 紧 …
 * 论文里 "the structural point of resolution or a cadence of a consonant
 * interval in polyphonic music" 的算法化。
 */
class Synchresis {
public:
	void setup();
	void buildGui(ofParameterGroup& group);
	void drawImGui();

	// 每帧 main 线程调用
	void update(float dt,
	            float targetEnergy,
	            float audioEnergy,
	            float visualEnergy);

	// 当前补偿信号（加到 conductor target 上送给 synth / flock）
	// 范围 ≈ ±0.5，clamp 由 ofApp 完成
	float audioCorrection()  const { return audioNudge;  }
	float visualCorrection() const { return visualNudge; }

	// 当前 sync 强度 [0..1]：0 = counterpoint 阶段，1 = cadence 峰值
	float syncStrength() const { return syncStrength_; }

	// 用户手动触发一次 cadence（重置 phase 到峰值附近）
	void triggerCadence();

	// History buffers — 给 ImGui 多线对比可视化用
	static constexpr int HIST_SIZE = 600;   // 10 秒 @ 60fps
	const std::vector<float>& targetHist() const { return targetHist_;  }
	const std::vector<float>& audioHist()  const { return audioHist_;   }
	const std::vector<float>& visualHist() const { return visualHist_;  }
	const std::vector<float>& syncHist()   const { return syncHist_;    }
	int historyIdx() const { return histIdx; }

	// GUI 参数
	ofParameter<bool>  enabled;            // 总开关（默认 false → 向后兼容 rp-35）
	ofParameter<float> syncPeriod;         // cadence 周期（秒）：每多久一次脉冲
	ofParameter<float> syncDuration;       // cadence 脉冲宽度（秒）：脉冲持续多久
	ofParameter<float> syncPower;          // 脉冲峰值时补偿强度的最大值（0..1）
	ofParameter<float> driftTolerance;     // 偏离 < 这个值时不补偿（避免追噪声）

private:
	// 内部状态
	float cadenceTimer = 0.0f;     // 0..syncPeriod 内的相位计时器
	float syncStrength_ = 0.0f;    // 当前脉冲值
	float audioNudge   = 0.0f;
	float visualNudge  = 0.0f;

	// History (ring buffers)
	std::vector<float> targetHist_;
	std::vector<float> audioHist_;
	std::vector<float> visualHist_;
	std::vector<float> syncHist_;
	int histIdx = 0;

	// Gaussian pulse 帮手：pulsePos ∈ [0..1] 内的脉冲强度
	float computePulse(float pulsePos) const;
};
