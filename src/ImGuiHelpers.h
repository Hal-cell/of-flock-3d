#pragma once

#include "ofMain.h"
#include "ofxImGui.h"

/**
 * ImGui ↔ ofParameter 桥接器
 * ─────────────────────────────────────────────
 * ofParameter 保留 → ofxPanel 的 XML save/load 还能用（settings 持久化）。
 * 这些 helper 把 ImGui 控件值同步到 ofParameter，保留双向。
 *
 * 用法：
 *   namespace ig = ImGuiHelp;
 *   ig::slider(myFloatParam);        // SliderFloat
 *   ig::sliderInt(myIntParam);       // SliderInt
 *   ig::check(myBoolParam);          // Checkbox
 *   ig::combo(myIntParam, names);    // Combo (枚举)
 */
namespace ImGuiHelp {

inline bool slider(ofParameter<float>& p, const char* fmt = "%.3f") {
    float v = p.get();
    if (ImGui::SliderFloat(p.getName().c_str(), &v, p.getMin(), p.getMax(), fmt)) {
        p.set(v);
        return true;
    }
    return false;
}

// ImGui 1.77 用 `power` 参数（默认 1.0=线性）实现非线性曲线。
// power > 1 → 低端被拉伸（细调小值方便），常用 3.0 模拟 log。
inline bool sliderLog(ofParameter<float>& p, const char* fmt = "%.4f") {
    float v = p.get();
    if (ImGui::SliderFloat(p.getName().c_str(), &v, p.getMin(), p.getMax(),
                           fmt, 3.0f)) {
        p.set(v);
        return true;
    }
    return false;
}

inline bool sliderInt(ofParameter<int>& p) {
    int v = p.get();
    if (ImGui::SliderInt(p.getName().c_str(), &v, p.getMin(), p.getMax())) {
        p.set(v);
        return true;
    }
    return false;
}

inline bool check(ofParameter<bool>& p) {
    bool v = p.get();
    if (ImGui::Checkbox(p.getName().c_str(), &v)) {
        p.set(v);
        return true;
    }
    return false;
}

inline bool combo(ofParameter<int>& p,
                  const std::vector<const char*>& items) {
    int v = p.get();
    if (ImGui::Combo(p.getName().c_str(), &v,
                     items.data(), (int)items.size())) {
        p.set(v);
        return true;
    }
    return false;
}

// 一个章节标题（折叠 header），返回 true 时内部 widgets 应渲染
inline bool section(const char* label, bool defaultOpen = true) {
    if (defaultOpen) ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
    return ImGui::CollapsingHeader(label);
}

} // namespace ImGuiHelp
