#include "Synth.h"
#include "ImGuiHelpers.h"

//==============================================================
//  Setup
//==============================================================
void Synth::setup(int sr, int bs){
	sampleRate = sr;
	bufferSize = bs;

	// 初始化 4 条 hall reverb delay line（长度对应 hall 空间尺度）
	// 用质数 ms 数避免反馈周期对齐；4 条 delay 长度跨度比例约 1:1.25:1.55:1.86
	// → 在 Hadamard 混合下产生密集 ill-correlated 反射
	float delayMs[NUM_REVERB_DELAYS] = {152.0f, 191.0f, 234.0f, 283.0f};
	for (int i = 0; i < NUM_REVERB_DELAYS; i++) {
		int n = std::max(1, (int)(delayMs[i] * 0.001f * sampleRate));
		delayLength[i] = n;
		delayBuf[i].assign(n, 0.0f);
		delayWrite[i] = 0;
		dampLpState[i] = 0.0f;
	}

	// Pre-delay buffer：分配 250ms 容量（覆盖 GUI 范围 0..200ms）
	preDelayBufLen = (int)(0.25f * sampleRate);
	preDelayBuf.assign(preDelayBufLen, 0.0f);
	preDelayWrite = 0;

	// ─── Granular source: 优先加载捆绑的 ChurchBells（FluCoMa Tremblay 包）───
	// 找不到 → 回落到合成 drone，确保即使 data/ 缺文件也能跑
	const std::string defaultSamplePath = ofToDataPath("Tremblay-CF-ChurchBells.wav", true);
	if (!loadGrainSource(defaultSamplePath)) {
		ofLogWarning("Synth") << "default sample not found, falling back to synthesized drone";
		synthesizeDefaultGrainSource();
	}
}

//--------------------------------------------------------------
//  Granular source 默认合成 + 拖拽加载
//--------------------------------------------------------------
void Synth::synthesizeDefaultGrainSource() {
	std::vector<float> src(4 * sampleRate);
	float f1 = 110.0f, f2 = 110.0f * 1.005f, f3 = 165.0f;
	float p1 = 0.0f, p2 = 0.0f, p3 = 0.0f;
	float lfoPhase = 0.0f;
	for (size_t i = 0; i < src.size(); i++) {
		float dt = 1.0f / sampleRate;
		float lfo = sinf(lfoPhase * TWO_PI);
		lfoPhase += 0.13f * dt;
		if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
		float ampMod = 0.7f + lfo * 0.3f;
		float n = ((float)std::rand() / RAND_MAX - 0.5f) * 0.05f;
		float s = (sinf(p1 * TWO_PI) * 0.40f
		        +  sinf(p2 * TWO_PI) * 0.35f
		        +  sinf(p3 * TWO_PI) * 0.25f) * ampMod + n;
		p1 += f1 * dt;  if (p1 >= 1.0f) p1 -= 1.0f;
		p2 += f2 * dt;  if (p2 >= 1.0f) p2 -= 1.0f;
		p3 += f3 * dt;  if (p3 >= 1.0f) p3 -= 1.0f;
		src[i] = s * 0.7f;
	}
	{
		std::lock_guard<std::mutex> lk(grainSourceMutex);
		grainSource = std::move(src);
		grainSourceLen = (int)grainSource.size();
		grainSourceName = "default (synthesized drone)";
		for (auto& g : grains) g.active = false;
	}
}

void Synth::resetGrainSourceDefault() {
	// 跟 setup() 同样的逻辑：先试 bundled ChurchBells，找不到才合成 drone
	const std::string defaultSamplePath = ofToDataPath("Tremblay-CF-ChurchBells.wav", true);
	if (!loadGrainSource(defaultSamplePath)) {
		synthesizeDefaultGrainSource();
	}
}

