#include "ofApp.h"

//==============================================================
//  setup — 在 main window 的上下文里调用
//==============================================================
void ofApp::setup(){
	ofSetFrameRate(60);
	ofSetVerticalSync(true);
	ofBackground(0);

	// ─── Flock + Synth + Conductor 参数 ───
	flock.buildGui(flockParams);
	flockGui.setup(flockParams);

	synth.buildGui(synthParams);
	synthGui.setup(synthParams);

	conductor.buildGui(morphologyParams);
	morphologyGui.setup(morphologyParams);

	synchresis.buildGui(synchresisParams);
	synchresisGui.setup(synchresisParams);

	scorePlayer.setup();
	scorePlayer.buildGui(scoreParams);
	scoreGui.setup(scoreParams);

	// 自动加载上次的设置（在 flock.setup 之前，让 setup 用上恢复值）
	if (ofFile::doesFileExist(ofToDataPath("flock_settings.xml"))) {
		flockGui.loadFromFile("flock_settings.xml");
		ofLogNotice() << "loaded flock_settings.xml";
	}
	if (ofFile::doesFileExist(ofToDataPath("synth_settings.xml"))) {
		synthGui.loadFromFile("synth_settings.xml");
		ofLogNotice() << "loaded synth_settings.xml";
	}
	if (ofFile::doesFileExist(ofToDataPath("morphology_settings.xml"))) {
		morphologyGui.loadFromFile("morphology_settings.xml");
		ofLogNotice() << "loaded morphology_settings.xml";
	}
	if (ofFile::doesFileExist(ofToDataPath("synchresis_settings.xml"))) {
		synchresisGui.loadFromFile("synchresis_settings.xml");
		ofLogNotice() << "loaded synchresis_settings.xml";
	}
	if (ofFile::doesFileExist(ofToDataPath("score_settings.xml"))) {
		scoreGui.loadFromFile("score_settings.xml");
		ofLogNotice() << "loaded score_settings.xml";
	}

	flock.setup(ofGetWidth(), ofGetHeight());
	conductor.setup();
	synchresis.setup();

	// ─── 音频引擎 ───
	int sampleRate  = 44100;
	int bufferSize  = 512;
	int numChannels = 2;

	ofSoundStreamSettings ssettings;
	ssettings.numInputChannels  = 0;
	ssettings.numOutputChannels = numChannels;
	ssettings.sampleRate        = sampleRate;
	ssettings.bufferSize        = bufferSize;
	ssettings.setOutListener(this);

	synth.setup(sampleRate, bufferSize);
	soundStream.setup(ssettings);

	ofLogNotice() << "Audio: " << sampleRate << "Hz, buffer " << bufferSize
	              << ", " << numChannels << "ch";

	// 注意：ImGui 在 drawGui() 第一次触发时才 init（确保它绑定到 gui window 的 events）
}

