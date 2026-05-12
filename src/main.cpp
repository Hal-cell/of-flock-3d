#include "ofMain.h"
#include "ofApp.h"

//========================================================================
int main( ){

	ofGLFWWindowSettings settings;
	settings.setSize(1280, 800);
	settings.windowMode = OF_WINDOW;          // F 切换 fullscreen
	settings.numSamples = 4;                  // 4x MSAA
	settings.setGLVersion(3, 2);              // 启用 GLSL 150
	settings.title = "of-flock-3d";

	auto window = ofCreateWindow(settings);

	ofRunApp(window, std::make_shared<ofApp>());
	ofRunMainLoop();

}
