#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup(){
	ofSetFrameRate(60);
	ofSetVerticalSync(true);
	ofBackground(0);

	// Flock3D GUI
	flock.buildGui(paramGroup);
	gui.setup(paramGroup);
	gui.setPosition(20, 20);

	// ─── 自动加载上次的设置（在 setup 之前 → 让 setup 用上恢复的参数）───
	std::string fname = "settings.xml";
	if (ofFile::doesFileExist(ofToDataPath(fname))) {
		gui.loadFromFile(fname);
		ofLogNotice() << "loaded " << fname;
	}

	flock.setup(ofGetWidth(), ofGetHeight());

	ofSetWindowTitle("of-flock-3d");
}

//--------------------------------------------------------------
void ofApp::exit(){
	// 自动保存当前 GUI 参数
	std::string fname = "settings.xml";
	gui.saveToFile(fname);
	ofLogNotice() << "saved " << fname;
}

//--------------------------------------------------------------
void ofApp::update(){
	flock.update();

	// ─── 这里是音频集成点：本帧的碰撞事件 ───
	// const auto& collisions = flock.getCollisionsThisFrame();
	// for (auto& ev : collisions) {
	//     // → 触发音频合成：ev.pos / ev.newMass / ev.color
	// }
}

//--------------------------------------------------------------
void ofApp::draw(){
	flock.draw();

	if (recording) {
		ofImage img;
		img.grabScreen(0, 0, ofGetWidth(), ofGetHeight());
		std::string fname = "recording_" + ofToString(frameNum, 5, '0') + ".png";
		img.save(fname);
		frameNum++;
	}

	if (showGui) {
		gui.draw();
		ofSetColor(255, 200);
		ofDrawBitmapString("h: GUI | f: fullscreen | s: snap | r: rec | space: reset",
		                   20, ofGetHeight() - 20);
		if (recording) {
			ofSetColor(255, 0, 0);
			ofDrawCircle(ofGetWidth() - 30, 30, 8);
			ofSetColor(255);
			ofDrawBitmapString("REC " + ofToString(frameNum), ofGetWidth() - 110, 35);
		}
	}
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){
	switch (key) {
		case 'h': case 'H': showGui = !showGui; break;
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

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h){
	flock.setup(w, h);
}
