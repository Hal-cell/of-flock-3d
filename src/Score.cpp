#include "Score.h"
#include "ImGuiHelpers.h"

//==============================================================
//  Setup — hardcoded demo scores
//==============================================================

void ScorePlayer::setup() {
	scores.clear();

	// ─── #1: "Figure 2 Arc" — 论文 Figure 2 的完整三段 ───
	// ascent → ascent+osc nested → descent，30 秒一个完整 arc
	{
		Score s;
		s.name = "Figure 2 Arc (30s)";
		s.events = {
			//  start   dur  mode  curve  oscRate  oscDepth
			//  modes: 0=FREE 1=ASCENT 2=DESCENT 3=OSCILLATION 4=ASCENT_OSC 5=DESCENT_OSC
			//  curves: 0=LINEAR 1=EXP 2=LOG 3=SIGMOID
			{0.0f,    8.0f,  1, 3, 0.5f, 0.25f},     // 0-8s ASCENT sigmoid
			{8.0f,   12.0f,  4, 2, 0.5f, 0.30f},     // 8-20s ASCENT_OSC log (oscillating ascent)
			{20.0f,  10.0f,  2, 3, 0.5f, 0.25f},     // 20-30s DESCENT sigmoid
		};
		s.totalDuration = 30.0f;
		scores.push_back(s);
	}

	// ─── #2: "Storm Cycle" — 短 ascent → 长 osc → 衰减式 osc ───
	{
		Score s;
		s.name = "Storm Cycle (25s)";
		s.events = {
			{0.0f,    3.0f,  1, 1, 0.5f, 0.25f},     // 0-3s ASCENT exp (急上)
			{3.0f,   12.0f,  3, 0, 0.8f, 0.40f},     // 3-15s OSCILLATION (狂风)
			{15.0f,  10.0f,  5, 3, 0.3f, 0.25f},     // 15-25s DESCENT_OSC sigmoid (褪去)
		};
		s.totalDuration = 25.0f;
		scores.push_back(s);
	}

	// ─── #3: "Quiet Breath" — 长慢呼吸，柔和 ───
	{
		Score s;
		s.name = "Quiet Breath (45s)";
		s.events = {
			{0.0f,   15.0f,  1, 2, 0.2f, 0.15f},     // 0-15s ASCENT log (慢吸气)
			{15.0f,  15.0f,  4, 3, 0.15f, 0.18f},    // 15-30s ASCENT_OSC sigmoid (高点呼吸)
			{30.0f,  15.0f,  2, 2, 0.2f, 0.15f},     // 30-45s DESCENT log (呼气)
		};
		s.totalDuration = 45.0f;
		scores.push_back(s);
	}
}

//==============================================================
//  GUI
//==============================================================

void ScorePlayer::buildGui(ofParameterGroup& group) {
	group.setName("Score");
	int maxIdx = std::max(0, (int)scores.size() - 1);
	group.add(scoreIdx.set("score", 0, 0, maxIdx));
	group.add(looping.set("loop", false));
}

const Score& ScorePlayer::currentScore() const {
	int i = scoreIdx.get();
	if (i < 0) i = 0;
	if (i >= (int)scores.size()) i = (int)scores.size() - 1;
	return scores[i];
}

void ScorePlayer::drawImGui() {
	namespace ig = ImGuiHelp;

	if (ig::section("Score Playback")) {
		ImGui::TextWrapped(
			"Pre-composed score sequences. Player auto-switches conductor "
			"mode/curve/osc at scheduled events; transitions use the rp-35 bridging.");
		ImGui::Spacing();

		// Score 选择下拉
		std::vector<const char*> names;
		for (auto& s : scores) names.push_back(s.name.c_str());
		int cur = scoreIdx.get();
		if (ImGui::Combo("score##sel", &cur, names.data(), (int)names.size())) {
			scoreIdx.set(cur);
		}
		ig::check(looping);

		ImGui::Spacing();

		// 控制按钮
		if (!playing) {
			if (ImGui::Button("▶ Play")) {
				// play() 需要 conductor 引用 — 由 ofApp 调
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(use ofApp Play button — needs conductor ref)");
		} else {
			if (ImGui::Button("■ Stop")) {
				stop();
			}
			ImGui::SameLine();
			if (ImGui::Button("⟲ Restart")) {
				restart();
			}
		}

		ImGui::Separator();

		// 当前状态
		const Score& s = currentScore();
		if (playing) {
			float pct = (s.totalDuration > 0.001f) ? elapsedTime / s.totalDuration : 0.0f;
			ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f),
			                   "playing: %s",
			                   s.name.c_str());
			ImGui::Text("elapsed: %.1f / %.1f s   (%.0f%%)",
			            elapsedTime, s.totalDuration, pct * 100.0f);
			ImGui::ProgressBar(pct, ImVec2(-1, 0));
			if (currentEventIdx >= 0 && currentEventIdx < (int)s.events.size()) {
				const ScoreEvent& e = s.events[currentEventIdx];
				static const char* modeNames[] = {
					"FREE", "ASCENT", "DESCENT", "OSCILLATION", "ASCENT_OSC", "DESCENT_OSC"
				};
				ImGui::Text("event %d/%zu: %s  rate=%.2f depth=%.2f",
				            currentEventIdx + 1, s.events.size(),
				            (e.mode >= 0 && e.mode < 6) ? modeNames[e.mode] : "?",
				            e.oscRate, e.oscDepth);
			}
		} else {
			ImGui::TextDisabled("not playing");
			ImGui::Text("loaded: %s   (%zu events / %.1fs)",
			            s.name.c_str(), s.events.size(), s.totalDuration);
		}
	}
}

//==============================================================
//  Playback logic
//==============================================================

void ScorePlayer::play(MorphologyConductor& conductor) {
	if (scores.empty()) return;
	playing = true;
	elapsedTime = 0.0f;
	currentEventIdx = -1;
	// 立刻应用第一个 event
	applyEvent(0, conductor);
	currentEventIdx = 0;
}

void ScorePlayer::stop() {
	playing = false;
}

void ScorePlayer::update(float dt, MorphologyConductor& conductor) {
	if (!playing) return;

	elapsedTime += dt;
	const Score& s = currentScore();

	// 检查是否到达 score 终点
	if (elapsedTime >= s.totalDuration) {
		if (looping.get()) {
			elapsedTime = 0.0f;
			currentEventIdx = -1;
		} else {
			stop();
			return;
		}
	}

	// 找当前应该是哪个 event
	int eventIdx = -1;
	for (int i = (int)s.events.size() - 1; i >= 0; i--) {
		if (elapsedTime >= s.events[i].startTime) {
			eventIdx = i;
			break;
		}
	}

	// 切到新 event：应用配置
	if (eventIdx != currentEventIdx && eventIdx >= 0) {
		applyEvent(eventIdx, conductor);
		currentEventIdx = eventIdx;
	}
}

void ScorePlayer::applyEvent(int idx, MorphologyConductor& conductor) {
	const Score& s = currentScore();
	if (idx < 0 || idx >= (int)s.events.size()) return;
	const ScoreEvent& e = s.events[idx];

	conductor.mode.set(e.mode);
	conductor.curveShape.set(e.curveShape);
	conductor.phaseDuration.set(e.duration);
	conductor.oscRate.set(e.oscRate);
	conductor.oscDepth.set(e.oscDepth);
	conductor.softRestart();  // 重置 phase，保留 bridgeOffset → 平滑过渡
}
