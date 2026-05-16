#pragma once

#include "ofMain.h"
#include "ofxGui.h"        // ofxPanel 保留（仅用于 XML save/load 持久化，不渲染）
#include "ofxImGui.h"      // GUI 渲染（运行在 guiWindow）
#include "Flock3D.h"
#include "Synth.h"
#include "MorphologyConductor.h"
#include "Synchresis.h"
#include "Score.h"

class ofApp : public ofBaseApp {
public:
	void setup() override;
	void update() override;
	void draw() override;
	void exit() override;
	void keyPressed(int key) override;
	void windowResized(int w, int h) override;
	void dragEvent(ofDragInfo dragInfo) override;   // 主窗口拖拽：wav/aif/flac → 替换 granular 源
	void dragEventGui(ofDragInfo& dragInfo);        // gui 窗口拖拽（main.cpp 注册到 fileDragEvent）

	// 音频回调（ofSoundStream 自动每 buffer 调用一次）
	void audioOut(ofSoundBuffer& buffer) override;

	// GUI window 的 draw event 回调（main.cpp 里订阅）
	void drawGui(ofEventArgs& args);

	// main.cpp 注入：第二个 OS 窗口的指针
	std::shared_ptr<ofAppBaseWindow> guiWindow;

private:
	Flock3D flock;
	Synth   synth;
	MorphologyConductor conductor;          // 主 conductor（counterpoint OFF=共享; ON=audio 用）
	MorphologyConductor conductorVisual;    // 第二 conductor（counterpoint ON 时驱动 visual）
	Synchresis          synchresis;
	ScorePlayer         scorePlayer;

	ofSoundStream soundStream;

	// 参数组保留 → ofxPanel 处理 XML save/load（不渲染界面，只做持久化）
	ofParameterGroup flockParams;
	ofParameterGroup synthParams;
	ofParameterGroup morphologyParams;
	ofParameterGroup morphologyVisualParams;   // 第二 conductor 的参数组
	ofParameterGroup synchresisParams;
	ofParameterGroup scoreParams;
	ofxPanel flockGui;
	ofxPanel synthGui;
	ofxPanel morphologyGui;
	ofxPanel morphologyVisualGui;
	ofxPanel synchresisGui;
	ofxPanel scoreGui;

	// ImGui — 在 drawGui() 第一次触发时初始化（保证 listener attach 到 gui window）
	ofxImGui::Gui imgui;
	bool imguiInitialized = false;
	void applyImGuiTheme();

	bool recording = false;
	int  frameNum  = 0;
	int  lastClusterCount = 0;
};
