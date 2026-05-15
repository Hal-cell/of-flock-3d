#include "MorphologyConductor.h"
#include "ImGuiHelpers.h"

//==============================================================
//  Setup / GUI
//==============================================================

void MorphologyConductor::setup() {
	history.assign(HISTORY_SIZE, 0.5f);
	historyIdx = 0;
	elapsedTime = 0.0f;
	currentValue = 0.5f;
	currentPhase = 0.0f;
}

void MorphologyConductor::buildGui(ofParameterGroup& group) {
	group.setName("Morphology");
	group.add(mode.set("mode",                  0,    0,    int(MODE_COUNT) - 1));
	group.add(curveShape.set("curve",           3,    0,    int(CURVE_COUNT) - 1));   // 默认 sigmoid
	group.add(phaseDuration.set("duration (s)", 8.0f, 1.0f, 60.0f));
	group.add(oscRate.set("osc rate (Hz)",      0.5f, 0.05f, 4.0f));
	group.add(oscDepth.set("osc depth",         0.25f, 0.0f, 0.5f));
	group.add(autoLoop.set("auto loop",         false));
}

//==============================================================
//  Trigger / update
//==============================================================

void MorphologyConductor::trigger() {
	elapsedTime = 0.0f;
	currentPhase = 0.0f;
	bridgeOffset = 0.0f;   // 用户显式 trigger 想要干净重启，不要 bridge
}

void MorphologyConductor::softRestart() {
	// 只重置 phase，保留 bridgeOffset → 配合 mode 切换捕获，平滑过渡到新 event
	elapsedTime = 0.0f;
	currentPhase = 0.0f;
	// 不动 bridgeOffset
}

void MorphologyConductor::update(float dt) {
	elapsedTime += dt;

	// 累积振荡相位 — 当 oscRate 动态变化时不会引起相位跳变
	// （旧实现 oscPhase = 2π·rate·elapsedTime 在 rate 改瞬间会让积累的 t × Δrate 全部叠到相位上）
	oscAccumPhase += 2.0f * PI * oscRate.get() * dt;
	while (oscAccumPhase >= 2.0f * PI) oscAccumPhase -= 2.0f * PI;
	while (oscAccumPhase < 0.0f)       oscAccumPhase += 2.0f * PI;

	Mode m = (Mode)mode.get();
	float dur = phaseDuration.get();
	float p = (dur > 0.001f) ? (elapsedTime / dur) : 0.0f;

	// Phase 进度处理（ASCENT / DESCENT 类型有 "完成"，OSCILLATION 无）
	// 记录是否刚发生 auto-loop 重启，后续 bridge 用
	bool autoLoopWrapped = false;
	float preLoopValue = currentValue;
	if (m == ASCENT || m == DESCENT || m == ASCENT_OSC || m == DESCENT_OSC) {
		if (p >= 1.0f) {
			if (autoLoop.get()) {
				// 不调 trigger()（trigger 会 clear bridge），手动 reset elapsedTime
				// 让下面的 bridge 捕获跨循环的跳变
				elapsedTime = 0.0f;
				p = 0.0f;
				autoLoopWrapped = true;
			} else {
				p = 1.0f;   // hold final
			}
		}
	}
	currentPhase = std::min(p, 1.0f);

	// ─── 计算无 offset 的 raw value ───
	float rawV = computeRawValue();

	// ─── 连续性 bridging（多种触发源）───
	// 1. Auto-loop wrap：ASCENT 到顶 → 下一帧从 0 重启 = 跳变 → 捕获桥接
	// 2. Mode 切换：从 ASCENT 切到 DESCENT_OSC 等 → 捕获
	// 3. Curve shape 切换：linear → sigmoid 等 → 捕获
	// 每种触发后 bridgeOffset 都 exp 衰减到 0（~250ms）
	int currentMode = mode.get();
	int currentCurveShape = curveShape.get();
	bool modeChanged = (currentMode != lastMode || currentCurveShape != lastCurveShape);

	if (autoLoopWrapped) {
		bridgeOffset = preLoopValue - rawV;
	}
	if (modeChanged && lastMode != -1) {
		bridgeOffset = currentValue - rawV;
	}
	lastMode = currentMode;
	lastCurveShape = currentCurveShape;

	// Offset exp 衰减
	bridgeOffset *= BRIDGE_DECAY;
	if (fabsf(bridgeOffset) < 0.001f) bridgeOffset = 0.0f;

	currentValue = ofClamp(rawV + bridgeOffset, 0.0f, 1.0f);

	// 推入历史 ring buffer
	history[historyIdx] = currentValue;
	historyIdx = (historyIdx + 1) % HISTORY_SIZE;
}

