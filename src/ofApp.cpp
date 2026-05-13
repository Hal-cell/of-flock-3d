#include "ofApp.h"

//==============================================================
//  setup — 在 main window 的上下文里调用
//==============================================================
void ofApp::setup(){
	ofSetFrameRate(60);
	ofSetVerticalSync(true);
	ofBackground(0);

	// ─── Flock + Synth 参数 ───
	flock.buildGui(flockParams);
	flockGui.setup(flockParams);

	synth.buildGui(synthParams);
	synthGui.setup(synthParams);

	// 自动加载上次的设置（在 flock.setup 之前，让 setup 用上恢复值）
	if (ofFile::doesFileExist(ofToDataPath("flock_settings.xml"))) {
		flockGui.loadFromFile("flock_settings.xml");
		ofLogNotice() << "loaded flock_settings.xml";
	}
	if (ofFile::doesFileExist(ofToDataPath("synth_settings.xml"))) {
		synthGui.loadFromFile("synth_settings.xml");
		ofLogNotice() << "loaded synth_settings.xml";
	}

	flock.setup(ofGetWidth(), ofGetHeight());

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
	ofLogNotice() << "saved flock_settings.xml + synth_settings.xml";
}

//==============================================================
//  update — 主线程（main window 上下文）
//==============================================================
void ofApp::update(){
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

		if (ImGui::BeginTabBar("MainTabs")) {
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
