#include "Synchresis.h"
#include "ImGuiHelpers.h"

void Synchresis::setup() {
	targetHist_.assign(HIST_SIZE, 0.5f);
	audioHist_.assign(HIST_SIZE,  0.0f);
	visualHist_.assign(HIST_SIZE, 0.0f);
	syncHist_.assign(HIST_SIZE,   0.0f);
	histIdx = 0;
	cadenceTimer = 0.0f;
}

void Synchresis::buildGui(ofParameterGroup& group) {
	group.setName("Synchresis");
	// 默认关闭 — 向后兼容 rp-35；开启后系统才有"自我感知 + 自校正"行为
	group.add(enabled.set("enabled",         false));
	group.add(syncPeriod.set("period (s)",    30.0f, 3.0f,  120.0f));
	group.add(syncDuration.set("pulse (s)",    5.0f, 0.5f,   30.0f));
	group.add(syncPower.set("power",           0.5f, 0.0f,    1.5f));
	group.add(driftTolerance.set("tolerance",  0.15f, 0.0f,   0.5f));
	// Counterpoint Mode（rp-49）— Battey 真正版
	group.add(counterpointEnabled.set("counterpoint",   false));
	group.add(convergenceAmount.set("convergence",       0.85f, 0.0f, 1.0f));
}

void Synchresis::triggerCadence() {
	// 把 timer 拨到脉冲峰值附近（peak at pulsePos=0.5）
	cadenceTimer = syncPeriod.get() * 0.5f;
}

float Synchresis::computePulse(float pulsePos) const {
	// Gaussian 脉冲，中心在 0.5，半宽由 syncDuration 决定
	// pulsePos ∈ [0..1]（cadenceTimer / syncPeriod）
	float dur    = syncDuration.get();
	float period = std::max(syncPeriod.get(), 0.001f);
	float sigma  = (dur / period) * 0.4f;       // 标准差 ≈ pulse 半宽
	if (sigma < 0.001f) sigma = 0.001f;
	float dx = pulsePos - 0.5f;
	return expf(-(dx * dx) / (2.0f * sigma * sigma));
}

void Synchresis::update(float dt,
                       float targetEnergy,
                       float audioEnergy,
                       float visualEnergy) {
	// History push（即使 disabled 也记录，方便用户看"如果开了会怎样"）
	targetHist_[histIdx] = targetEnergy;
	audioHist_[histIdx]  = audioEnergy;
	visualHist_[histIdx] = visualEnergy;

	// 关闭时 nudge 永远 0
	if (!enabled.get()) {
		syncStrength_ = 0.0f;
		audioNudge    = 0.0f;
		visualNudge   = 0.0f;
		syncHist_[histIdx] = 0.0f;
		histIdx = (histIdx + 1) % HIST_SIZE;
		return;
	}

	// 推进 cadence timer（mod period）
	float period = std::max(syncPeriod.get(), 0.001f);
	cadenceTimer += dt;
	while (cadenceTimer >= period) cadenceTimer -= period;

	// 计算当前脉冲值
	float pulsePos = cadenceTimer / period;          // 0..1
	syncStrength_  = computePulse(pulsePos);         // 0..1，中心 0.5 处 = 1

	// 误差 = target - measured；正值 = measured 偏低，需要向上拉
	float audioErr  = targetEnergy - audioEnergy;
	float visualErr = targetEnergy - visualEnergy;

	// Tolerance gate：偏离小于 tolerance 不补偿（避免追噪声 / 持续震荡）
	float tol = driftTolerance.get();
	if (fabsf(audioErr)  < tol) audioErr  = 0.0f;
	if (fabsf(visualErr) < tol) visualErr = 0.0f;

	// 最终 nudge = syncStrength × power × error
	// → counterpoint 阶段 (sync~=0)：nudge≈0，声画自由
	// → cadence 阶段 (sync~=1)：nudge = power × error，拉向 target
	float gain = syncStrength_ * syncPower.get();
	audioNudge  = gain * audioErr;
	visualNudge = gain * visualErr;

	syncHist_[histIdx] = syncStrength_;
	histIdx = (histIdx + 1) % HIST_SIZE;
}

void Synchresis::drawImGui() {
	namespace ig = ImGuiHelp;

	if (ig::section("Synchresis (self-aware coupling)")) {
		ImGui::TextWrapped(
			"System listens to its own audio + visual energy and periodically "
			"forces them to converge (cadence) — implementing Battey's Fluid "
			"Audiovisual Counterpoint.");
		ImGui::Spacing();

		ig::check(enabled);
		ig::slider(syncPeriod,     "%.1f s");
		ig::slider(syncDuration,   "%.1f s");
		ig::slider(syncPower);
		ig::slider(driftTolerance);

		if (ImGui::Button("Trigger cadence now")) {
			triggerCadence();
		}

		ImGui::Separator();
		ImGui::TextDisabled("Counterpoint mode (Battey 真正版)");
		ImGui::TextWrapped(
			"OFF = single conductor drives both audio + visual (legacy).\n"
			"ON  = audio follows Morphology, visual follows Morphology (Visual). "
			"Cadence pulses pull visual toward audio by `convergence`.");
		ig::check(counterpointEnabled);
		ig::slider(convergenceAmount);
		ImGui::TextDisabled("convergence: 0=pure counterpoint, 1=full snap at cadence peak");
		if (counterpointEnabled.get()) {
			ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f),
			                   "→ visual conductor active   force ≈ %.2f",
			                   convergenceForce());
		}

		ImGui::Separator();

		// State 显示
		ImVec4 syncColor = enabled.get()
			? ImVec4(1.0f - syncStrength_,
			         syncStrength_ * 0.8f + 0.2f,
			         syncStrength_ * 0.5f + 0.3f,
			         1.0f)
			: ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
		ImGui::TextColored(syncColor,
		                   "sync strength: %.2f  %s",
		                   syncStrength_,
		                   syncStrength_ > 0.5f ? "← CADENCE" : "(counterpoint)");
		ImGui::Text("audio nudge:  %+.3f", audioNudge);
		ImGui::Text("visual nudge: %+.3f", visualNudge);

		// 4 路 history 分别用 PlotLines 显示（栈式排列）
		ImGui::Separator();
		ImGui::TextDisabled("trajectories (last 10s)");

		ImGui::Text("target");
		ImGui::PlotLines("##target", targetHist_.data(), (int)targetHist_.size(),
		                 histIdx, nullptr, 0.0f, 1.0f, ImVec2(0, 40));

		ImGui::Text("audio energy");
		ImGui::PlotLines("##audio", audioHist_.data(), (int)audioHist_.size(),
		                 histIdx, nullptr, 0.0f, 1.0f, ImVec2(0, 40));

		ImGui::Text("visual energy");
		ImGui::PlotLines("##visual", visualHist_.data(), (int)visualHist_.size(),
		                 histIdx, nullptr, 0.0f, 1.0f, ImVec2(0, 40));

		ImGui::Text("sync strength (cadence pulses)");
		ImGui::PlotLines("##sync", syncHist_.data(), (int)syncHist_.size(),
		                 histIdx, nullptr, 0.0f, 1.0f, ImVec2(0, 30));
	}
}
