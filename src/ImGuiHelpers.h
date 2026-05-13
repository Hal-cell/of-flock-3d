#pragma once

#include "ofMain.h"
#include "ofxImGui.h"

/**
 * ImGui ↔ ofParameter 桥接器
 * ─────────────────────────────────────────────
 * ofParameter 保留 → ofxPanel 的 XML save/load 还能用（settings 持久化）。
 * 这些 helper 把 ImGui 控件值同步到 ofParameter，保留双向。
 *
 * ID 命名空间：
 *   ImGui 默认用 widget label 计算 widget ID。如果两个 widget 同 label
 *   （如 windVol 和 clusterDroneVol 都叫 "vol"），ID 碰撞 → 拖一个会
 *   带动另一个。修法：用 ofParameter 实例的内存地址做 ID 命名空间，
 *   每个 ofParameter 地址唯一 → ID 唯一。display label 保持原状。
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
    ImGui::PushID((void*)&p);
    bool changed = ImGui::SliderFloat(p.getName().c_str(), &v,
                                      p.getMin(), p.getMax(), fmt);
    ImGui::PopID();
    if (changed) p.set(v);
    return changed;
}

// ImGui 1.77 用 `power` 参数（默认 1.0=线性）实现非线性曲线。
// power > 1 → 低端被拉伸（细调小值方便），常用 3.0 模拟 log。
inline bool sliderLog(ofParameter<float>& p, const char* fmt = "%.4f") {
    float v = p.get();
    ImGui::PushID((void*)&p);
    bool changed = ImGui::SliderFloat(p.getName().c_str(), &v,
                                      p.getMin(), p.getMax(), fmt, 3.0f);
    ImGui::PopID();
    if (changed) p.set(v);
    return changed;
}

inline bool sliderInt(ofParameter<int>& p) {
    int v = p.get();
    ImGui::PushID((void*)&p);
    bool changed = ImGui::SliderInt(p.getName().c_str(), &v,
                                    p.getMin(), p.getMax());
    ImGui::PopID();
    if (changed) p.set(v);
    return changed;
}

inline bool check(ofParameter<bool>& p) {
    bool v = p.get();
    ImGui::PushID((void*)&p);
    bool changed = ImGui::Checkbox(p.getName().c_str(), &v);
    ImGui::PopID();
    if (changed) p.set(v);
    return changed;
}

inline bool combo(ofParameter<int>& p,
                  const std::vector<const char*>& items) {
    int v = p.get();
    ImGui::PushID((void*)&p);
    bool changed = ImGui::Combo(p.getName().c_str(), &v,
                                items.data(), (int)items.size());
    ImGui::PopID();
    if (changed) p.set(v);
    return changed;
}

// 一个章节标题（折叠 header），返回 true 时内部 widgets 应渲染
inline bool section(const char* label, bool defaultOpen = true) {
    if (defaultOpen) ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
    return ImGui::CollapsingHeader(label);
}

} // namespace ImGuiHelp
