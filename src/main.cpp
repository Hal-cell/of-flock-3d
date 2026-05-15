#include "ofMain.h"
#include "ofApp.h"

//========================================================================
//  双窗口架构
//  ─────────────────────────────────────────────
//  Main window  : 全屏 flock 渲染（ofEasyCam orbit / zoom）
//  GUI window   : 独立 OS 窗口，专跑 ImGui 控制面板
//
//  实现要点：
//    - 两个 ofGLFWWindow，gui window 用 shareContextWith 共享 GL 资源
//    - app 主跑在 main window；gui window 订阅 draw event → ofApp::drawGui()
//    - ofxImGui::Gui 在 drawGui() 第一次触发时初始化（确保 listener attach 到 gui window）
//========================================================================
int main(){
	// ─── Main window（视觉）───
	ofGLFWWindowSettings mainSettings;
	mainSettings.setSize(1280, 800);
	mainSettings.windowMode = OF_WINDOW;          // F 切换 fullscreen
	mainSettings.numSamples = 4;                  // 4x MSAA
	mainSettings.setGLVersion(3, 2);              // GLSL 150
	mainSettings.title = "of-flock-3d";
	auto mainWindow = ofCreateWindow(mainSettings);

	// ─── GUI window（控制面板）───
	// shareContextWith 让两个窗口共享 GL 资源（避免 shader 双份编译）
	ofGLFWWindowSettings guiSettings;
	guiSettings.setSize(440, 900);
	guiSettings.setPosition({40, 80});
	guiSettings.windowMode = OF_WINDOW;
	guiSettings.setGLVersion(3, 2);
	guiSettings.title = "of-flock-3d · controls";
	guiSettings.shareContextWith = mainWindow;
	guiSettings.resizable = true;
	auto guiWindow = ofCreateWindow(guiSettings);

	// ofApp 主跑在 main window；gui window 订阅 draw 事件
	auto app = std::make_shared<ofApp>();
	app->guiWindow = guiWindow;

	ofAddListener(guiWindow->events().draw,
	              app.get(),
	              &ofApp::drawGui);

	// 也让 gui window 接受拖拽（默认 ofApp::dragEvent 只在 main window 触发）
	ofAddListener(guiWindow->events().fileDragEvent,
	              app.get(),
	              &ofApp::dragEventGui);

	ofRunApp(mainWindow, app);
	ofRunMainLoop();
}
