#pragma once

#include "ofMain.h"

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

	// 提供 tail 长度归一化（0..1）给 Synth，作为 FM ratio / decay 的调制源
	// 用 base tail length（GUI slider）而不是 effective，避免反馈循环
	float getCurrentTailNormalized() const {
		float t = (float)tailLength / (float)TRAIL_MAX;
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		return t;
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

	// ─── Cluster detection（DBSCAN 风格：每帧独立，看当前粒子邻居密度）───
	// 思路：每帧对每个粒子查半径 R 内的邻居数；够多 → 核心点
	// BFS 把相邻核心点连成 cluster；不维护历史，cluster 移动也能稳定识别
	ofParameter<float> clusterRadius;       // 邻居搜索半径（世界单位）
	ofParameter<int>   clusterMinNeighbors; // 核心点的邻居数下限（DBSCAN minPts）
	ofParameter<int>   clusterMinCount;     // 整个 cluster 至少多少粒子才算成立

	// ─── Trail（光束尾巴）───
	// 长度 = baseTailLen × (0.5 + audioInfluence × tailAudioSensitivity × 1.5)
	// audioInfluence 来自 Synth：event decay / FM ratio / drone cutoff 归一化平均
	ofParameter<int>   tailLength;             // 基础尾巴长度（帧数，0=关）
	ofParameter<float> tailAudioSensitivity;   // 音频影响敏感度（0=纯手动，2=高度耦合）
	ofParameter<float> tailAlpha;              // trail 整体 alpha
	float              audioInfluence = 0.0f;  // 来自外部 setAudioInfluence()

	// helpers
	void      resizeParticles();
	void      respawnFlockParticle(Particle& p);
	void      mergeIntoBigger(Particle& winner, Particle& loser);
	void      loadShaderInline();
	glm::vec3 computeFieldForce(const glm::vec3& pos, float ns) const;
};