//==============================================================
//  ImGui theme
//==============================================================
void ofApp::applyImGuiTheme(){
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding    = 8.0f;
	style.FrameRounding     = 4.0f;
	style.GrabRounding      = 3.0f;
	style.TabRounding       = 4.0f;
	style.ScrollbarRounding = 6.0f;
	style.WindowPadding     = ImVec2(12, 10);
	style.FramePadding      = ImVec2(8, 4);
	style.ItemSpacing       = ImVec2(8, 6);
	style.WindowBorderSize  = 0.0f;
	style.FrameBorderSize   = 0.0f;
	style.WindowTitleAlign  = ImVec2(0.5f, 0.5f);

	ImVec4* c = style.Colors;
	c[ImGuiCol_WindowBg]            = ImVec4(0.07f, 0.08f, 0.10f, 1.00f);  // 独立 OS 窗口 → 不透明
	c[ImGuiCol_TitleBg]             = ImVec4(0.10f, 0.12f, 0.16f, 1.00f);
	c[ImGuiCol_TitleBgActive]       = ImVec4(0.20f, 0.30f, 0.55f, 1.00f);
	c[ImGuiCol_TitleBgCollapsed]    = ImVec4(0.10f, 0.12f, 0.16f, 0.80f);
	c[ImGuiCol_Header]              = ImVec4(0.18f, 0.22f, 0.32f, 0.55f);
	c[ImGuiCol_HeaderHovered]       = ImVec4(0.28f, 0.36f, 0.62f, 0.70f);
	c[ImGuiCol_HeaderActive]        = ImVec4(0.30f, 0.40f, 0.75f, 0.85f);
	c[ImGuiCol_Tab]                 = ImVec4(0.12f, 0.15f, 0.22f, 1.00f);
	c[ImGuiCol_TabHovered]          = ImVec4(0.30f, 0.42f, 0.78f, 1.00f);
	c[ImGuiCol_TabActive]           = ImVec4(0.35f, 0.45f, 0.85f, 1.00f);
	c[ImGuiCol_TabUnfocused]        = ImVec4(0.10f, 0.12f, 0.18f, 1.00f);
	c[ImGuiCol_TabUnfocusedActive]  = ImVec4(0.20f, 0.28f, 0.50f, 1.00f);
	c[ImGuiCol_FrameBg]             = ImVec4(0.15f, 0.18f, 0.24f, 0.80f);
	c[ImGuiCol_FrameBgHovered]      = ImVec4(0.22f, 0.28f, 0.42f, 0.85f);
	c[ImGuiCol_FrameBgActive]       = ImVec4(0.30f, 0.38f, 0.60f, 0.90f);
	c[ImGuiCol_SliderGrab]          = ImVec4(0.50f, 0.65f, 0.95f, 1.00f);
	c[ImGuiCol_SliderGrabActive]    = ImVec4(0.65f, 0.80f, 1.00f, 1.00f);
	c[ImGuiCol_Button]              = ImVec4(0.20f, 0.28f, 0.45f, 0.80f);
	c[ImGuiCol_ButtonHovered]       = ImVec4(0.30f, 0.40f, 0.65f, 0.95f);
	c[ImGuiCol_ButtonActive]        = ImVec4(0.40f, 0.50f, 0.85f, 1.00f);
	c[ImGuiCol_CheckMark]           = ImVec4(0.55f, 0.75f, 1.00f, 1.00f);
	c[ImGuiCol_Separator]           = ImVec4(0.25f, 0.30f, 0.40f, 0.50f);
	c[ImGuiCol_Text]                = ImVec4(0.92f, 0.94f, 0.98f, 1.00f);
	c[ImGuiCol_TextDisabled]        = ImVec4(0.55f, 0.60f, 0.70f, 1.00f);
}

//==============================================================
//  exit
//==============================================================
void ofApp::exit(){
	soundStream.close();
	flockGui.saveToFile("flock_settings.xml");
	synthGui.saveToFile("synth_settings.xml");
	morphologyGui.saveToFile("morphology_settings.xml");
	synchresisGui.saveToFile("synchresis_settings.xml");
	scoreGui.saveToFile("score_settings.xml");
	ofLogNotice() << "saved flock/synth/morphology/synchresis/score _settings.xml";
}

//==============================================================
//  update — 主线程（main window 上下文）
//==============================================================
void ofApp::update(){
	float dt = ofGetLastFrameTime();

	// 0. Score Player — 如果在播，覆写 conductor 的 mode/curve/etc 参数
	scorePlayer.update(dt, conductor);

	// 1. Morphology Conductor — 产生目标轨迹
	conductor.update(dt);
	float target = conductor.value();

	// 2. Synchresis — 系统对自身的感知 + 周期 cadence 校正
	//    （论文 Battey Fluid Audiovisual Counterpoint 的算法化）
	float audioE  = synth.getAudioEnergyMeasured();
	float visualE = flock.getVisualEnergyMeasured();
	synchresis.update(dt, target, audioE, visualE);

	// 3. 把 target + nudge 推给 synth / flock（仍在 [0..1] 范围）
	float audioTarget  = ofClamp(target + synchresis.audioCorrection(),  0.0f, 1.0f);
	float visualTarget = ofClamp(target + synchresis.visualCorrection(), 0.0f, 1.0f);
	synth.setConductorValue(audioTarget);
	flock.setConductorValue(visualTarget);

	flock.update();

	// Audio → Visual：音频活跃度 → trail 长度
	flock.setAudioInfluence(synth.getAudioInfluenceForTail());

	// Visual → Audio：tail 长度 → FM idxDecay
	synth.setTailInfluence(flock.getCurrentTailNormalized());

	// Field amp 总和 → 风声音量
	synth.setFieldAmpTotal(flock.getFieldAmpTotal());

	// Cluster 检测 → drone voice 池
	auto clusters = flock.getClusters(Synth::getMaxDroneVoices());
	synth.updateClusterVoices(clusters, flock.getWorldRadius());
	lastClusterCount = (int)clusters.size();
	// 把 cluster 数推给 Synth — granular grain rate 用（cluster 多 → 颗粒更密）
	synth.setClusterCount(lastClusterCount);

	// Mycelium 上一帧的 link 数 → Click 引擎驱动（菌丝越密 → click 越密）
	// 上一帧的值（draw 后）— 1 frame 延迟可忽略
	synth.setMyceliumDensity(flock.getMyceliumLinkCount());

	// 主线程 → synth：本帧的碰撞 → 触发 event 音
	for (const auto& ev : flock.getCollisionsThisFrame()) {
		synth.triggerCollision(ev);
	}
}