//==============================================================
//  computeRawValue — 不含 offset / 不含 clamp 的"纯 mode 输出"
//==============================================================
float MorphologyConductor::computeRawValue() const {
	Mode m = (Mode)mode.get();
	// 用累积相位（update() 维护），避免改 oscRate 时相位跳变
	float oscVal = oscDepth.get() * sinf(oscAccumPhase);

	switch (m) {
		case FREE:
			return 0.5f;

		case ASCENT:
			return applyCurve(currentPhase);

		case DESCENT:
			return 1.0f - applyCurve(currentPhase);

		case OSCILLATION:
			// 围绕 0.5 振荡
			return 0.5f + oscVal;

		case ASCENT_OSC: {
			// Nested morphology（论文 Figure 2 中段）：
			// 中心线 ↑，**振荡幅度也跟着 ↑**（peak1 < peak4）
			// 公式：
			//   pcurve   = 曲线进度 0..1
			//   effDepth = depth * pcurve（振荡幅度 0 → max）
			//   center   = pcurve * (1 - effDepth) （确保 phase=1 时 peak 触顶 1）
			//   v        = center + effDepth · sin(2π·rate·t)
			// 边界值检验：
			//   phase=0: center=0, effDepth=0 → v=0 平静起步
			//   phase=1: center=1-depth, effDepth=depth → peak=1，trough=1-2depth
			float depth   = oscDepth.get();
			float pcurve  = applyCurve(currentPhase);
			float effDepth = depth * pcurve;
			float center  = pcurve * (1.0f - effDepth);
			return center + effDepth * sinf(oscAccumPhase);
		}

		case DESCENT_OSC: {
			// 镜像 ascent_osc：中心线 1→0，振荡幅度也 max→0（结尾平静收场）
			//   phase=0: center=1-depth, effDepth=depth → peak=1
			//   phase=1: center=0, effDepth=0 → v=0
			float depth     = oscDepth.get();
			float pcurve    = applyCurve(currentPhase);
			float remaining = 1.0f - pcurve;
			float effDepth  = depth * remaining;
			float center    = remaining * (1.0f - effDepth);
			return center + effDepth * sinf(oscAccumPhase);
		}

		default:
			return 0.5f;
	}
}

//==============================================================
//  Curve shape
//==============================================================

float MorphologyConductor::applyCurve(float t) const {
	t = ofClamp(t, 0.0f, 1.0f);
	switch ((Curve)curveShape.get()) {
		case LINEAR:      return t;
		case EXPONENTIAL: return t * t;
		case LOGARITHMIC: return sqrtf(t);
		case SIGMOID:     return t * t * (3.0f - 2.0f * t);   // smoothstep
		default:          return t;
	}
}

//==============================================================
//  Mode name
//==============================================================

std::string MorphologyConductor::getModeName() const {
	static const char* names[MODE_COUNT] = {
		"free",
		"ascent",
		"descent",
		"oscillation",
		"ascent + osc",
		"descent + osc"
	};
	int m = mode.get();
	if (m >= 0 && m < int(MODE_COUNT)) return names[m];
	return "?";
}

//==============================================================
//  ImGui
//==============================================================

void MorphologyConductor::drawImGui() {
	namespace ig = ImGuiHelp;

	if (ig::section("Morphology Conductor")) {
		ImGui::TextWrapped(
			"Shared morphological trajectory driving audio + visual together. "
			"Implements Spectromorphological Synchresis (Smalley 1997, 2007).");
		ImGui::Spacing();

		static const std::vector<const char*> modeNames = {
			"free (passthrough)",
			"ascent",
			"descent",
			"oscillation",
			"ascent + osc (nested)",
			"descent + osc (nested)"
		};
		ig::combo(mode, modeNames);

		static const std::vector<const char*> curveNames = {
			"linear",
			"exponential (t^2)",
			"logarithmic (sqrt t)",
			"sigmoid (smoothstep)"
		};
		ig::combo(curveShape, curveNames);

		ig::slider(phaseDuration, "%.1f s");
		ig::slider(oscRate,       "%.2f Hz");
		ig::slider(oscDepth);
		ig::check(autoLoop);

		ImGui::Separator();

		// 状态显示
		ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f),
		                   "value: %.3f   phase: %.2f / 1.0",
		                   currentValue, currentPhase);

		// 触发按钮
		if (ImGui::Button("Trigger phase (restart)")) {
			trigger();
		}

		// 历史曲线（环形 buffer，PlotLines 用 offset 参数循环显示）
		ImGui::Separator();
		ImGui::Text("trajectory (last 4s)");
		ImGui::PlotLines("##history",
		                 history.data(), (int)history.size(),
		                 historyIdx,
		                 nullptr,
		                 0.0f, 1.0f,
		                 ImVec2(0, 80));
	}
}
