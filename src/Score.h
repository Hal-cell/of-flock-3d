#pragma once

#include "ofMain.h"
#include "MorphologyConductor.h"

/**
 * Score / ScorePlayer — 一键演奏一首完整作品
 * ──────────────────────────────────────────────
 * 论文 Figure 2 的 "ascent → osc+ascent → descent" 完整曲线 = 一个 Score。
 * Score 是一组按时间排序的 ScoreEvent（每个 event 描述一段时间内 conductor
 * 的 mode / curve / 振荡参数）。ScorePlayer 按 elapsed 时间自动切换 events，
 * 跨 event 用 MorphologyConductor 的 bridging 机制平滑过渡。
 *
 * v0：硬编码 2 个 demo score（Figure 2 Arc / Storm Cycle）。
 * 后续可扩展：用户 record 自己的表演 → 序列化为 score。
 */

struct ScoreEvent {
	float startTime;     // 自 score 开始的秒数
	float duration;      // 该 event 内的 phase duration（送给 conductor.phaseDuration）
	int   mode;          // MorphologyConductor::Mode 枚举值
	int   curveShape;    // MorphologyConductor::Curve 枚举值
	float oscRate;
	float oscDepth;
};

struct Score {
	std::string name;
	std::vector<ScoreEvent> events;
	float totalDuration;
};

class ScorePlayer {
public:
	void setup();
	void buildGui(ofParameterGroup& group);
	void drawImGui();

	// 每帧调用，**在 conductor.update 之前**，可能写 conductor 的 ofParameter
	void update(float dt, MorphologyConductor& conductor);

	void play(MorphologyConductor& conductor);
	void stop();
	void restart() { elapsedTime = 0.0f; currentEventIdx = -1; }

	bool   isPlaying() const { return playing; }
	float  elapsed()   const { return elapsedTime; }
	int    currentEvent() const { return currentEventIdx; }
	std::string currentScoreName() const {
		int i = scoreIdx.get();
		if (i < 0 || i >= (int)scores.size()) return "?";
		return scores[i].name;
	}

	ofParameter<int>  scoreIdx;    // 选哪个 score（0..scores.size()-1）
	ofParameter<bool> looping;

private:
	std::vector<Score> scores;

	float elapsedTime    = 0.0f;
	int   currentEventIdx = -1;
	bool  playing        = false;

	const Score& currentScore() const;
	void applyEvent(int idx, MorphologyConductor& conductor);
};