//==============================================================
//  draw — main window：只画 flock
//==============================================================
void ofApp::draw(){
	flock.draw();

	if (recording) {
		ofImage img;
		img.grabScreen(0, 0, ofGetWidth(), ofGetHeight());
		std::string fname = "recording_" + ofToString(frameNum, 5, '0') + ".png";
		img.save(fname);
		frameNum++;
	}

	// 录制指示器
	if (recording) {
		ofSetColor(255, 0, 0);
		ofDrawCircle(ofGetWidth() - 30, 30, 8);
		ofSetColor(255);
		ofDrawBitmapString("REC " + ofToString(frameNum), ofGetWidth() - 110, 35);
	}
}

//==============================================================
//  drawGui — gui window 的 draw 回调（独立 OS 窗口）
//==============================================================
void ofApp::drawGui(ofEventArgs&){
	// 第一次触发时初始化 ImGui — 此时正处于 gui window 的上下文，
	// engine.setup() 内部的 ofAddListener(ofEvents()...) 会绑定到这个窗口的 events
	if (!imguiInitialized) {
		imgui.setup();
		applyImGuiTheme();
		imguiInitialized = true;
	}

	ofBackground(15, 18, 24);   // GUI 窗口背景（暗灰蓝）

	imgui.begin();

	// 让 ImGui 主面板占满整个 gui window
	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImVec2((float)ofGetWidth(), (float)ofGetHeight()));

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
	                         ImGuiWindowFlags_NoResize |
	                         ImGuiWindowFlags_NoMove |
	                         ImGuiWindowFlags_NoCollapse |
	                         ImGuiWindowFlags_NoBringToFrontOnFocus;

	if (ImGui::Begin("of-flock-3d", nullptr, flags)) {
		// 顶部 HUD
		ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f),
		                   "clusters %d   drones %d/%d",
		                   lastClusterCount,
		                   synth.getActiveDroneCount(),
		                   Synth::getMaxDroneVoices());
		ImGui::SameLine();
		ImGui::TextDisabled("  fps %.1f", ofGetFrameRate());
		ImGui::Separator();

		// HUD 第二行：morphology conductor 状态
		ImGui::TextColored(ImVec4(0.85f, 0.7f, 0.4f, 1.0f),
		                   "morphology: %-13s value %.2f  phase %.2f",
		                   conductor.getModeName().c_str(),
		                   conductor.value(),
		                   conductor.phaseProgress());
		// HUD 第三行：synchresis 状态
		{
			float ss = synchresis.syncStrength();
			ImGui::TextColored(ImVec4(1.0f - ss, ss * 0.8f + 0.2f, ss * 0.5f + 0.3f, 1.0f),
			                   "sync:       %-13s   audio %.2f  visual %.2f",
			                   ss > 0.5f ? "← CADENCE" : "(counterpoint)",
			                   synth.getAudioEnergyMeasured(),
			                   flock.getVisualEnergyMeasured());
		}
		// HUD 第四行：score 状态（仅 playing 时显示）
		if (scorePlayer.isPlaying()) {
			ImGui::TextColored(ImVec4(0.95f, 0.7f, 0.85f, 1.0f),
			                   "score:      ▶ %s   %.1fs",
			                   scorePlayer.currentScoreName().c_str(),
			                   scorePlayer.elapsed());
		}
		ImGui::Separator();

		if (ImGui::BeginTabBar("MainTabs")) {
			if (ImGui::BeginTabItem("Morphology")) {
				ImGui::BeginChild("MorphScroll", ImVec2(0, 0), false);
				conductor.drawImGui();
				ImGui::Spacing();
				synchresis.drawImGui();
				ImGui::Spacing();
				scorePlayer.drawImGui();
				// Play button needs conductor ref → 在 ofApp 这里直接渲染
				if (!scorePlayer.isPlaying()) {
					if (ImGui::Button("▶ Play Score")) {
						scorePlayer.play(conductor);
					}
				} else {
					if (ImGui::Button("■ Stop Score")) {
						scorePlayer.stop();
					}
				}
				ImGui::EndChild();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Visual")) {
				ImGui::BeginChild("VisualScroll", ImVec2(0, 0), false);
				flock.drawImGui();
				ImGui::EndChild();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Synth")) {
				ImGui::BeginChild("SynthScroll", ImVec2(0, 0), false);
				synth.drawImGui();
				ImGui::EndChild();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Help")) {
				ImGui::TextDisabled("Keys (focus main window)");
				ImGui::BulletText("f : fullscreen");
				ImGui::BulletText("s : snapshot (PNG)");
				ImGui::BulletText("r : record PNG sequence");
				ImGui::BulletText("space : reset flock");
				ImGui::BulletText("mouse drag : orbit camera");
				ImGui::BulletText("scroll : zoom");
				ImGui::Separator();
				ImGui::TextDisabled("Status");
				ImGui::Text("recording : %s", recording ? "ON" : "OFF");
				ImGui::Text("audio influence (→ tail) : %.2f",
				            synth.getAudioInfluenceForTail());
				ImGui::Text("tail (→ FM idxDecay) : %.2f",
				            flock.getCurrentTailNormalized());
				ImGui::Text("field amp total (→ wind) : %.2f",
				            flock.getFieldAmpTotal());
				ImGui::Text("morphology conductor : %.2f (%s)",
				            conductor.value(), conductor.getModeName().c_str());
				ImGui::Separator();
				ImGui::TextDisabled("Synchresis (self-aware coupling)");
				ImGui::Text("audio energy (measured) : %.2f",
				            synth.getAudioEnergyMeasured());
				ImGui::Text("visual energy (measured) : %.2f",
				            flock.getVisualEnergyMeasured());
				ImGui::Text("sync strength : %.2f", synchresis.syncStrength());
				ImGui::Text("audio nudge : %+.3f", synchresis.audioCorrection());
				ImGui::Text("visual nudge : %+.3f", synchresis.visualCorrection());
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}
	ImGui::End();

	imgui.end();
}