// ─── 内嵌的 WAV 解析（OF 0.12 没有 ofSoundFile）───
// 只支持 PCM WAV：16-bit / 24-bit / 32-bit int / 32-bit float
// 正常 ableton/audacity 导出的样本都能读
namespace {

struct WavData {
	int sampleRate = 0;
	int numChannels = 0;
	std::vector<float> interleaved;   // -1..1 范围的 mono/stereo interleaved float
};

static uint32_t readLE32(const uint8_t* p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t readLE16(const uint8_t* p) {
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static bool parseWav(const std::string& path, WavData& out) {
	ofFile f(path, ofFile::ReadOnly, true);
	if (!f.exists()) return false;
	auto data = ofBufferFromFile(path, true).getData();
	const uint8_t* buf = (const uint8_t*)data;
	size_t size = ofBufferFromFile(path, true).size();
	if (size < 44) return false;

	if (buf[0] != 'R' || buf[1] != 'I' || buf[2] != 'F' || buf[3] != 'F') return false;
	if (buf[8] != 'W' || buf[9] != 'A' || buf[10] != 'V' || buf[11] != 'E') return false;

	// 走 chunk 找 fmt 和 data
	size_t pos = 12;
	uint16_t audioFormat = 0, numChannels = 0, bitsPerSample = 0;
	uint32_t sampleRate = 0;
	const uint8_t* dataPtr = nullptr;
	size_t dataSize = 0;

	while (pos + 8 <= size) {
		char ckId[5] = {(char)buf[pos],(char)buf[pos+1],(char)buf[pos+2],(char)buf[pos+3],0};
		uint32_t ckSize = readLE32(buf + pos + 4);
		if (pos + 8 + ckSize > size) break;
		if (std::string(ckId) == "fmt ") {
			audioFormat   = readLE16(buf + pos + 8);
			numChannels   = readLE16(buf + pos + 10);
			sampleRate    = readLE32(buf + pos + 12);
			bitsPerSample = readLE16(buf + pos + 22);
		} else if (std::string(ckId) == "data") {
			dataPtr  = buf + pos + 8;
			dataSize = ckSize;
		}
		pos += 8 + ckSize + (ckSize & 1);   // chunk 对齐到偶数字节
	}

	if (!dataPtr || numChannels == 0 || sampleRate == 0) return false;

	int bytesPerSample = bitsPerSample / 8;
	if (bytesPerSample <= 0) return false;
	int numSamples = (int)(dataSize / bytesPerSample);

	out.sampleRate  = (int)sampleRate;
	out.numChannels = (int)numChannels;
	out.interleaved.resize(numSamples);

	if (audioFormat == 1 && bitsPerSample == 16) {
		const int16_t* p = (const int16_t*)dataPtr;
		for (int i = 0; i < numSamples; i++) out.interleaved[i] = p[i] / 32768.0f;
	} else if (audioFormat == 1 && bitsPerSample == 24) {
		for (int i = 0; i < numSamples; i++) {
			const uint8_t* s = dataPtr + i * 3;
			int32_t v = (int32_t)((s[0]) | (s[1] << 8) | (s[2] << 16));
			if (v & 0x800000) v |= 0xFF000000;   // sign-extend
			out.interleaved[i] = (float)v / 8388608.0f;
		}
	} else if (audioFormat == 1 && bitsPerSample == 32) {
		const int32_t* p = (const int32_t*)dataPtr;
		for (int i = 0; i < numSamples; i++) out.interleaved[i] = p[i] / 2147483648.0f;
	} else if (audioFormat == 3 && bitsPerSample == 32) {
		const float* p = (const float*)dataPtr;
		for (int i = 0; i < numSamples; i++) out.interleaved[i] = p[i];
	} else {
		return false;   // 不支持的格式
	}
	return true;
}

} // anon

bool Synth::loadGrainSource(const std::string& path) {
	std::string ext = ofToLower(ofFilePath::getFileExt(path));
	if (ext != "wav") {
		ofLogError("Synth") << "loadGrainSource: only WAV supported in this build (got ." << ext << ")";
		return false;
	}

	WavData wav;
	if (!parseWav(path, wav)) {
		ofLogError("Synth") << "loadGrainSource: failed to parse WAV " << path
		                    << " (only PCM 16/24/32-bit and 32-bit float supported)";
		return false;
	}

	int srcSr = wav.sampleRate;
	int numCh = wav.numChannels;
	int numFrames = (int)(wav.interleaved.size() / std::max(1, numCh));
	if (numFrames < 100) {
		ofLogError("Synth") << "loadGrainSource: too short (" << numFrames << " frames)";
		return false;
	}

	// 转 mono
	std::vector<float> mono(numFrames);
	for (int i = 0; i < numFrames; i++) {
		float s = 0.0f;
		for (int c = 0; c < numCh; c++) s += wav.interleaved[i * numCh + c];
		mono[i] = s / (float)numCh;
	}

	// 重采样到 synth sampleRate（如果不一致）
	if (srcSr != sampleRate && srcSr > 0) {
		float ratio = (float)sampleRate / (float)srcSr;
		int newLen = (int)(mono.size() * ratio);
		std::vector<float> resampled(newLen);
		for (int i = 0; i < newLen; i++) {
			float srcIdx = (float)i / ratio;
			int idx = (int)srcIdx;
			float frac = srcIdx - (float)idx;
			if (idx >= (int)mono.size() - 1) {
				resampled[i] = mono.back();
			} else {
				resampled[i] = mono[idx] * (1.0f - frac) + mono[idx + 1] * frac;
			}
		}
		mono = std::move(resampled);
	}

	// 截到 30 秒（防超长样本占内存）
	int maxLen = 30 * sampleRate;
	if ((int)mono.size() > maxLen) mono.resize(maxLen);

	// 简单 normalize 到 ~0.7 peak
	float peak = 0.0f;
	for (float v : mono) { float a = fabsf(v); if (a > peak) peak = a; }
	if (peak > 0.001f) {
		float g = 0.7f / peak;
		for (float& v : mono) v *= g;
	}

	// Atomic swap：拿锁、换 buffer、reset 所有 active grain
	{
		std::lock_guard<std::mutex> lk(grainSourceMutex);
		grainSource = std::move(mono);
		grainSourceLen = (int)grainSource.size();
		grainSourceName = ofFilePath::getFileName(path);
		for (auto& g : grains) g.active = false;
	}
	ofLogNotice("Synth") << "loadGrainSource ok: " << grainSourceName
	                     << " (" << grainSourceLen << " samples, " << numCh << "ch → mono)";
	return true;
}

void Synth::buildGui(ofParameterGroup& group){
	group.setName("Synth");
	group.add(audioEnabled.set("audio ON",       true));
	group.add(masterVol.set("masterVol",         0.5f, 0.0f, 1.0f));
	group.add(eventVol.set("eventVol",           0.6f, 0.0f, 1.0f));
	group.add(eventDecayMs.set("decay (ms)",    50.0f, 5.0f, 500.0f));
	group.add(eventAttackMs.set("attack (ms)",   2.0f, 0.1f, 50.0f));
	group.add(eventGainPerHit.set("hit gain",    0.5f, 0.05f, 1.5f));
	group.add(minMassToFire.set("minMass",       0.0f, 0.0f, 50.0f));
	group.add(eventQuantize.set("quantize",      true));
	// ─── Hall Reverb ───
	group.add(reverbAmt.set("reverbAmt",         0.55f, 0.0f, 1.0f));      // 干湿比
	group.add(reverbSize.set("reverbSize",       0.85f, 0.0f, 0.97f));     // 长尾控制（0=无反馈，0.97=极长）
	group.add(reverbDamp.set("reverbDamp",       0.5f,  0.0f, 0.99f));     // HF 衰减
	group.add(reverbPreDelayMs.set("reverb preDelay (ms)", 20.0f, 0.0f, 200.0f));
	group.add(rootFreq.set("rootFreq (Hz)",      110.0f, 55.0f, 440.0f));
	// scale: 0=pentaMin 1=pentaMaj 2=major 3=minor 4=dorian 5=mixolydian
	//        6=phrygian 7=lydian 8=blues 9=hirajoshi 10=wholeTone 11=harmonic
	group.add(scaleType.set("scale",             0, 0, int(SCALE_COUNT) - 1));
	// 当 scale 切换时，drone voice 沿这个时长 glide 到新音高（ms）
	group.add(droneGlideMs.set("drone glide (ms)", 600.0f, 5.0f, 4000.0f));
	// 给 Synchresis 用的 audio energy 灵敏度（默认 1，audio 太静 → 调高）
	group.add(audioEnergyGain.set("audio energy gain", 1.0f, 0.1f, 5.0f));

	// ─── FM 子组（2-op：carrier + modulator）───
	// 一些典型 ratio 玩法：
	//   1.0      → carrier 自我调制，方波感
	//   2.0      → 奇数谐波（clarinet 风）
	//   3.0/4.0  → 整数倍（bright but harmonic）
	//   3.5/4.5  → 非整数倍（bell-like 金属）
	//   7.0/7.5  → 经典"DX7 bell"
	//   0.5      → 低于 carrier，sub-harmonic 感
	fmGroup.setName("FM");
	fmGroup.add(fmRatio.set("FM ratio",          2.0f,  0.5f, 8.0f));    // 自动 snap 到 0.5 倍数
	fmGroup.add(fmIndex.set("FM index",          3.0f,  0.0f, 12.0f));
	fmGroup.add(fmIndexDecayMs.set("FM idxDecay (ms)",  40.0f, 1.0f, 500.0f));
	// Tail 长度 → FM idxDecay 正相关调制（base + tail × depth × 400ms）
	fmGroup.add(tailToIdxDecayDepth.set("tail → idxDecay", 0.5f, 0.0f, 1.0f));
	// Event wave fold（独立于 drone fold）：每个 event voice 的输出做 sin fold
	// conductor 高时自动加深（同 stageFold 编排）→ 高能段 event 更金属
	fmGroup.add(eventFoldAmount.set("event fold", 0.0f, 0.0f, 1.0f));
	group.add(fmGroup);

	// ─── Cluster Drone 子组（saw + SVF lowpass + chord 优先 pitch）───
	clusterDroneGroup.setName("ClusterDrone");
	clusterDroneGroup.add(clusterDroneVol.set("vol",          0.5f,   0.0f,   1.0f));
	clusterDroneGroup.add(clusterAttackMs.set("attack (ms)",  800.0f, 50.0f, 4000.0f));
	clusterDroneGroup.add(clusterReleaseMs.set("release (ms)", 1500.0f, 50.0f, 6000.0f));
	clusterDroneGroup.add(clusterDetune.set("detune",         0.008f, 0.0f,  0.03f));
	clusterDroneGroup.add(clusterProximity.set("proximity",   80.0f,  10.0f, 400.0f));
	clusterDroneGroup.add(clusterCutoff.set("cutoff (Hz)",    600.0f, 80.0f, 8000.0f));
	clusterDroneGroup.add(clusterResonance.set("resonance",   0.3f,   0.0f,  0.95f));
	// Wave folder（西海岸合成派）：sin(x · drive) 形式，加谐波不加幅度
	// conductor 上升时也会自动加深（× audioCondScalar），让 spectrum 跟能量轨迹绑定
	clusterDroneGroup.add(clusterDroneFold.set("fold",        0.0f,   0.0f,  1.0f));
	group.add(clusterDroneGroup);

	// ─── Wind 子组（持续滤波噪声；field amp → cutoff）───
	// 音量独立 slider；field amp 总和 0..1 把 cutoff 抬高
	windGroup.setName("Wind");
	windGroup.add(windVol.set("vol",                 0.4f,    0.0f,    1.0f));
	windGroup.add(windCutoff.set("base cutoff (Hz)", 800.0f,  100.0f,  8000.0f));
	windGroup.add(windResonance.set("resonance",     0.2f,    0.0f,    0.9f));
	windGroup.add(windAmpToCutoff.set("amp→cutoff",  1.0f,    0.0f,    3.0f));  // amp 满时最多加 4kHz × 此值
	windGroup.add(windLfoRate.set("gust rate (Hz)",  0.4f,    0.05f,   4.0f));
	windGroup.add(windLfoDepth.set("gust depth",     0.4f,    0.0f,    1.0f));
	group.add(windGroup);

	// ─── Granular 子组（rp-44）───
	// 颗粒采样云：cluster 数高时密度增加；低能段被 conductor staging 压低音量
	granGroup.setName("Granular");
	granGroup.add(granVol.set("vol",                       0.3f,  0.0f, 1.0f));
	// 短 grain + 高频率 = "咔嗒"流；长 grain + 低频率 = 流畅 cloud
	// base rate 200Hz 上限 → grain 重叠成连续纹理（5ms / grain 间隔）
	granGroup.add(grainSizeMs.set("grain size (ms)",       35.0f, 10.0f, 300.0f));
	granGroup.add(grainBaseRate.set("base rate (Hz)",       8.0f,  0.5f, 200.0f));
	granGroup.add(granClusterInfluence.set("cluster influence", 6.0f, 0.0f, 20.0f));
	// 中心 pitch offset：所有 grain 都先按这个偏移移调，再叠加 spread 随机
	// -12 = 下八度（拉长慢拷贝感），+12 = 上八度（加亮 chip 风），0 = 原速
	granGroup.add(grainPitchOffset.set("pitch offset (st)", 0.0f, -24.0f, 24.0f));
	granGroup.add(grainPitchSpread.set("pitch spread (st)", 5.0f,  0.0f, 24.0f));
	granGroup.add(grainPanSpread.set("pan spread",          0.6f,  0.0f, 1.0f));
	// 包络 attack 占比：0.05 = 锐起音 + 长衰减（pluck/click 感）
	//                  0.5 = 对称三角（≈ Hann，平滑无 transient）
	granGroup.add(grainAttackFrac.set("attack frac",        0.08f, 0.02f, 0.5f));
	group.add(granGroup);

	// ─── Morphology Conductor 影响 ───
	// 论文 Spectromorphological Synchresis：conductor 曲线对 audio 侧 brightness 的影响幅度
	// 0 = 不受影响（向后兼容 rp-34）；1 = 满影响（conductor 1.0 → drone cutoff + FM index 双倍）
	group.add(conductorAmount.set("conductor amount", 0.0f, 0.0f, 1.0f));
}

//--------------------------------------------------------------
//  ImGui rendering — 替代 ofxPanel 的现代 GUI
//--------------------------------------------------------------
void Synth::drawImGui(){
	namespace ig = ImGuiHelp;

	if (ig::section("Master")) {
		ig::check(audioEnabled);
		ig::slider(masterVol);
		ig::slider(rootFreq, "%.1f Hz");
		static const std::vector<const char*> scaleNames = {
			"penta minor", "penta major", "major (ion)", "minor (aeolian)",
			"dorian", "mixolydian", "phrygian", "lydian",
			"blues", "hirajoshi", "whole tone", "harmonic series"
		};
		ig::combo(scaleType, scaleNames);
		ig::slider(droneGlideMs, "%.0f ms");
	}

	if (ig::section("Event (Particle Hits)")) {
		ig::slider(eventVol);
		ig::slider(eventDecayMs, "%.0f ms");
		ig::slider(eventAttackMs, "%.2f ms");
		ig::slider(eventGainPerHit);
		ig::slider(minMassToFire, "%.1f");
		ig::check(eventQuantize);
	}

	if (ig::section("FM (Event Tone)")) {
		ig::slider(fmRatio, "%.2f");
		ig::slider(fmIndex);
		ig::slider(fmIndexDecayMs, "%.0f ms");
		ig::slider(tailToIdxDecayDepth);
		ig::slider(eventFoldAmount);
		ImGui::TextDisabled("event fold 加深 → 钟声金属化；conductor 高时自动加深");
	}

	if (ig::section("Cluster Drone")) {
		ig::slider(clusterDroneVol);
		ig::slider(clusterAttackMs, "%.0f ms");
		ig::slider(clusterReleaseMs, "%.0f ms");
		ig::slider(clusterDetune, "%.4f");
		ig::slider(clusterProximity, "%.1f");
		ig::sliderLog(clusterCutoff, "%.0f Hz");
		ig::slider(clusterResonance);
		ig::slider(clusterDroneFold);
		ImGui::TextDisabled("fold 加深 → 谐波丰富但幅度不变；conductor 高时自动加深");
	}

	if (ig::section("Wind")) {
		ig::slider(windVol);
		ig::sliderLog(windCutoff, "%.0f Hz");
		ig::slider(windResonance);
		ig::slider(windAmpToCutoff);
		ig::slider(windLfoRate, "%.2f Hz");
		ig::slider(windLfoDepth);
	}

	if (ig::section("Granular (cluster-driven cloud)")) {
		ig::slider(granVol, "%.2f");
		ig::slider(grainSizeMs, "%.0f ms");
		ig::slider(grainBaseRate, "%.1f Hz");
		ig::slider(granClusterInfluence, "%.1f Hz/cluster");
		ig::slider(grainPitchOffset, "%+.1f st");
		ig::slider(grainPitchSpread, "%.1f st");
		ig::slider(grainPanSpread);
		ig::slider(grainAttackFrac, "%.2f");
		ImGui::TextDisabled("attack frac: 0.05 锐 pluck/click | 0.5 对称 Hann/平滑");
		ImGui::TextDisabled("Effective rate = base + cluster × influence");

		// Sample 状态 + 拖拽提示 + reset
		ImGui::Separator();
		std::string srcName;
		{
			std::lock_guard<std::mutex> lk(grainSourceMutex);
			srcName = grainSourceName;
		}
		ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f),
		                   "source: %s", srcName.c_str());

		// ─── 专用 drop zone（拖文件到这片区域，或这个 GUI 窗口任何位置）───
		// 视觉指示，实际 drop handler 在 ofApp::dragEventGui 全窗口生效
		ImGui::Spacing();
		ImGui::PushStyleColor(ImGuiCol_ChildBg,  ImVec4(0.15f, 0.22f, 0.32f, 0.55f));
		ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0.45f, 0.65f, 0.95f, 0.65f));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.5f);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
		ImGui::BeginChild("granular_dropzone", ImVec2(-1, 64), true);
		{
			const char* line1 = "DROP  .WAV  HERE";
			const char* line2 = "(or anywhere on this control window)";
			ImVec2 avail = ImGui::GetContentRegionAvail();
			ImVec2 sz1 = ImGui::CalcTextSize(line1);
			ImVec2 sz2 = ImGui::CalcTextSize(line2);
			float totalH = sz1.y + sz2.y + 4.0f;
			float yStart = (avail.y - totalH) * 0.5f;
			if (yStart < 0) yStart = 0;
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + yStart);
			ImGui::SetCursorPosX((avail.x - sz1.x) * 0.5f);
			ImGui::TextColored(ImVec4(0.85f, 0.93f, 1.0f, 0.95f), "%s", line1);
			ImGui::SetCursorPosX((avail.x - sz2.x) * 0.5f);
			ImGui::TextColored(ImVec4(0.65f, 0.75f, 0.90f, 0.75f), "%s", line2);
		}
		ImGui::EndChild();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(2);

		if (ImGui::Button("Reset to default source")) {
			resetGrainSourceDefault();
		}
		ImGui::TextDisabled("当前 cluster 数: %d   active grains: %d",
		                    a_clusterCount.load(),
		                    (int)std::count_if(grains.begin(), grains.end(),
		                                       [](const Grain& g) { return g.active; }));
	}

	if (ig::section("Hall Reverb")) {
		ig::slider(reverbAmt);
		ig::slider(reverbSize);
		ig::slider(reverbDamp);
		ig::slider(reverbPreDelayMs, "%.0f ms");
	}

	if (ig::section("Conductor → Audio")) {
		ImGui::TextWrapped(
			"How strongly does the Morphology Conductor modulate audio "
			"brightness (drone cutoff + FM index)?");
		ig::slider(conductorAmount);
		ImGui::TextDisabled("0 = no effect; 1 = full coupling.");
	}
}

