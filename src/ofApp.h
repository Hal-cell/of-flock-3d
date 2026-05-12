#pragma once

#include "ofMain.h"
#include "ofxGui.h"
#include "Flock3D.h"

class ofApp : public ofBaseApp {
public:
	void setup() override;
	void update() override;
	void draw() override;
	void exit() override;
	void keyPressed(int key) override;
	void windowResized(int w, int h) override;

private:
	Flock3D flock;
	ofParameterGroup paramGroup;
	ofxPanel gui;

	bool showGui   = true;
	bool recording = false;
	int  frameNum  = 0;
};
