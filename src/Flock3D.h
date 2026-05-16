#pragma once

#include "ofMain.h"
#include "EnergyStage.h"
#include <unordered_map>

/**
 * Flock3D — 纯 3D Flock 粒子系统（独立版）
 * ──────────────────────────────────────────────
 * 粒子在多种宏观流场叠加 + boid 力（分离/凝聚）共同作用下流动。
 * 太近的粒子会 merge（大吞小），并通过 spawn 速率 + min alive 补回。
 * 所有出生/死亡都是平滑 fade in/out。
 *
 * 关键设计：
 *   - 6 种 field（noise/vortex/spiral/curl/attractor/repeller）可独立 amp 叠加
 *   - boid sep / coh 控制局部团聚行为（K=10 随机邻居采样）
 *   - 使用 ofEasyCam → 鼠标拖动旋转 / 滚轮缩放
 */
class Flock3D {

public:
	void setup(int w, int h);
	void update();
	void draw();
	void reset();
	void buildGui(ofParameterGroup& group);
	void drawImGui();   // 用 ImGui 渲染 GUI（替代 ofxPanel）
	void keyPressed(int key);
	std::string getName() const { return "Flock 3D"; }

	// 给外部（如 ofApp / 音频模块）订阅碰撞事件用
	struct CollisionEvent {
		glm::vec3   pos;       // 合并发生的位置（winner 当前位置）
		float       newMass;   // 合并后的质量
		float       winnerSize;
		float       loserSize;
		ofFloatColor color;    // 合并后的混色
		bool        isAccent;  // 偶尔 accent merge（视觉更大闪烁、音频高八度）
	};
	const std::vector<CollisionEvent>& getCollisionsThisFrame() const { return collisionsThisFrame; }

	// ─── 给音频合成器用的统计量 ───
	struct Stats {
		float aliveRatio   = 0.0f;   // alive / total
		float meanSpeed    = 0.0f;   // 平均速度模长
		float spread       = 0.0f;   // 平均到原点的距离（"扩散度"）
		glm::vec3 meanPos  = glm::vec3(0);
		int   collisionCount = 0;    // 本帧碰撞次数
		// 6 个 field 的归一化权重（sum 接近 1，全 0 也合法）
		float fieldNoise = 0, fieldVortex = 0, fieldSpiral = 0;
		float fieldCurl  = 0, fieldAttractor = 0, fieldRepeller = 0;
	};
	Stats getStats() const;

	// 团簇代表（top-K by mass，质量大的粒子 = 多次 merge 的累积）
	struct ClusterCandidate {
		glm::vec3    pos;
		glm::vec3    velocity;
		float        mass;
		ofFloatColor color;
	};
	std::vector<ClusterCandidate> getTopByMass(int K) const;

	// 空间团簇检测（用 3D grid hash，找密度高的 cell）
	struct Cluster {
		glm::vec3    centroid;
		glm::vec3    velocity;
		float        totalMass;
		int          particleCount;
		ofFloatColor avgColor;
	};
	std::vector<Cluster> getClusters(int maxK = 8) const;

	float getWorldRadius() const { return worldRadius.get(); }

	// 由 ofApp 每帧调用：传递音频参数归一化平均（0..1）
	// 影响 trail 长度（正相关：audio 越"亮"/越"长"，尾巴越长）
	void setAudioInfluence(float v) { audioInfluence = v; }

	// 由 ofApp 每帧调用：morphology conductor 输出值（0..1，baseline=0.5）
	// 见 论文 Spectromorphological Synchresis 节
	void setConductorValue(float v) { conductorValue = v; }

	// 实测视觉能量 [0..1]：density × meanSpeed × meanBrightness 复合
	// 给 Synchresis 自感知用（"系统看见自己"）
	float getVisualEnergyMeasured() const;

	// 提供 tail 长度归一化（0..1）给 Synth — 调制 FM idxDecay
	// 用 base tail length（GUI slider 值）— 避免和 audio→tail 形成反馈循环
	float getCurrentTailNormalized() const {
		float t = (float)tailLength / (float)TRAIL_MAX;
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		return t;
	}

	// "流动型" field amp 总和归一化（0..1），给 Synth 风声 cutoff 用
	// 只用 vortex + spiral + curl（"风感"的 field，不算 noise/attractor/repeller）
	// 单个 amp 上限 200，3 个全开 = 600；用 / 400 作归一化分母
	// → 一个 field=200 → 0.5；两个 field=200+200 → 1.0；三个全满 → clamp 1.0
	float getFieldAmpTotal() const {
		float total = vortexAmp + spiralAmp + curlAmp;
		float norm = total / 400.0f;
		if (norm < 0.0f) norm = 0.0f;
		if (norm > 1.0f) norm = 1.0f;
		return norm;
	}

private:
	// 每个粒子的 trail 长度上限（编译时常量）
	// 内存：20K particles × 24 vec3 × 12 bytes ≈ 5.5 MB（可接受）
	static constexpr int TRAIL_MAX = 24;

