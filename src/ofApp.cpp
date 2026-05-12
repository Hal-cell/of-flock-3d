#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup(){
	ofSetFrameRate(60);
	ofSetVerticalSync(true);
	ofBackground(0);

	// ─── Flock GUI ───
	flock.buildGui(flockParams);
	flockGui.setup(flockParams);
	flockGui.setPosition(20, 20);

	// ─── Synth GUI ───（独立 panel，放在 Flock GUI 右侧）
	synth.buildGui(synthParams);
	synthGui.setup(synthParams);
	synthGui.setPosition(260, 20);

	// 自动加载上次的设置（必须在 flock.setup 之前，让 setup 用上恢复值）
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

	ofSetWindowTitle("of-flock-3d");
}

//--------------------------------------------------------------
void ofApp::exit(){
	soundStream.close();
	flockGui.saveToFile("flock_settings.xml");
	synthGui.saveToFile("synth_settings.xml");
	ofLogNotice() << "saved flock_settings.xml + synth_settings.xml";
}

//--------------------------------------------------------------
void ofApp::update(){
	flock.update();

	// Audio → Visual：把音频活跃度推给 flock 用于 trail 长度调节
	// 三个 synth 参数（event decay / FM ratio / drone cutoff）归一化平均
	flock.setAudioInfluence(synth.getAudioInfluenceForTail());

	// Visual → Audio：tail 长度归一化 → FM idxDecay 正相关调制
	// 用 base tail length（GUI slider 值），避免反馈循环
	synth.setTailInfluence(flock.getCurrentTailNormalized());

	// Field amp 总和 → 风声音量（风声层在 audioOut 内自动衰减/增强）
	synth.setFieldAmpTotal(flock.getFieldAmpTotal());

	// Cluster 检测 → cluster drone voice 池（最多 4 个 drone）
	auto clusters = flock.getClusters(Synth::getMaxDroneVoices());
	synth.updateClusterVoices(clusters, flock.getWorldRadius());
	lastClusterCount = (int)clusters.size();

	// 主线程 → synth：本帧的所有碰撞 → 触发短促 event 音
	for (const auto& ev : flock.getCollisionsThisFrame()) {
		synth.triggerCollision(ev);
	}
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
		flockGui.draw();
		synthGui.draw();
		ofSetColor(255, 200);
		ofDrawBitmapString("clusters: " + ofToString(lastClusterCount) +
		                   "   drones active: " + ofToString(synth.getActiveDroneCount()) +
		                   " / " + ofToString(Synth::getMaxDroneVoices()),
		                   20, ofGetHeight() - 40);
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
void ofApp::audioOut(ofSoundBuffer& buffer){
	// 音频线程回调（每 buffer 一次，约 11ms @ 44.1kHz/512）
	synth.audioOut(buffer);
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