//--------------------------------------------------------------
//  主线程：每次碰撞推一个 TriggerEvent 到 ring buffer
//--------------------------------------------------------------
void Synth::triggerCollision(const Flock3D::CollisionEvent& ev){
	if (ev.newMass < minMassToFire) return;

	float wr = a_worldRadius.load();

	// 基频（fundamental P0）
	float freq;
	if (eventQuantize) {
		freq = massToFreq(ev.newMass);
	} else {
		float l = log10f(std::max(ev.newMass, 1.0f));
		float t = ofClamp((l - 0.5f) / 2.0f, 0.0f, 1.0f);
		t = 1.0f - t;
		freq = rootFreq * powf(2.0f, t * 3.0f);
	}

	// Accent: 高八度（freq * 2）+ 振幅小幅 boost，让重音突出
	float gain = eventGainPerHit;
	if (ev.isAccent) {
		freq *= 2.0f;        // +1 octave
		gain *= 1.3f;        // 30% 更响
	}

	// pan：预算成 equal-power 系数
	float panPos = ofClamp(ev.pos.x / (wr * 2.0f) + 0.5f, 0.0f, 1.0f);
	float panL = cosf(panPos * HALF_PI);
	float panR = sinf(panPos * HALF_PI);

	// P0 基础衰减：每 sample 指数衰减系数 = exp(-1 / (T_samples))
	float decaySamples = eventDecayMs * 0.001f * sampleRate;
	float baseDecay = expf(-1.0f / std::max(decaySamples, 1.0f));

	// 亮度（particle 颜色亮度，0..1）
	float brightness = ofClamp((ev.color.r + ev.color.g + ev.color.b) / 3.0f, 0.0f, 1.0f);

	// 起音 ramp（从 GUI ms 转 samples）
	int attackSamples = std::max(1, (int)(eventAttackMs * 0.001f * sampleRate));

	// ─── 主线程预计算 FM 参数 ───
	float nyquist = sampleRate * 0.45f;

	// fmRatio: GUI float → snap 到最近 0.5 倍数（不被 conductor 调制：
	// 之前 rp-40 把 ratio 锚到 1.0 at 低能 → 配合 modIndex=0 时事件就成纯 sine 听不见）
	float rawRatio = fmRatio.get();
	float snappedRatio = roundf(rawRatio * 2.0f) / 2.0f;
	if (snappedRatio < 0.5f) snappedRatio = 0.5f;

	// Carrier
	float carrierFreq = std::min(freq, nyquist);

	// Modulator
	float modFreqRaw = freq * snappedRatio;
	float modFreq = std::min(modFreqRaw, nyquist);

	// modIndex: 用 brightness 缩放（暗色 0.3x，亮色 1.0x → 暗 = 纯 sine，亮 = 金属）
	float modIndexScale = 0.3f + brightness * 0.7f;
	float modIndexInit  = fmIndex.get() * modIndexScale;

	// Accent 命中时也让 modIndex 增加（更明亮的重音）
	if (ev.isAccent) modIndexInit *= 1.5f;

	// FM modIndex 编排：高能段更金属，但**永远不归零**（保证事件总是听得见）
	// stageFM 用 blendRange，低能 floor = userValue × 0.5（仍有 bell 音色），
	// 高能到 userValue × 1.0
	// 之前 0.5..1.0 + blend 的写法在低能段 modIndex=0 → 事件成纯 sine 被掩盖
	{
		static const EnergyStage stageFM {0.0f, 1.0f, 3};  // sigmoid，全程响应
		float ca = conductorAmount.get();
		float cv = a_conductorValue.load();
		float energy = 0.5f * (1.0f - ca) + cv * ca;
		modIndexInit = stageFM.blendRange(energy, modIndexInit * 0.5f, modIndexInit, ca);
	}

	// modIndex 衰减（独立于 carrier）
	// Tail 调制：tail 长度 × depth × 400ms 加到 base idxDecay 上（正相关）
	float tailInf = a_tailInfluence.load();   // 0..1
	float idxDecayMod = tailInf * tailToIdxDecayDepth.get() * 400.0f;
	float effIdxDecayMs = fmIndexDecayMs.get() + idxDecayMod;
	float modIdxDecaySamples = effIdxDecayMs * 0.001f * sampleRate;
	float modIndexDecay = expf(-1.0f / std::max(modIdxDecaySamples, 1.0f));

	TriggerEvent te;
	te.carrierFreq     = carrierFreq;
	te.modFreq         = modFreq;
	te.carrierAmp      = gain;
	te.carrierDecay    = baseDecay;
	te.modIndex        = modIndexInit;
	te.modIndexDecay   = modIndexDecay;
	te.panL            = panL;
	te.panR            = panR;
	te.attackSamples   = attackSamples;

	int w = ringWrite.load(std::memory_order_relaxed);
	int next = (w + 1) & (RING_SIZE - 1);
	if (next == ringRead.load(std::memory_order_acquire)) {
		return;
	}
	eventRing[w] = te;
	ringWrite.store(next, std::memory_order_release);
}