	struct Particle {
		glm::vec3    pos;
		glm::vec3    velocity;
		ofFloatColor color;
		float        size;
		float        mass;
		bool         alive;
		int          lifetime;
		int          maxLifetime;
		int          fadeInTimer;
		int          fadeOutTimer;
		int          flashTimer;
		float        flashScale;

		// Trail：环形 buffer 存最近 TRAIL_MAX 帧的位置
		// 因为粒子运动直接由 field velocity 决定，trail 自然展示 velocity 轨迹
		glm::vec3    trail[TRAIL_MAX];
		int          trailWriteIdx = 0;   // 下次写入位置
		int          trailCount    = 0;   // 已积累的有效帧数（caps at TRAIL_MAX）
	};

	std::vector<Particle> particles;
	std::vector<CollisionEvent> collisionsThisFrame;   // 每帧清空，update 内累积
	ofEasyCam cam;
	ofShader  particleShader;

	// Fast XorShift32 PRNG（替代 std::rand — 200K calls/frame × ~30ns 节省 ~6ms）
	// 无 libc lock，每 call ~1ns。每个 Flock3D 实例有独立 state
	mutable uint32_t prngState = 0x12345678u;
	inline uint32_t fastRand() const {
		uint32_t x = prngState;
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		prngState = x;
		return x;
	}
	// 等价于 std::rand() % n，但避免取模 div（用 64-bit mul + shift）
	inline int fastRandBelow(int n) const {
		return (int)(((uint64_t)fastRand() * (uint64_t)n) >> 32);
	}

	// 复用的 mesh 对象（避免每帧重新分配；vector capacity 保留）
	ofMesh particleMesh;
	ofMesh trailMesh;

	int   width  = 0, height = 0;
	float noiseTimeOffset = 0.0f;

	// ─── 通用粒子参数 ───
	ofParameter<int>   particleCount;
	ofParameter<float> worldRadius;
	ofParameter<float> particleAlpha;
	ofParameter<bool>  autoRotate;
	ofParameter<bool>  showAxis;
	ofParameter<float> hueBase;
	ofParameter<float> hueRange;
	ofParameter<float> brightness;

	// ─── 宏观流场 ───
	ofParameter<float> noiseAmplitude;
	ofParameter<float> noiseScale;
	ofParameter<float> noiseSpeed;
	ofParameter<float> vortexAmp;
	ofParameter<float> spiralAmp;
	ofParameter<float> curlAmp;
	ofParameter<float> attractorAmp;
	ofParameter<float> repellerAmp;

	// ─── Boid 力 ───
	ofParameter<float> flockSeparation;
	ofParameter<float> flockCohesion;
	ofParameter<float> flockCohesionSpeed;
	ofParameter<float> flockNeighborRadius;
	ofParameter<float> mergeDistance;
	ofParameter<float> flockSpawnRate;
	ofParameter<float> flockMinAlive;
	ofParameter<float> flockDamping;
	ofParameter<float> particleSizeMin;
	ofParameter<float> particleSizeMax;

	// ─── Lifecycle ───
	ofParameter<bool>  lifecycleMode;
	ofParameter<int>   lifespanMin;
	ofParameter<int>   lifespanMax;

	// ─── Fade ───
	ofParameter<int>   fadeInFrames;
	ofParameter<int>   fadeOutFrames;

	// ─── Flash（merge 时的高亮闪烁）───
	ofParameter<int>   flashFrames;     // 闪烁持续帧数
	ofParameter<float> flashIntensity;  // 0..2，闪烁强度（颜色+size 加成）

	// ─── Accent（偶尔大闪烁 + 音频高八度）───
	ofParameter<float> accentChance;    // 0..1，每次 merge 命中的概率
	ofParameter<float> accentSizeMul;   // accent flash 的 size 倍率

	// ─── Cluster detection（两个直观参数）───
	// 把粒子实际占用的 bbox 切成 gridRes³ 个 cell，每 cell 内闪烁粒子 ≥ minFlash → cluster
	ofParameter<int>   clusterGridRes;     // 3..10：每边 cell 数
	ofParameter<int>   clusterMinFlash;    // 1..100：cell 内最少几个闪烁粒子算 cluster
	ofParameter<bool>  showClusterGrid;    // debug：显示 grid + 高亮 dense cell

	// 缓存 grid 信息（getClusters 写入，draw 读取做可视化）
	mutable glm::vec3       lastBboxMin{0};
	mutable glm::vec3       lastBboxMax{0};
	mutable int             lastGridRes = 5;
	mutable bool            lastBboxValid = false;
	mutable std::vector<int> lastCellCounts;