//==============================================================
//  audio
//==============================================================
void ofApp::audioOut(ofSoundBuffer& buffer){
	synth.audioOut(buffer);
}

//==============================================================
//  keys — 主窗口键盘（不再有 'h' 切换 GUI，因为 GUI 是独立窗口）
//==============================================================
void ofApp::keyPressed(int key){
	switch (key) {
		case 'f': case 'F': ofToggleFullscreen(); break;
		case 's': case 'S': {
			ofImage img;
			img.grabScreen(0, 0, ofGetWidth(), ofGetHeight());
			std::string fname = "snapshot_" + ofGetTimestampString() + ".png";
			img.save(fname);
			ofLogNotice() << "saved " << fname;
			break;
		}
		case 'r': case 'R':
			recording = !recording;
			if (recording) frameNum = 0;
			ofLogNotice() << "recording " << (recording ? "ON" : "OFF");
			break;
		case ' ':
			flock.reset();
			ofLogNotice() << "flock reset";
			break;
		default:
			flock.keyPressed(key);
			break;
	}
}

//==============================================================
void ofApp::windowResized(int w, int h){
	flock.setup(w, h);
}

//==============================================================
//  Drag & drop — 拖 wav 文件进任一窗口 → 换 granular 源
//  （当前只支持 .wav；ofDragInfo.files 是 of::filesystem::path 列表）
//==============================================================
static bool tryLoadDroppedAudio(Synth& synth, const std::vector<of::filesystem::path>& files) {
	for (const auto& p : files) {
		std::string path = p.string();
		std::string ext = ofToLower(ofFilePath::getFileExt(path));
		if (ext == "wav") {
			if (synth.loadGrainSource(path)) {
				ofLogNotice() << "granular source <- " << ofFilePath::getFileName(path);
				return true;
			}
		}
	}
	ofLogWarning() << "drag: no usable WAV file";
	return false;
}

void ofApp::dragEvent(ofDragInfo dragInfo){
	if (dragInfo.files.empty()) return;
	tryLoadDroppedAudio(synth, dragInfo.files);
}

void ofApp::dragEventGui(ofDragInfo& dragInfo){
	if (dragInfo.files.empty()) return;
	tryLoadDroppedAudio(synth, dragInfo.files);
}