//--------------------------------------------------------------
//  HUD 用：当前活跃的 drone voice 数
//--------------------------------------------------------------
int Synth::getActiveDroneCount() const {
	int count = 0;
	for (int i = 0; i < NUM_DRONE_VOICES; i++) {
		if (droneVoices[i].active.load()) count++;
	}
	return count;
}

//--------------------------------------------------------------
//  给 Flock3D trail 用：归一化的音频活跃度
//  三个核心 synth 参数都映射到 [0..1] 然后平均：
//    event decay (50..500 ms) — 越长事件越长
//    FM ratio (0.5..8)         — 越高泛音越复杂
//    cluster cutoff (80..8000) — 越高 drone 越亮
//  全部"长 / 亮 / 复杂" → 视觉尾巴正相关变长
//--------------------------------------------------------------
float Synth::getAudioInfluenceForTail() const {
	float e = ofClamp((eventDecayMs.get()   - 50.0f)   / 450.0f,  0.0f, 1.0f);
	float f = ofClamp((fmRatio.get()        - 0.5f)    / 7.5f,    0.0f, 1.0f);
	float c = ofClamp((clusterCutoff.get()  - 80.0f)   / 7920.0f, 0.0f, 1.0f);
	return (e + f + c) / 3.0f;
}

//==============================================================
//  Cluster Drone — 主线程：和声优先级 + 不重复的 pitch 分配
//  - chord 优先级（按典型 chord 排序，先 root/octave/5th，再 3rd 等）
//  - 优先级中跳过被任一活 voice 占用的半音
//  - 拿到候选半音后再 quantize 到当前 scale（与 events 共调）
//==============================================================
int Synth::pickFreshSemitone() const {
	// chord-priority 半音偏移（从 rootFreq 起算）
	// 经典 chord：root, 8va, 5th, 8va+5th, 2 octs, M3, m3, 11, 7th, M6...
	// 含负值（去低于 root 的位置）增加变化
	static const int priority[] = {
		0, 12, 7, 24, 19,                  // root + 5th 系
		4, 16, 3, 15,                      // M3 / m3 + 8va
		11, 23, 10, 22,                    // M7 / m7
		9, 21, 2, 14,                      // M6 / M2
		-12, -5, 5, 17,                    // 8va 下、4th 系
		36, -24                            // 极端高低（fallback）
	};
	const int nPriority = sizeof(priority) / sizeof(priority[0]);

	// 收集已用半音
	std::vector<int> used;
	used.reserve(NUM_DRONE_VOICES);
	for (int i = 0; i < NUM_DRONE_VOICES; i++) {
		if (droneTracking[i].active) {
			used.push_back(droneTracking[i].semitone);
		}
	}

	for (int k = 0; k < nPriority; k++) {
		int candidate = priority[k];

		// 量化到当前 scale（与 event 同调）
		float root = rootFreq;
		float candFreq = root * powf(2.0f, candidate / 12.0f);
		float quantFreq = quantizeToScale(candFreq);
		int quantSemi = (int)roundf(12.0f * log2f(quantFreq / root));

		bool isUsed = false;
		for (int u : used) {
			if (u == quantSemi) { isUsed = true; break; }
		}
		if (!isUsed) return quantSemi;
	}

	// 全占满（极少见）→ 找一个还没用的高位
	int maxUsed = -100;
	for (int u : used) if (u > maxUsed) maxUsed = u;
	return maxUsed + 12;
}