	// ─── Trail（光束尾巴）───
	// 长度 = baseTailLen × (0.5 + audioInfluence × tailAudioSensitivity × 1.5)
	// audioInfluence 来自 Synth：event decay / FM ratio / drone cutoff 归一化平均
	ofParameter<int>   tailLength;             // 基础尾巴长度（帧数，0=关）
	ofParameter<float> tailAudioSensitivity;   // 音频影响敏感度（0=纯手动，2=高度耦合）
	ofParameter<float> tailAlpha;              // trail 整体 alpha
	float              audioInfluence = 0.0f;  // 来自外部 setAudioInfluence()

	// ─── Mycelium（菌丝网络）：4 种 link 模式 + FBO bloom ───
	// LinkMode:
	//   DISTANCE  — 距离阈值内取最近 N 条（每帧重建，无记忆）
	//   KNN       — 严格 K 近邻（不限距离，linkDist 只作搜索上限）
	//   LIFETIME  — 距离阈值 + 持久化：link 一旦生成会保留 N 帧才衰减消失
	//                可看到"丝慢慢长出来 + 慢慢消散"的菌丝呼吸感
	//   GABRIEL   — Gabriel graph（伪 Delaunay 子集）：两点间球内无第三点 → 连
	//                给出空间均匀的有机 mesh
	// FBO bloom 作为 wrapper：任何 mode 渲染到 FBO + 帧间 fade 累积 → 生长扩散感
	enum LinkMode {
		LINK_DISTANCE = 0,
		LINK_KNN,
		LINK_LIFETIME,
		LINK_GABRIEL,
	};

	ofParameterGroup   myceliumGroup;
	ofParameter<bool>  myceliumEnabled;
	ofParameter<int>   myceliumLinkMode;     // 0..3 see LinkMode
	ofParameter<float> myceliumLinkDist;     // 距离阈值 / KNN 搜索半径 → cell size
	ofParameter<int>   myceliumMaxLinks;     // DISTANCE / GABRIEL: 每 node 最多几条
	ofParameter<int>   myceliumKnnK;         // KNN 模式 K 值
	ofParameter<int>   myceliumLifetime;     // LIFETIME 模式：link 寿命（帧）
	ofParameter<int>   myceliumNodeStride;   // 每 N 个粒子取 1 个做 node（密度控制）
	ofParameter<float> myceliumAlpha;        // 线整体 alpha
	ofParameter<float> myceliumFadeNear;     // 远端 fade（DISTANCE/GABRIEL）
	ofParameter<float> myceliumLineWidth;    // GL line width

	// FBO bloom（生长扩散）
	ofParameter<bool>  myceliumFboBloom;
	ofParameter<float> myceliumFboFade;      // 0..1，每帧保留比例（0.95 = 5% fade）

	ofMesh             myceliumMesh;         // 复用顶点 buffer
	// 持久化 link：key = (min(a,b)<<32)|max(a,b)，val = ageInFrames
	std::unordered_map<uint64_t, int> persistentLinks;
	ofFbo              myceliumFbo;
	int                myceliumFboW = 0, myceliumFboH = 0;

	// ─── Morphology Conductor 输入 ───
	// conductorValue: 0..1, baseline=0.5（来自顶层 MorphologyConductor）
	// conductorAmount: 0..1（用户调节 conductor 对 Flock 的影响强度，0=不受影响）
	// 论文：Spectromorphological Synchresis — ascent/descent/osc 形态学曲线驱动
	//       视觉的 "energy-motion trajectory"
	ofParameter<float> conductorAmount;
	float              conductorValue = 0.5f;
	// 调制目标（视觉侧）：
	//   - field amp scalar     → 6 个 field amp 同时乘
	//   - particle size scalar → 粒子 size 缩放
	//   - brightness scalar    → 整体 brightness 缩放
	// 在 update() / draw() 起始计算 conductorScalar，传给 helpers

	// ─── Material（着色 3D 小球 + 可调 halo；flash 时切换到 soft-disc）───
	// 平时：sphere（Lambert + spec）+ halo（可调 glow）
	// Flash 时：自动变 rp-24 风格的柔软发光软盘，突出碰撞瞬间
	ofParameter<float> matBrightness;   // 整体亮度 0..1
	ofParameter<float> matSpecular;     // 镜面高光强度 0..1
	ofParameter<float> matAmbient;      // 暗面填充（避免黑死）0..0.5
	ofParameter<float> matGlow;         // sphere 外的 halo 发光 0..1.5

	// helpers
	void      resizeParticles();
	void      respawnFlockParticle(Particle& p);
	void      mergeIntoBigger(Particle& winner, Particle& loser);
	void      loadShaderInline();
	glm::vec3 computeFieldForce(const glm::vec3& pos, float ns) const;
	// Mycelium 渲染管线
	void      drawMycelium();           // 路由：FBO mode 或直接 mode
	void      buildMyceliumMesh();      // 按 linkMode 填充 myceliumMesh
	void      renderMyceliumMesh();     // glLineWidth + draw + 还原
	void      drawMyceliumViaFbo();     // FBO bloom 路径（cam 内部 begin）
	void      ensureMyceliumFbo();      // 懒分配 / 窗口变化时重建
};
