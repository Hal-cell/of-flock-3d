#pragma once

#include <cmath>

/**
 * EnergyStage — 把 macro energy [0..1] 映射到一个 0..1 的"进场强度"
 * ──────────────────────────────────────────────
 * 论文 Spectromorphology 的核心：每个参数的响应曲线**不一样**。
 * 风声铺底要早进，drone vol 中段进，FM 金属感留给高能段。
 *
 * 每个参数有自己的：
 *   - 激活窗口 [activationStart, activationEnd] —— energy 在这之外不变化
 *   - 曲线形状 (linear / exp / log / sigmoid) —— 窗口内的过渡形态
 *
 * 工作流程：
 *   stage(energy)               → 0..1 进场强度
 *   blend(energy, userVal, ca)  → userVal × (1 - ca + ca × stage(energy))
 *                                 → ca=0 时永远 = userVal（无影响）
 *                                 → ca=1 时 0..userVal 跟着 stage 走
 */
struct EnergyStage {
	float activationStart = 0.0f;
	float activationEnd   = 1.0f;
	int   curve           = 0;   // 0=linear, 1=exp(t²), 2=log(√t), 3=sigmoid(smoothstep)

	float stageOf(float energy) const {
		float span = activationEnd - activationStart;
		if (span < 1e-6f) return energy >= activationStart ? 1.0f : 0.0f;
		float local = (energy - activationStart) / span;
		if (local < 0.0f) local = 0.0f;
		if (local > 1.0f) local = 1.0f;
		switch (curve) {
			case 0: return local;
			case 1: return local * local;                       // exp: 慢启快终
			case 2: return std::sqrt(local);                    // log: 快启慢终
			case 3: return local * local * (3.0f - 2.0f * local); // smoothstep
			default: return local;
		}
	}

	// 把 user 设的"满值"用 conductor amount 跟 stage 调出来
	// ca=0     → 永远输出 userValue（向后兼容）
	// ca=1     → 0..userValue 跟 stage 一致
	// 0<ca<1   → 平滑过渡
	float blend(float energy, float userValue, float ca) const {
		float s = stageOf(energy);
		return userValue * (1.0f - ca + ca * s);
	}

	// 同上但 lowValue ≠ 0（用于 cutoff 这种 "off=200Hz，不是 0Hz" 的情况）
	float blendRange(float energy, float lowValue, float userValue, float ca) const {
		float s = stageOf(energy);
		float fraction = 1.0f - ca + ca * s;
		return lowValue + (userValue - lowValue) * fraction;
	}
};
