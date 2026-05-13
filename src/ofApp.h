#pragma once

#include "ofMain.h"
#include "ofxGui.h"        // ofxPanel 保留（仅用于 XML save/load 持久化，不渲染）
#include "ofxImGui.h"      // 新 GUI 渲染
#include "Flock3D.h"
#include "Synth.h"

class ofApp : public ofBaseApp {
public:
	void setup() override;
	void update() override;
	void draw() override;
	void exit() override;
	void keyPressed(int key) override;
	void windowResized(int w, int h) override;

	// 音频回调（ofSoundStream 自动每 buffer 调用一次）
	void audioOut(ofSoundBuffer& buffer) override;

private:
	Flock3D flock;
	Synth   synth;

	ofSoundStream soundStream;

	// 参数组保留 → ofxPanel 处理 XML save/load（不渲染界面，只做持久化）
	ofParameterGroup flockParams;
	ofParameterGroup synthParams;
	ofxPanel flockGui;
	ofxPanel synthGui;

	// 新 GUI（ofxImGui）
	ofxImGui::Gui imgui;
	void applyImGuiTheme();

	bool showGui   = true;
	bool recording = false;
	int  frameNum  = 0;
	int  lastClusterCount = 0;   // HUD 显示
};