//==============================================================
//  Cluster Drone — 主线程：根据 cluster 列表分配 / 匹配 / 释放 voice
//  - 匹配规则：cluster 与"已激活 voice 的 trackedPos" 邻近 → 复用
//  - 没匹配：分配空闲 voice，新计算 pitch（quantize 到 scale）
//  - 没新 cluster 跟踪的 voice → 启动 fadeout 倒计时
//  - fadeout 倒计时完 → 释放 voice 槽
//==============================================================
void Synth::updateClusterVoices(const std::vector<Flock3D::Cluster>& clusters, float wr){
	a_worldRadius.store(wr);

	// ─── Scale 切换检测：所有活 voice 重新对齐到新 scale（保留 currentFreq → 自动 glide）───
	int curScale = scaleType.get();
	if (curScale != lastScaleType) {
		lastScaleType = curScale;
		for (int i = 0; i < NUM_DRONE_VOICES; i++) {
			if (!droneTracking[i].active) continue;
			// 用当前 semitone 重新量化到新 scale → 新的 targetFreq
			int   oldSemi  = droneTracking[i].semitone;
			float candFreq = rootFreq * powf(2.0f, oldSemi / 12.0f);
			float newFreq  = quantizeToScale(candFreq);
			int   newSemi  = (int)roundf(12.0f * log2f(newFreq / rootFreq));
			droneTracking[i].semitone = newSemi;
			droneVoices[i].targetFreq.store(newFreq);
			// currentFreq 不重置 → 在音频线程里以 glideCoef 平滑过渡
		}
	}

	std::array<bool, NUM_DRONE_VOICES> matched{};
	matched.fill(false);

	float proximity = clusterProximity.get();
	float proxSq = proximity * proximity;

	// ─── Pass 1: 给每个 cluster 找 voice ───
	for (const auto& c : clusters) {
		// (a) 找邻近已激活 voice
		int bestIdx = -1;
		float bestDistSq = proxSq;
		for (int i = 0; i < NUM_DRONE_VOICES; i++) {
			if (matched[i]) continue;
			if (!droneTracking[i].active) continue;
			glm::vec3 d = droneTracking[i].trackedPos - c.centroid;
			float distSq = glm::dot(d, d);
			if (distSq < bestDistSq) {
				bestDistSq = distSq;
				bestIdx = i;
			}
		}

		// (b) 没邻近 → 分配空闲槽（活但不需是当前正激活的）
		bool isNewVoice = false;
		if (bestIdx < 0) {
			for (int i = 0; i < NUM_DRONE_VOICES; i++) {
				if (!matched[i] && !droneTracking[i].active) {
					bestIdx = i;
					isNewVoice = true;
					break;
				}
			}
		}

		if (bestIdx < 0) continue;   // 全部 voice 被占 → 这个 cluster 暂时没声音

		auto& tr = droneTracking[bestIdx];
		auto& v  = droneVoices[bestIdx];

		if (isNewVoice) {
			// 新 voice：用和声优先级分配未占用的半音
			tr.active = true;
			int semi = pickFreshSemitone();
			tr.semitone = semi;
			float freq = rootFreq * powf(2.0f, semi / 12.0f);

			// CRITICAL：重置音频线程本地状态，避免上次 voice 残留导致 attack 从中间值开始
			// 此时 active 还是 false（audio thread 不会读这些字段），安全写入
			v.currentVol  = 0.0f;       // attack 必须从 0 起
			v.currentFreq = freq;       // 不要 glide，直接到目标频率
			v.currentPan  = 0.5f;       // 等下面 targetPan 设置后会平滑过渡
			v.phase[0]    = 0.0f;
			v.phase[1]    = 0.333f;
			v.phase[2]    = 0.666f;
			v.svfLow      = 0.0f;
			v.svfBand     = 0.0f;

			v.targetFreq.store(freq);
			// 最后才设 active=true，确保音频线程读到的是完整初始化的 voice
			v.active.store(true);
		}
		// 更新跟踪位置 + 目标音量 + pan
		tr.trackedPos = c.centroid;
		tr.fadeoutSec = 0.0f;   // 重置 fadeout（如果之前在淡出，rebound 回 sustain）
		v.targetVol.store(1.0f);
		float pan = ofClamp(c.centroid.x / (wr * 2.0f) + 0.5f, 0.0f, 1.0f);
		v.targetPan.store(pan);
		matched[bestIdx] = true;
	}

	// ─── Pass 2: 没匹配上的活 voice → fadeout（时间精准）───
	// 用实际 frame time 而不是假设 60fps，避免帧率波动让 audio 还没到 0 就被切断
	float dt = ofGetLastFrameTime();
	if (dt > 0.1f) dt = 0.1f;
	float releaseSec = clusterReleaseMs.get() * 0.001f;
	// 多给 5% 余量，确保音频侧 envelope 真的到 0 后才释放 voice
	float releaseWithMargin = releaseSec * 1.05f;

	for (int i = 0; i < NUM_DRONE_VOICES; i++) {
		if (matched[i]) continue;
		if (!droneTracking[i].active) continue;

		if (droneTracking[i].fadeoutSec <= 0.0f) {
			// 启动 fadeout
			droneVoices[i].targetVol.store(0.0f);
			droneTracking[i].fadeoutSec = releaseWithMargin;
		} else {
			droneTracking[i].fadeoutSec -= dt;
			if (droneTracking[i].fadeoutSec <= 0.0f) {
				// 完全淡出 → 释放（此时 audio envelope 已严格归零，无 cliff）
				droneTracking[i].active = false;
				droneVoices[i].active.store(false);
			}
		}
	}
}

