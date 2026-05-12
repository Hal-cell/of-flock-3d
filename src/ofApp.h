#pragma once

#include "ofMain.h"
#include "ofxGui.h"
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

	// 两个独立的参数组 + panel（Flock + Synth）
	ofParameterGroup flockParams;
	ofParameterGroup synthParams;
	ofxPanel flockGui;
	ofxPanel synthGui;

	bool showGui   = true;
	bool recording = false;
	int  frameNum  = 0;
};
