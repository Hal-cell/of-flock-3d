# of-flock-3d

3D Flock 粒子艺术装置 — 在 openFrameworks (C++) 里实时计算粒子物理，并基于粒子聚集行为生成音频。

**A 3D particle flock with audio synthesis driven by particle dynamics.**

## 视听内容 / What it does

**视觉**
- 20K 粒子在 3D 空间运动，受 6 种力场（noise / vortex / spiral / curl / attractor / repeller）影响
- 粒子互相 boid 行为（separation / cohesion）+ 太近时 merge（大吞小）
- merge 触发**白色闪烁**（视觉同步声学事件）
- 粒子着色为 3D 小球（Lambert + spec + 可调 halo），不刺眼
- 粒子拖**光束尾巴**（长度可调，与音频参数耦合）

**音频**
- **Cluster Drone**：检测粒子聚集，每个 cluster 触发一个 polyphonic 持续音（saw + SVF lowpass，五声音阶 chord）
- **Event Sound**：每次 merge 触发一个短促 FM 钟声（pitch 由质量量化到 scale，偶有 accent 高八度）
- **Wind Layer**：持续滤波白噪声风声，cutoff 受 vortex/spiral/curl amp 总和驱动
- **Hall Reverb**：4-tap Hadamard FDN + HF damping + pre-delay，大空间感

**视听耦合**
- audio cutoff → trail 长度（声音越亮，尾巴越长）
- tail 长度 → FM idxDecay（视觉尾巴越长，音色越温暖）
- cluster 数 ↔ drone voice 数（最多 4 个 polyphonic voice）

## Prerequisites

- macOS（其他平台未测试；理论上 Linux/Windows 经 projectGenerator 重新生成项目应可工作）
- [openFrameworks 0.12.x](https://openframeworks.cc/download/)
- Xcode 14+

## Setup

### 1. Clone

```bash
git clone https://github.com/Hal-cell/of-flock-3d.git
```

### 2. Place under openFrameworks 目录

把 `of-flock-3d/` 文件夹**放在 OF 的 `apps/myApps/`（或其他位置）**下，例如：

```
openFrameworks/
├── addons/
├── examples/
└── apps/
    └── myApps/
        └── of-flock-3d/    ← clone 到这里
```

### 3. Re-generate Xcode project

`.xcodeproj` 引用了相对路径到 OF 库。如果不在标准位置，需要用 projectGenerator 重新生成：

1. 打开 `openFrameworks/projectGenerator/projectGenerator.app`
2. **Project path**: 选 `of-flock-3d` 文件夹
3. **OF path**: 你的 openFrameworks 根目录
4. **Addons**: 勾选 `ofxGui`
5. 点 `Update`（或旧版的 `Generate`）

### 4. Open + build

```bash
open of-flock-3d.xcodeproj
```

Xcode 里选 Release scheme，按 ⌘R 运行。

> **第一次编译 30-60 秒**（要编 OF 库）。如果报 `#version 150 not supported` → 检查 `main.cpp` 里 `settings.setGLVersion(3, 2)` 没问题。

## Controls

| 键 | 作用 |
|---|---|
| `h` | 隐藏 / 显示 GUI |
| `f` | 全屏切换 |
| `s` | 截图（保存到 `bin/data/`）|
| `r` | 录制 PNG 序列（再按一次停止）|
| `space` | 重置 flock |
| 鼠标拖动 | 旋转视角（ofEasyCam） |
| 滚轮 | 缩放 |

## Rollback tags

项目用 git tags 记录所有"已知好"的版本，方便随时回滚：

```
rp-00-visual-only           最初视觉基线
rp-01-synth-v1              首个音频版本
rp-09-saw-drone-bfs         cluster drone 加入
rp-11-fix-envelope-cliff    修 ADSR cliff bug
rp-12-hall-reverb           hall reverb
rp-13-particle-trails       粒子拖尾
rp-21-wind-layer            风声层
rp-23-cluster-simple        简化 cluster 检测
rp-27-shaded-spheres        着色 3D 小球 shader
... (完整列表见 git tag --list)
```

切换到任意版本：

```bash
git checkout rp-12-hall-reverb     # 临时切换
git reset --hard rp-12-hall-reverb # 永久回滚（破坏未提交修改）
```

详细演进见 [rollback-points.md](rollback-points.md)。

## 文件结构

```
of-flock-3d/
├── README.md                 ← 这个文件
├── rollback-points.md        ← 每个 rp tag 的详细说明
├── of-flock-3d.xcodeproj/    ← Xcode 项目（用 projectGenerator 重新生成）
├── addons.make               ← 依赖：ofxGui
├── Makefile / config.make / Project.xcconfig
├── bin/data/                 ← settings.xml + 截图 + 录制输出
└── src/
    ├── main.cpp              ← OF window 入口 (GL 3.2)
    ├── ofApp.h/cpp           ← Flock3D + Synth + GUI + 自动存档
    ├── Flock3D.h/cpp         ← 粒子系统、力场、boid、merge、trail、cluster
    └── Synth.h/cpp           ← drone + FM events + wind + reverb
```

## Author

Hal Xu (xch980226@gmail.com) — audiovisual artist / music tech PhD @ Edinburgh

Built collaboratively with Claude. Issues + PRs welcome.

## License

MIT