//==============================================================
//  音频线程：生成样本
//==============================================================
void Synth::audioOut(ofSoundBuffer& buffer){
	int n = buffer.getNumFrames();
	int ch = buffer.getNumChannels();
	if (ch < 2) return;

	if (!audioEnabled) {
		// 输出静音
		for (int i = 0; i < n * ch; i++) buffer[i] = 0.0f;
		return;
	}

	float master = masterVol;
	float verbAmt = reverbAmt;
	float evtVol = eventVol;

	// ─── 把 ring buffer 里的 trigger event 分配给空闲 event voices ───
	int r = ringRead.load(std::memory_order_relaxed);
	int wEnd = ringWrite.load(std::memory_order_acquire);
	while (r != wEnd) {
		const TriggerEvent& te = eventRing[r];

		// 找空闲 voice，或偷 carrier amp 最小的
		int targetIdx = -1;
		float minAmp = 1e9;
		for (int i = 0; i < NUM_EVENT_VOICES; i++) {
			if (!eventVoices[i].active) { targetIdx = i; break; }
			if (eventVoices[i].carrierAmp < minAmp) {
				minAmp = eventVoices[i].carrierAmp;
				targetIdx = i;
			}
		}
		if (targetIdx >= 0) {
			auto& v = eventVoices[targetIdx];
			v.active         = true;
			v.attackCounter  = 0;
			v.attackSamples  = te.attackSamples;
			v.panL           = te.panL;
			v.panR           = te.panR;

			v.carrierFreq    = te.carrierFreq;
			v.carrierPhase   = 0.0f;
			v.carrierAmp     = te.carrierAmp;
			v.carrierDecay   = te.carrierDecay;

			v.modFreq        = te.modFreq;
			v.modPhase       = 0.123f;     // 错开避免 click
			v.modIndex       = te.modIndex;
			v.modIndexDecay  = te.modIndexDecay;
		}
		r = (r + 1) & (RING_SIZE - 1);
	}
	ringRead.store(r, std::memory_order_release);

	// ─── EnergyStage 编排式响应（rp-37）───
	// 不再用统一 scalar 喷所有参数。每个参数有自己的激活窗口 + 曲线 ——
	// 论文 Spectromorphology 的"参数在能量轴上错峰进场"。
	// energy = 在 conductorAmount 控制下，conductor.value 跟 0.5 之间的混合
	float energy;
	{
		float cv = a_conductorValue.load();
		float ca = conductorAmount.get();
		energy = 0.5f * (1.0f - ca) + cv * ca;
	}
	float condAmt = conductorAmount.get();

	// 预设激活窗口（论文式编排：风铺底 → drone 中段 → cutoff/fold/FM 留高能段）
	static const EnergyStage stageWind    {0.0f, 0.5f, 3};  // sigmoid，最早进
	static const EnergyStage stageDroneV  {0.3f, 0.7f, 3};  // sigmoid，中段
	static const EnergyStage stageCutoff  {0.2f, 0.9f, 2};  // log，慢慢开亮
	static const EnergyStage stageFold    {0.5f, 1.0f, 1};  // exp，高能晚进金属感
	static const EnergyStage stageEvtVol  {0.0f, 1.0f, 3};  // sigmoid，全程；event vol 60%..100%
	// (FM modIndex stage 在 triggerCollision 使用)

	// Event vol 微弱 staging（60% floor → 100%）：低能段 event 稍轻不消失
	float evtVolStaged = stageEvtVol.blendRange(energy, eventVol.get() * 0.6f,
	                                            eventVol.get(), condAmt);

	// ─── 预算 SVF cutoff / resonance 系数（per-buffer，每 sample 不变）───
	// stageCutoff: 200Hz (dark) → userCutoff，能量 0.2..0.9 慢慢开亮（log 曲线）
	float baseCutoff = ofClamp(clusterCutoff.get(), 20.0f, sampleRate * 0.4f);
	float cutoff = ofClamp(stageCutoff.blendRange(energy, 200.0f, baseCutoff, condAmt),
	                       20.0f, sampleRate * 0.4f);
	float svfFc = 2.0f * sinf(PI * cutoff / sampleRate);
	if (svfFc > 0.99f) svfFc = 0.99f;
	float svfQ  = 1.0f - ofClamp(clusterResonance.get(), 0.0f, 0.95f);
	if (svfQ < 0.05f) svfQ = 0.05f;

	// 线性包络：每 sample 增量 = 1 / (attackMs * sampleRate / 1000)
	// 精准时长：经过 attackMs 后 currentVol 严格 = 1.0；release 同理严格 = 0.0
	// 避免指数包络在固定时间内只到 0.37 → 被 fadeoutFrames 掐断的 cliff
	float attackSamples  = std::max(1.0f, clusterAttackMs.get()  * 0.001f * sampleRate);
	float releaseSamples = std::max(1.0f, clusterReleaseMs.get() * 0.001f * sampleRate);
	float attackPerSample  = 1.0f / attackSamples;
	float releasePerSample = 1.0f / releaseSamples;
	float detune = clusterDetune;
	float detuneRatios[3] = {1.0f, 1.0f + detune, 1.0f - detune};

	// Glide 系数：exp 衰减，~ glideMs 内到达 95% 目标
	float glideMs = ofClamp(droneGlideMs.get(), 1.0f, 10000.0f);
	float glideSamples = glideMs * 0.001f * sampleRate;
	float glideCoef = 3.0f / std::max(1.0f, glideSamples);
	if (glideCoef > 1.0f) glideCoef = 1.0f;

	// ─── Drone Wave Folder ───
	// 用 stageFold 编排，fold 只在高能段（energy > 0.5）开始展开
	float baseFold  = clusterDroneFold.get();
	float effFold   = stageFold.blend(energy, baseFold, condAmt);
	float foldDrive = 1.0f + effFold * 5.0f;
	bool foldActive = effFold > 0.001f;

	// ─── Event Wave Folder（独立于 drone fold）───
	// 共用 stageFold（同样高能晚进），但用 user 自己设的 eventFoldAmount 作 base
	// → drone 和 event 可分别开关 fold 量
	float baseEventFold  = eventFoldAmount.get();
	float effEventFold   = stageFold.blend(energy, baseEventFold, condAmt);
	float eventFoldDrive = 1.0f + effEventFold * 5.0f;
	bool eventFoldActive = effEventFold > 0.001f;

	// ─── 预算 Wind 层参数（per-buffer，每 sample 不变）───
	// stageWind：风声 0..0.5 能量段就饱和（铺底纹理，最先进场）
	float wndVol = stageWind.blend(energy, windVol.get(), condAmt);
	float wndCutoffBase = ofClamp(windCutoff.get(), 50.0f, sampleRate * 0.4f); // base cutoff
	float wndQ = 1.0f - ofClamp(windResonance.get(), 0.0f, 0.95f);
	if (wndQ < 0.05f) wndQ = 0.05f;
	float wndFieldAmp = a_fieldAmpTotal.load();   // 0..1
	// amp 把 cutoff 抬高：amp=1 + depth=1 → +4000Hz；depth=3 → +12000Hz
	float wndAmpCutoffShift = wndFieldAmp * windAmpToCutoff.get() * 4000.0f;
	float wndLfoIncr = ofClamp(windLfoRate.get(), 0.0f, 10.0f) / sampleRate;
	float wndLfoD = windLfoDepth;

	// per-sample 平滑目标（per-buffer 常量，每 sample 渐进逼近）
	// 解决 conductor 能量突变时各值阶跃 → 11ms buffer 边界 click/distortion
	// stageDroneV: drone 在能量 0.3..0.7 段渐入（sigmoid，中段乐器）
	float cdrVolTarget    = stageDroneV.blend(energy, clusterDroneVol.get(), condAmt);
	float svfFcTarget     = svfFc;       // 已 clamp
	float foldDriveTarget = foldDrive;
	float wndVolTarget    = wndVol;
	float evtVolTarget    = evtVolStaged;   // event vol 也加平滑（之前漏了）
	float granVolTarget   = stageDroneV.blend(energy, granVol.get(), condAmt);   // granular 也 staging

	// Granular per-buffer：算 effective rate = base + cluster × influence
	int   curClusters    = a_clusterCount.load();
	float effGrainRate   = grainBaseRate.get() + (float)curClusters * granClusterInfluence.get();
	if (effGrainRate < 0.01f) effGrainRate = 0.01f;
	float samplesPerGrain = (float)sampleRate / effGrainRate;
	float grainAttackF = ofClamp(grainAttackFrac.get(), 0.02f, 0.5f);
	float invAttackF   = 1.0f / grainAttackF;
	float invDecayF    = 1.0f / (1.0f - grainAttackF);

	for (int i = 0; i < n; i++) {
		float left = 0, right = 0;

		// 每 sample 渐进（1-pole）；coef 0.0003 → tau ≈ 50ms（比 16ms 更柔）
		const float SMOOTH = 0.0003f;
		cdrVolSmooth    += (cdrVolTarget    - cdrVolSmooth)    * SMOOTH;
		svfFcSmooth     += (svfFcTarget     - svfFcSmooth)     * SMOOTH;
		foldDriveSmooth += (foldDriveTarget - foldDriveSmooth) * SMOOTH;
		windVolSmooth   += (wndVolTarget    - windVolSmooth)   * SMOOTH;
		evtVolSmooth    += (evtVolTarget    - evtVolSmooth)    * SMOOTH;
		granVolSmooth   += (granVolTarget   - granVolSmooth)   * SMOOTH;

		// ───────────────────────────────
		// A. Cluster Drone — 多声部，每 voice = 3 detuned saw + SVF lowpass
		// ───────────────────────────────
		{
			float cdrVol = cdrVolSmooth;
			float cdrSumL = 0, cdrSumR = 0;
			for (int v = 0; v < NUM_DRONE_VOICES; v++) {
				auto& dv = droneVoices[v];
				if (!dv.active.load()) continue;

				float tVol  = dv.targetVol.load();
				float tFreq = dv.targetFreq.load();
				float tPan  = dv.targetPan.load();

				// 线性包络（精准时长，到点严格归零/满值）
				if (tVol > dv.currentVol) {
					dv.currentVol += attackPerSample;
					if (dv.currentVol > tVol) dv.currentVol = tVol;
				} else if (tVol < dv.currentVol) {
					dv.currentVol -= releasePerSample;
					if (dv.currentVol < tVol) dv.currentVol = tVol;
				}

				// Pitch glide：用 droneGlideMs 控制，scale 切换时这里自动滑过去
				dv.currentFreq += (tFreq - dv.currentFreq) * glideCoef;
				dv.currentPan  += (tPan - dv.currentPan)  * 0.001f;

				if (dv.currentVol <= 0.0f && tVol <= 0.0f) continue;

				// 3 detuned saws 叠加（PolyBLEP 抗混叠）
				float rawSample = 0;
				for (int s = 0; s < 3; s++) {
					float f = dv.currentFreq * detuneRatios[s];
					if (f > sampleRate * 0.45f) f = sampleRate * 0.45f;
					float dt = f / sampleRate;
					rawSample += saw(dv.phase[s], dt);
					dv.phase[s] += dt;
					if (dv.phase[s] >= 1.0f) dv.phase[s] -= 1.0f;
				}
				rawSample *= (1.0f / 3.0f);   // 3 saws 归一化

				// SVF state-variable lowpass（per-voice 独立滤波）
				// 用 svfFcSmooth（per-sample smoothed）避免 cutoff 阶跃 → click
				dv.svfLow  += svfFcSmooth * dv.svfBand;
				float svfHigh = rawSample - dv.svfLow - svfQ * dv.svfBand;
				dv.svfBand += svfFcSmooth * svfHigh;
				float filtered = dv.svfLow;

				float sample = filtered * dv.currentVol;

				float panL = cosf(dv.currentPan * HALF_PI);
				float panR = sinf(dv.currentPan * HALF_PI);
				cdrSumL += sample * panL;
				cdrSumR += sample * panR;
			}
			cdrSumL *= cdrVol * (2.0f / NUM_DRONE_VOICES);
			cdrSumR *= cdrVol * (2.0f / NUM_DRONE_VOICES);

			// Wave folder：sin(x · drive) 加密谐波（西海岸合成）
			// 仅当 effFold > 0 才折，否则直通省 sinf 开销
			// 用 foldDriveSmooth 避免 drive 阶跃产生的瞬时金属化爆裂
			if (foldActive) {
				cdrSumL = sinf(cdrSumL * foldDriveSmooth);
				cdrSumR = sinf(cdrSumR * foldDriveSmooth);
			}

			left  += cdrSumL;
			right += cdrSumR;
		}

		// ───────────────────────────────
		// B. EventVoices — 短促粒子触发声（2-op FM 合成）
		//   carrier 承载振幅包络，modulator 调制 carrier 相位
		//   modIndex 独立衰减 → 自然 bell envelope（亮起音 → 温暖尾音）
		// ───────────────────────────────
		float eventSumL = 0, eventSumR = 0;
		for (int v = 0; v < NUM_EVENT_VOICES; v++) {
			auto& vc = eventVoices[v];
			if (!vc.active) continue;

			// Attack envelope（防起音 click）
			float envAttack = 1.0f;
			if (vc.attackCounter < vc.attackSamples) {
				envAttack = (float)vc.attackCounter / (float)vc.attackSamples;
				vc.attackCounter++;
			}

			// Modulator
			float modSample = sinf(vc.modPhase * TWO_PI) * vc.modIndex;
			vc.modPhase += vc.modFreq / sampleRate;
			if (vc.modPhase >= 1.0f) vc.modPhase -= 1.0f;
			vc.modIndex *= vc.modIndexDecay;

			// Carrier with phase modulation
			float voiceSample = sinf(vc.carrierPhase * TWO_PI + modSample) * vc.carrierAmp;
			vc.carrierPhase += vc.carrierFreq / sampleRate;
			if (vc.carrierPhase >= 1.0f) vc.carrierPhase -= 1.0f;
			vc.carrierAmp *= vc.carrierDecay;

			if (vc.carrierAmp < 0.00001f) {
				vc.active = false;
				continue;
			}

			voiceSample *= envAttack;

			eventSumL += voiceSample * vc.panL;
			eventSumR += voiceSample * vc.panR;
		}
		// 归一化：除以 voice 数量；用 staged event vol（60%..100% 区间）
		eventSumL *= evtVolSmooth * (2.0f / NUM_EVENT_VOICES);
		eventSumR *= evtVolSmooth * (2.0f / NUM_EVENT_VOICES);

		// Event Wave Folder：sin(x · drive) 加密谐波（高能段事件更金属）
		if (eventFoldActive) {
			eventSumL = sinf(eventSumL * eventFoldDrive);
			eventSumR = sinf(eventSumR * eventFoldDrive);
		}

		left  += eventSumL;
		right += eventSumR;

		// ───────────────────────────────
		// C. Wind — 持续滤波噪声（field amp 总和驱动音量）
		// L/R 各自独立白噪声 + SVF lowpass → 自然立体声
		// LFO 微调 cutoff → 风阵 gust 感
		// 信号在 reverb 之前，因此也会被 hall reverb 处理
		// ───────────────────────────────
		if (wndVol > 0.001f) {
			// LFO 调制 cutoff（gust）
			windLfoPhase += wndLfoIncr;
			if (windLfoPhase >= 1.0f) windLfoPhase -= 1.0f;
			float lfo = sinf(windLfoPhase * TWO_PI);   // -1..1

			// 综合 cutoff = (base + amp 抬高) × LFO 调制因子
			// field amp 高 → 风更亮锐；amp 低 → 风暗闷
			float curCutoff = (wndCutoffBase + wndAmpCutoffShift) * (1.0f + lfo * wndLfoD * 0.6f);
			if (curCutoff < 50.0f) curCutoff = 50.0f;
			if (curCutoff > sampleRate * 0.4f) curCutoff = sampleRate * 0.4f;
			float wndFc = 2.0f * sinf(PI * curCutoff / sampleRate);
			if (wndFc > 0.99f) wndFc = 0.99f;

			// 白噪声（L/R 独立）：std::rand() / RAND_MAX 范围 0..1 → 居中到 -1..1
			float nL = (float)std::rand() * (2.0f / (float)RAND_MAX) - 1.0f;
			float nR = (float)std::rand() * (2.0f / (float)RAND_MAX) - 1.0f;

			// SVF lowpass L
			windSvfLowL  += wndFc * windSvfBandL;
			float highL = nL - windSvfLowL - wndQ * windSvfBandL;
			windSvfBandL += wndFc * highL;

			// SVF lowpass R
			windSvfLowR  += wndFc * windSvfBandR;
			float highR = nR - windSvfLowR - wndQ * windSvfBandR;
			windSvfBandR += wndFc * highR;

			// 音量独立（不被 field amp 影响）；用 windVolSmooth 防 buffer 阶跃 click
			float wL = windSvfLowL * windVolSmooth;
			float wR = windSvfLowR * windVolSmooth;

			left  += wL;
			right += wR;
		}

		// ───────────────────────────────
		// C2. Granular layer — cluster 数高时颗粒云（rp-44）
		// 每帧调度新 grain；处理活 grain（windowed 读 source + pitch shift + pan）
		// 用 try_lock 保护 grainSource swap：主线程拖文件加载时短暂跳过 granular
		// 一帧（避免读越界 / vector relocation 中读到旧地址）
		// ───────────────────────────────
		std::unique_lock<std::mutex> grainLock(grainSourceMutex, std::try_to_lock);
		if (grainLock.owns_lock())
		{
			// 调度新 grain
			grainSchedAccum -= 1.0f;
			if (grainSchedAccum <= 0.0f) {
				for (int g = 0; g < NUM_GRAINS; g++) {
					if (grains[g].active) continue;
					grains[g].active = true;
					grains[g].age = 0;
					grains[g].length = (int)(grainSizeMs.get() * 0.001f * sampleRate);
					if (grains[g].length < 1) grains[g].length = 1;
					// pitch = 中心 offset + 随机 [-spread, +spread] 半音
					float offset   = grainPitchOffset.get();
					float spread   = grainPitchSpread.get();
					float jitter   = (((float)std::rand() / (float)RAND_MAX) * 2.0f - 1.0f) * spread;
					float pitchSemi = offset + jitter;
					grains[g].pitch = powf(2.0f, pitchSemi / 12.0f);
					// 随机源位置（确保有完整 grain length 可读）
					// 高 pitch 时一个 grain 会读 length × pitch 个 source sample
					// → 用 ceil(pitch + 1) 当 safety margin（高 pitch 也不会提前截断）
					int marginMul = std::max(2, (int)std::ceil(grains[g].pitch + 1.0f));
					int maxStart = grainSourceLen - grains[g].length * marginMul;
					if (maxStart < 1) maxStart = 1;
					grains[g].readPos = (float)(std::rand() % maxStart);
					// 随机 pan
					float pSpread = grainPanSpread.get();
					float panRandom = (1.0f - pSpread) * 0.5f
					                 + ((float)std::rand() / (float)RAND_MAX) * pSpread;
					grains[g].panL = cosf(panRandom * HALF_PI);
					grains[g].panR = sinf(panRandom * HALF_PI);
					break;
				}
				grainSchedAccum += samplesPerGrain;
				if (grainSchedAccum < 1.0f) grainSchedAccum = samplesPerGrain;  // safety
			}

			// 处理活 grain
			float gL = 0.0f, gR = 0.0f;
			for (auto& gr : grains) {
				if (!gr.active) continue;
				int idx = (int)gr.readPos;
				if (idx >= grainSourceLen - 1) {
					gr.active = false;
					continue;
				}
				float frac = gr.readPos - (float)idx;
				float s = grainSource[idx] * (1.0f - frac) + grainSource[idx + 1] * frac;
				// 三角包络：可调 attack 比例 → "短 attack + 长 decay" = pluck/click
				// attack 段（t < attackF）：env = t / attackF（线性 0→1）
				// decay 段（t >= attackF）：env = 1 - (t - attackF) / (1 - attackF)（线性 1→0）
				float t = (float)gr.age / (float)gr.length;
				float env;
				if (t < grainAttackF) {
					env = t * invAttackF;
				} else {
					env = (1.0f - t) * invDecayF;
				}
				s *= env;
				gL += s * gr.panL;
				gR += s * gr.panR;
				gr.readPos += gr.pitch;
				gr.age++;
				if (gr.age >= gr.length) gr.active = false;
			}
			// 总音量缩放（× 0.5 防多 grain 累加爆音）
			gL *= granVolSmooth * 0.5f;
			gR *= granVolSmooth * 0.5f;
			left  += gL;
			right += gR;
		}

		// ───────────────────────────────
		// D. Hall Reverb — Hadamard FDN + HF damping + pre-delay
		// ───────────────────────────────
		if (verbAmt > 0.001f) {
			// 干信号（pre-delay 前）
			float inMix = (left + right) * 0.5f;

			// Pre-delay：input 先延迟，再喂给 reverb（"距离感"）
			int preDelaySamples = (int)(reverbPreDelayMs.get() * 0.001f * sampleRate);
			if (preDelaySamples < 0) preDelaySamples = 0;
			if (preDelaySamples >= preDelayBufLen) preDelaySamples = preDelayBufLen - 1;

			preDelayBuf[preDelayWrite] = inMix;
			int preReadIdx = preDelayWrite - preDelaySamples;
			if (preReadIdx < 0) preReadIdx += preDelayBufLen;
			float reverbInput = preDelayBuf[preReadIdx];
			preDelayWrite = (preDelayWrite + 1) % preDelayBufLen;

			// 读 4 条 delay 当前输出
			float d[NUM_REVERB_DELAYS];
			for (int k = 0; k < NUM_REVERB_DELAYS; k++) {
				d[k] = delayBuf[k][delayWrite[k]];
			}

			// HF damping（每条 delay 自己的 1-pole lowpass）
			// damp=0 → 系数=1（无衰减，bright）；damp=1 → 系数=0（深度衰减，dark）
			float dampCoef = 1.0f - reverbDamp.get();
			if (dampCoef < 0.01f) dampCoef = 0.01f;
			float damped[NUM_REVERB_DELAYS];
			for (int k = 0; k < NUM_REVERB_DELAYS; k++) {
				dampLpState[k] += dampCoef * (d[k] - dampLpState[k]);
				damped[k] = dampLpState[k];
			}

			// Hadamard 4×4 mix（能量守恒的"旋转"，把 4 条 delay 互相散开）
			// h0 =  d0 + d1 + d2 + d3 } 都 *0.5
			// h1 =  d0 - d1 + d2 - d3
			// h2 =  d0 + d1 - d2 - d3
			// h3 =  d0 - d1 - d2 + d3
			float h[4];
			h[0] = (damped[0] + damped[1] + damped[2] + damped[3]) * 0.5f;
			h[1] = (damped[0] - damped[1] + damped[2] - damped[3]) * 0.5f;
			h[2] = (damped[0] + damped[1] - damped[2] - damped[3]) * 0.5f;
			h[3] = (damped[0] - damped[1] - damped[2] + damped[3]) * 0.5f;

			// 写回 delay buffer：input + feedback × mixed
			float feedback = reverbSize.get();
			if (feedback > 0.97f) feedback = 0.97f;   // 防爆
			for (int k = 0; k < NUM_REVERB_DELAYS; k++) {
				delayBuf[k][delayWrite[k]] = reverbInput + h[k] * feedback;
				delayWrite[k] = (delayWrite[k] + 1) % delayLength[k];
			}

			// 立体声 spread：d0+d2 → L, d1+d3 → R
			float verbL = (d[0] + d[2]) * 0.5f;
			float verbR = (d[1] + d[3]) * 0.5f;

			left  += verbL * verbAmt;
			right += verbR * verbAmt;
		}

		// ───────────────────────────────
		// Master limiter + 输出
		// ───────────────────────────────
		left  = tanhf(left  * master);
		right = tanhf(right * master);

		buffer[i * ch + 0] = left;
		buffer[i * ch + 1] = right;
	}

	// ─── 实测 audio 能量（给 Synchresis 自感知用）───
	// 只用 RMS（不再混 peak）：512 samples 的平均功率本质就平滑
	// peak 是 transient (FM 事件)，会让 curve 颠簸，且每次 cluster merge 都跳一次
	// → 跟 visual energy（20K 粒子均值）的平滑度对齐
	// 归一化分母 0.08 + 用户可调 audioEnergyGain（默认 1，可上 5）
	float sumSq = 0.0f;
	for (int i = 0; i < n; i++) {
		float mono = (buffer[i * ch + 0] + buffer[i * ch + 1]) * 0.5f;
		sumSq += mono * mono;
	}
	float rms = sqrtf(sumSq / std::max(n, 1));
	float measuredE = (rms * audioEnergyGain.get()) / 0.08f;
	if (measuredE > 1.0f) measuredE = 1.0f;
	if (measuredE < 0.0f) measuredE = 0.0f;
	// 1-pole smoothing 强化：coef 0.05（之前 0.15）→ tau ≈ 150ms（之前 ~50ms）
	// 比 visual 那种"天然平均"差不多了
	float prev = a_audioEnergyMeasured.load(std::memory_order_relaxed);
	float smoothed = prev * 0.95f + measuredE * 0.05f;
	a_audioEnergyMeasured.store(smoothed, std::memory_order_relaxed);
}

