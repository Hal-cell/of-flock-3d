# of-flock-3d

独立的 3D Flock 粒子系统，从 [[of-sketch]] 的 Mode 3 抽出，专注一个事情：**纯 flock 视觉 + 碰撞事件输出**。

## 文件结构

```
of-flock-3d/
├── README.md                       ← 这个文件
├── of-flock-3d.xcodeproj           ← Xcode 项目（projectGenerator 生成）
├── addons.make                     ← 依赖：ofxGui
├── Makefile / config.make / Project.xcconfig / *.plist  ← projectGenerator 生成
├── bin/
│   └── data/                       ← 运行时存档（settings.xml）+ 截图/录制
└── src/
    ├── main.cpp                    ← OF window 启动
    ├── ofApp.h / ofApp.cpp         ← 单一 Flock3D + GUI + 自动存档
    ├── Flock3D.h                   ← Flock 系统接口（含 CollisionEvent 输出）
    └── Flock3D.cpp                 ← 6-field + boid + merge + lifecycle + fade
```

## 打开 / 构建

```bash
open /Users/xuchenghao/Documents/TestVault/10-projects/of-flock-3d/of-flock-3d.xcodeproj
```

Xcode 里 ⌘R 编译运行。

## 重新生成 Xcode 项目（如果改了 addons / 新增了 src 文件）

1. 打开 projectGenerator: `open ~/Documents/OpenFramework/projectGenerator/projectGenerator.app`
2. **Project path**: `/Users/xuchenghao/Documents/TestVault/10-projects/of-flock-3d`（**外层**，不是嵌套的）
3. **OF path**: `/Users/xuchenghao/Documents/OpenFramework`
4. **Addons**: 勾选 `ofxGui`
5. 点 `Generate`（或新版的 `Update`）
6. 重新打开 Xcode 项目

## 操作

| 键 | 作用 |
|---|---|
| `h` | 显示 / 隐藏 GUI |
| `f` | 全屏切换 |
| `s` | 截图（保存到 bin/data/） |
| `r` | 录制 PNG 序列（再按一次停止） |
| `space` | 重置 flock |
| 鼠标拖动 | 旋转视角（ofEasyCam） |
| 滚轮 | 缩放 |

## 关键新增：碰撞事件输出

`Flock3D::getCollisionsThisFrame()` 返回本帧发生的所有 merge 事件：

```cpp
struct CollisionEvent {
    glm::vec3   pos;       // 合并位置
    float       newMass;   // 合并后质量
    float       winnerSize;
    float       loserSize;
    ofFloatColor color;    // 合并后颜色
};
```

在 `ofApp::update()` 里有个注释占位，是音频合成的接入点。

## 未来扩展

- [ ] OSC 输出碰撞事件（发到 Max/MSP / Pd）
- [ ] 内嵌 ofxMaxim 直接 DSP 合成
- [ ] MIDI 输出到 modular synth