//==============================================================
//  音阶 / 频率映射
//==============================================================
float Synth::quantizeToScale(float freq) const {
	// 把任意频率量化到当前 scale 的最近一个音
	// 每种 scale 用半音 offset 表示（相对 root），顺序对应 Synth::Scale enum
	static const std::vector<std::vector<int>> scales = {
		{0, 3, 5, 7, 10},          // SCALE_PENTA_MIN  五声小调
		{0, 2, 4, 7, 9},           // SCALE_PENTA_MAJ  五声大调
		{0, 2, 4, 5, 7, 9, 11},    // SCALE_MAJOR      自然大调（Ionian）
		{0, 2, 3, 5, 7, 8, 10},    // SCALE_MINOR_NAT  自然小调（Aeolian）
		{0, 2, 3, 5, 7, 9, 10},    // SCALE_DORIAN     多利亚
		{0, 2, 4, 5, 7, 9, 10},    // SCALE_MIXOLYDIAN 米索利底亚
		{0, 1, 3, 5, 7, 8, 10},    // SCALE_PHRYGIAN   弗里几亚（西班牙）
		{0, 2, 4, 6, 7, 9, 11},    // SCALE_LYDIAN     利底亚（梦幻 +4）
		{0, 3, 5, 6, 7, 10},       // SCALE_BLUES      蓝调
		{0, 2, 3, 7, 8},           // SCALE_HIRAJOSHI  平调子（日本）
		{0, 2, 4, 6, 8, 10},       // SCALE_WHOLE_TONE 全音
		{0, 12, 19, 24, 28, 31},   // SCALE_HARMONIC   谐波系列
	};
	int sIdx = ofClamp((int)scaleType, 0, (int)scales.size() - 1);
	const auto& scale = scales[sIdx];

	float root = rootFreq;
	float semis = 12.0f * log2f(freq / root);   // 当前频率离 root 多少半音
	int octave = (int)floorf(semis / 12.0f);
	float semInOct = semis - octave * 12;

	// 找最近的 scale step
	int bestStep = scale[0];
	float bestDist = 100.0f;
	for (int step : scale) {
		float d = fabsf(step - semInOct);
		if (d < bestDist) { bestDist = d; bestStep = step; }
	}

	float quantized = octave * 12 + bestStep;
	return root * powf(2.0f, quantized / 12.0f);
}

float Synth::massToFreq(float mass) const {
	// 质量 3..hundreds → 把对数映射到 [低频, 高频]
	// log10(3)=0.48, log10(300)=2.48
	float l = log10f(std::max(mass, 1.0f));
	float t = ofClamp((l - 0.5f) / 2.0f, 0.0f, 1.0f);   // 0..1
	// 反向：mass 大 → 低音
	t = 1.0f - t;
	float root = rootFreq;
	float octaves = 3.0f;
	float raw = root * powf(2.0f, t * octaves);
	return quantizeToScale(raw);
}

//==============================================================
//  PolyBLEP saw（抗混叠）
//==============================================================
float Synth::polyBlep(float t, float dt) const {
	if (t < dt) {
		t /= dt;
		return t + t - t * t - 1.0f;
	} else if (t > 1.0f - dt) {
		t = (t - 1.0f) / dt;
		return t * t + t + t + 1.0f;
	}
	return 0.0f;
}

float Synth::saw(float phase, float dt) const {
	float v = 2.0f * phase - 1.0f;
	v -= polyBlep(phase, dt);
	return v;
}
