#include "Flock3D.h"
#include "ImGuiHelpers.h"

//==============================================================
//  GUI
//==============================================================
void Flock3D::buildGui(ofParameterGroup& group) {
	group.setName(getName());

	// ─── 通用粒子 ───
	group.add(particleCount.set("particles",        20000,  500, 80000));
	group.add(worldRadius.set("world radius",       250.0f, 50.0f, 600.0f));
	group.add(particleAlpha.set("alpha",            0.6f,   0.05f,  1.0f));
	group.add(autoRotate.set("autoRotate",          true));
	group.add(showAxis.set("show axis",             false));
	group.add(hueBase.set("hueBase",                0.55f,  0.0f,  1.0f));
	group.add(hueRange.set("hueRange",              0.2f,   0.0f,  1.0f));
	group.add(brightness.set("brightness",          1.0f,   0.0f,  1.5f));

	// ─── 宏观流场 ───
	group.add(noiseAmplitude.set("noise amp",       25.0f,  0.0f,   200.0f));
	group.add(noiseScale.set("noise scale",         0.005f, 0.0001f, 0.05f));
	group.add(noiseSpeed.set("noise speed",         0.05f,  0.0f,   0.5f));
	group.add(vortexAmp.set("vortex amp",            0.0f,  0.0f,   200.0f));
	group.add(spiralAmp.set("spiral amp",            0.0f,  0.0f,   200.0f));
	group.add(curlAmp.set("curl amp",                0.0f,  0.0f,   200.0f));
	group.add(attractorAmp.set("attractor amp",      0.0f,  0.0f,   200.0f));
	group.add(repellerAmp.set("repeller amp",        0.0f,  0.0f,   200.0f));

	// ─── Boid 力 ───
	group.add(flockSeparation.set("flock sep",       0.8f,  0.0f,   5.0f));
	group.add(flockCohesion.set("flock coh",         1.0f,  0.0f,   20.0f));
	group.add(flockCohesionSpeed.set("coh speed",    0.01f, 0.0f,   0.5f));
	group.add(flockNeighborRadius.set("flock nbrR", 80.0f,  10.0f,  400.0f));
	group.add(mergeDistance.set("merge dist",        6.0f,  1.0f,   40.0f));
	group.add(flockSpawnRate.set("spawn rate",      25.0f,  0.0f,   40000.0f));
	group.add(flockMinAlive.set("min alive",         0.5f,  0.0f,   1.0f));
	group.add(flockDamping.set("damping",            0.92f, 0.5f,   1.0f));
	group.add(particleSizeMin.set("sizeMin",         3.0f,  1.0f,   20.0f));
	group.add(particleSizeMax.set("sizeMax",        10.0f,  2.0f,   60.0f));

	// ─── Lifecycle ───
	group.add(lifecycleMode.set("lifecycle MODE",   false));
	group.add(lifespanMin.set("life min frames",   180,    30,   2000));
	group.add(lifespanMax.set("life max frames",   600,    60,   3000));

	// ─── Fade ───
	group.add(fadeInFrames.set("fadeIn frames",    20,     0,   120));
	group.add(fadeOutFrames.set("fadeOut frames",  30,     0,   120));

	// ─── Flash（merge 时高亮闪烁）───
	group.add(flashFrames.set("flash frames",      12,     0,   60));     // ~200ms @ 60fps
	group.add(flashIntensity.set("flash intensity", 1.0f, 0.0f, 2.0f));

	// ─── Accent（偶尔重音：视觉大闪烁 + 音频高八度）───
	group.add(accentChance.set("accent chance",  0.1f, 0.0f, 1.0f));      // 10% 默认
	group.add(accentSizeMul.set("accent size",   2.5f, 1.0f, 5.0f));      // 普通的 2.5 倍 size

	// ─── Cluster detection（两个直观参数）───
	// gridRes：世界切成 gridRes³ 个 cell（5 = 125 cells，cell ~100u 在默认 world）
	// minFlash：cell 内"正在闪烁"（刚 merge 完）的粒子数 ≥ 此值 → cluster
	// 只数闪烁粒子 → 直接锁定 merge 活跃区
	group.add(clusterGridRes.set("cluster grid",      5,    3,   10));
	group.add(clusterMinFlash.set("cluster minFlash", 5,    1,   100));
	group.add(showClusterGrid.set("show cluster grid", false));   // debug

	// ─── Trail（光束尾巴）───
	// length = baseLen × (0.5 + audio_influence × sensitivity × 1.5)
	// audio_influence 综合 event decay / FM ratio / cluster cutoff
	group.add(tailLength.set("tail length",          8,    0, int(TRAIL_MAX)));
	group.add(tailAudioSensitivity.set("tail audio sens", 1.0f, 0.0f, 2.0f));
	group.add(tailAlpha.set("tail alpha",            0.45f, 0.0f, 1.0f));

	// ─── Material ───
	// 默认 sphere 着色 + 可调 halo；flash 时自动切到 rp-24 风格软盘（碰撞瞬间突出）
	group.add(matBrightness.set("brightness",   0.55f, 0.0f, 1.0f));
	group.add(matSpecular.set("specular",       0.35f, 0.0f, 1.0f));
	group.add(matAmbient.set("ambient",         0.25f, 0.0f, 0.5f));
	group.add(matGlow.set("glow",               0.3f,  0.0f, 1.5f));

	// ─── Morphology Conductor 影响强度 ───
	// 论文 Spectromorphological Synchresis：conductor 输出曲线对 visual field amp 的影响幅度
	// 0 = 不受影响（向后兼容 rp-34）；1 = 满影响（conductor 0→1 让 field 0→2x）
	group.add(conductorAmount.set("conductor amount", 0.0f, 0.0f, 1.0f));
}

//==============================================================
//  ImGui rendering — 把 ofParameter 渲染成现代 GUI
//==============================================================
void Flock3D::drawImGui() {
	namespace ig = ImGuiHelp;

	if (ig::section("General")) {
		ig::sliderInt(particleCount);
		ig::slider(worldRadius, "%.0f");
		ig::slider(particleAlpha);
		ig::check(autoRotate);
		ig::check(showAxis);
		ig::slider(hueBase);
		ig::slider(hueRange);
		ig::slider(brightness);
	}

	if (ig::section("Force Fields")) {
		ImGui::TextDisabled("macro fields");
		ig::slider(noiseAmplitude, "%.1f");
		ig::sliderLog(noiseScale);
		ig::slider(noiseSpeed);
		ImGui::Separator();
		ImGui::TextDisabled("flow fields (drive wind)");
		ig::slider(vortexAmp, "%.1f");
		ig::slider(spiralAmp, "%.1f");
		ig::slider(curlAmp, "%.1f");
		ImGui::Separator();
		ImGui::TextDisabled("radial");
		ig::slider(attractorAmp, "%.1f");
		ig::slider(repellerAmp, "%.1f");
	}

	if (ig::section("Boid / Merge")) {
		ig::slider(flockSeparation);
		ig::slider(flockCohesion);
		ig::slider(flockCohesionSpeed);
		ig::slider(flockNeighborRadius, "%.1f");
		ig::slider(mergeDistance, "%.1f");
		ig::slider(flockSpawnRate, "%.0f");
		ig::slider(flockMinAlive);
		ig::slider(flockDamping);
		ig::slider(particleSizeMin, "%.1f");
		ig::slider(particleSizeMax, "%.1f");
	}

	if (ig::section("Lifecycle / Fade")) {
		ig::check(lifecycleMode);
		ig::sliderInt(lifespanMin);
		ig::sliderInt(lifespanMax);
		ImGui::Separator();
		ig::sliderInt(fadeInFrames);
		ig::sliderInt(fadeOutFrames);
	}

	if (ig::section("Flash / Accent")) {
		ig::sliderInt(flashFrames);
		ig::slider(flashIntensity);
		ImGui::Separator();
		ig::slider(accentChance);
		ig::slider(accentSizeMul);
	}

	if (ig::section("Cluster Detection")) {
		ig::sliderInt(clusterGridRes);
		ig::sliderInt(clusterMinFlash);
		ig::check(showClusterGrid);
	}

	if (ig::section("Trail")) {
		ig::sliderInt(tailLength);
		ig::slider(tailAudioSensitivity);
		ig::slider(tailAlpha);
	}

	if (ig::section("Material")) {
		ig::slider(matBrightness);
		ig::slider(matSpecular);
		ig::slider(matAmbient);
		ig::slider(matGlow);
	}

	if (ig::section("Conductor → Visual")) {
		ImGui::TextWrapped(
			"How strongly does the Morphology Conductor modulate the "
			"visual field amplitudes (论文：Spectromorphological Synchresis)?");
		ig::slider(conductorAmount);
		ImGui::TextDisabled("0 = no effect; 1 = full coupling.");
	}
}

//==============================================================
//  Lifecycle
//==============================================================
void Flock3D::setup(int w, int h){
	width  = w;
	height = h;

	cam.setDistance(1000.0f);
	cam.setNearClip(1.0f);
	cam.setFarClip(5000.0f);
	cam.setPosition(700.0f, -400.0f, 700.0f);
	cam.lookAt(glm::vec3(0));

	// Seed XorShift PRNG（避免全 0 lock 状态；混入 frame time）
	uint64_t t = (uint64_t)(ofGetElapsedTimeMicros() | 1);
	prngState = (uint32_t)(t ^ (t >> 32));
	if (prngState == 0) prngState = 0xC0FFEE13u;

	loadShaderInline();
	resizeParticles();
}

//--------------------------------------------------------------
void Flock3D::reset(){
	resizeParticles();
	noiseTimeOffset = 0.0f;
}

//==============================================================
//  Particles
//==============================================================
void Flock3D::respawnFlockParticle(Particle& p){
	float r = worldRadius.get();
	p.pos = glm::vec3(ofRandom(-r, r), ofRandom(-r, r), ofRandom(-r, r));
	p.velocity = glm::vec3(ofRandom(-30.0f, 30.0f), ofRandom(-30.0f, 30.0f), ofRandom(-30.0f, 30.0f));

	float h = hueBase + ofRandom(-hueRange * 0.5f, hueRange * 0.5f);
	h = fmod(h + 1.0f, 1.0f);
	p.color = ofFloatColor::fromHsb(h, 0.75f, brightness, particleAlpha);

	p.size  = ofRandom(particleSizeMin, particleSizeMax);
	p.mass  = p.size;
	p.alive = true;
	p.maxLifetime = (int)ofRandom(lifespanMin, lifespanMax + 1);
	p.lifetime    = p.maxLifetime;

	p.fadeInTimer  = fadeInFrames;
	p.fadeOutTimer = -1;
	p.flashTimer   = 0;
	p.flashScale   = 1.0f;

	// Trail：重生时清空历史（防止位置跳变拉出长线）
	p.trailWriteIdx = 0;
	p.trailCount    = 0;
	// 也把所有历史位置预填为当前位置（防止初帧渲染时残留）
	for (int i = 0; i < TRAIL_MAX; i++) {
		p.trail[i] = p.pos;
	}
}

//--------------------------------------------------------------
void Flock3D::mergeIntoBigger(Particle& winner, Particle& loser){
	float totalMass = winner.mass + loser.mass;
	float newSize = powf(totalMass, 1.0f / 3.0f) * 2.0f;
	winner.size = std::min(newSize, particleSizeMax.get() * 3.0f);

	// 记录 loser 的 size（碰撞事件需要在 winner.mass 改之前抓）
	float loserSize = loser.size;
	float prevWinnerSize = winner.size;

	winner.mass = totalMass;
	winner.color.r = (winner.color.r * winner.mass + loser.color.r * loser.mass) / totalMass;
	winner.color.g = (winner.color.g * winner.mass + loser.color.g * loser.mass) / totalMass;
	winner.color.b = (winner.color.b * winner.mass + loser.color.b * loser.mass) / totalMass;
	winner.color.a = std::min(1.0f, winner.color.a + loser.color.a * 0.1f);

	// ─── Accent 抛骰子（一次决定视觉 + 音频，保持同步）───
	bool isAccent = (ofRandom(1.0f) < accentChance);

	// ─── 关键：把碰撞事件抛给外部（ofApp / 音频模块）───
	CollisionEvent ev;
	ev.pos        = winner.pos;
	ev.newMass    = totalMass;
	ev.winnerSize = prevWinnerSize;
	ev.loserSize  = loserSize;
	ev.color      = winner.color;
	ev.isAccent   = isAccent;
	collisionsThisFrame.push_back(ev);

	// winner 触发 flash（accent 命中 → 更大 + 更久）
	winner.flashTimer = flashFrames * (isAccent ? 2 : 1);
	winner.flashScale = isAccent ? accentSizeMul.get() : 1.0f;

	// loser 启动淡出
	if (loser.fadeOutTimer < 0) {
		loser.fadeOutTimer = fadeOutFrames;
	}
}

//--------------------------------------------------------------
void Flock3D::resizeParticles(){
	particles.clear();
	particles.reserve(particleCount);
	for (int i = 0; i < particleCount; i++) {
		Particle p;
		respawnFlockParticle(p);
		particles.push_back(p);
	}
}

//==============================================================
//  Field force
//==============================================================
glm::vec3 Flock3D::computeFieldForce(const glm::vec3& pos, float ns) const {
	glm::vec3 total(0);
	const float EPS = 0.001f;

	if (noiseAmplitude > EPS) {
		float s = noiseAmplitude * 4.0f;
		glm::vec3 nf(
			ofNoise(pos.x * ns,         pos.y * ns,         pos.z * ns + noiseTimeOffset)         - 0.5f,
			ofNoise(pos.x * ns + 100,   pos.y * ns + 100,   pos.z * ns + 100 + noiseTimeOffset)   - 0.5f,
			ofNoise(pos.x * ns + 200,   pos.y * ns + 200,   pos.z * ns + 200 + noiseTimeOffset)   - 0.5f
		);
		total += nf * s;
	}

	if (vortexAmp > EPS) {
		float r2 = pos.x * pos.x + pos.z * pos.z;
		if (r2 >= 1.0f) {
			float s = vortexAmp * 4.0f;
			float invR = 1.0f / sqrtf(r2);
			total += glm::vec3(-pos.z * invR, 0.0f, pos.x * invR) * s;
		}
	}

	if (spiralAmp > EPS) {
		float s = spiralAmp * 4.0f;
		float r2 = pos.x * pos.x + pos.z * pos.z;
		if (r2 < 1.0f) {
			total += glm::vec3(0, s * 0.5f, 0);
		} else {
			float invR = 1.0f / sqrtf(r2);
			glm::vec3 tangent(-pos.z * invR, 0.0f, pos.x * invR);
			glm::vec3 inward(-pos.x * invR, 0.0f, -pos.z * invR);
			total += (tangent * 1.0f + inward * 0.3f + glm::vec3(0, 0.6f, 0)) * s;
		}
	}

	if (curlAmp > EPS) {
		float s = curlAmp * 4.0f;
		glm::vec3 nf(
			ofNoise(pos.y * ns + 50,  pos.z * ns + 100, noiseTimeOffset)         - 0.5f,
			ofNoise(pos.z * ns + 100, pos.x * ns + 50,  noiseTimeOffset + 100)   - 0.5f,
			ofNoise(pos.x * ns + 50,  pos.y * ns + 100, noiseTimeOffset + 200)   - 0.5f
		);
		total += nf * (s * 1.5f);
	}

	if (attractorAmp > EPS) {
		float s = attractorAmp * 4.0f;
		total += -pos * (s * 0.02f);
	}

	if (repellerAmp > EPS) {
		float r2 = glm::dot(pos, pos);
		if (r2 >= 1.0f) {
			float s = repellerAmp * 4.0f;
			float invR2 = 1.0f / r2;
			total += pos * invR2 * (s * 8000.0f);
		}
	}

	// Morphology Conductor 调制（论文 Spectromorphological Synchresis）：
	// conductorValue 0..1（0.5=baseline）经 conductorAmount 缩放后乘到 total。
	// At amount=0: 不影响（scalar=1）。
	// At amount=1, conductor=1.0: scalar=2.0（双倍 field force）。
	// At amount=1, conductor=0.0: scalar=0.0（field force 归零，粒子静止）。
	float ca = conductorAmount.get();
	float conductorScalar = 1.0f + (conductorValue - 0.5f) * 2.0f * ca;
	total *= conductorScalar;

	return total;
}

//==============================================================
//  Update
//==============================================================
void Flock3D::update(){
	// 清空上一帧的碰撞事件
	collisionsThisFrame.clear();

	if ((int)particles.size() != particleCount) {
		resizeParticles();
	}

	float dt = ofGetLastFrameTime();
	if (dt > 0.1f) dt = 0.1f;

	int n = (int)particles.size();
	if (n == 0) return;

	noiseTimeOffset += noiseSpeed * 0.01f;
	float ns        = noiseScale;
	float nbrR      = flockNeighborRadius;
	float nbrRSq    = nbrR * nbrR;
	float mergeD    = mergeDistance;
	float mergeSq   = mergeD * mergeD;
	float sepW      = flockSeparation;
	float cohW      = flockCohesion;
	float cohSpeed  = flockCohesionSpeed;
	float damping   = flockDamping;
	float maxDist   = worldRadius.get() * 2.5f;
	float maxDistSq = maxDist * maxDist;
	const int K     = 10;

	int frameParity = (int)(ofGetFrameNum() & 1);

	// ─── 1. boid + merge ───
	for (int i = 0; i < n; i++) {
		auto& p = particles[i];
		if (!p.alive) continue;

		bool pFading = (p.fadeOutTimer >= 0);

		glm::vec3 separation(0);
		glm::vec3 cohesionCenter(0);
		int cohesionCount = 0;

		for (int k = 0; k < K; k++) {
			int j = fastRandBelow(n);   // 替代 std::rand() % n（避免 libc lock）
			if (j == i) continue;
			auto& q = particles[j];
			if (!q.alive) continue;
			bool qFading = (q.fadeOutTimer >= 0);

			glm::vec3 diff = p.pos - q.pos;
			float distSq = glm::dot(diff, diff);

			if (distSq < mergeSq && !pFading && !qFading) {
				if (p.size >= q.size) {
					mergeIntoBigger(p, q);
				} else {
					mergeIntoBigger(q, p);
					break;
				}
				continue;
			}

			if (distSq < nbrRSq && distSq > 0.0001f) {
				float dist = sqrtf(distSq);
				float scale = (nbrR - dist) / (dist * nbrR);
				separation     += diff * scale;
				cohesionCenter += q.pos;
				cohesionCount++;
			}
		}

		if (!p.alive) continue;

		glm::vec3 force(0);
		if (cohesionCount > 0) {
			cohesionCenter /= float(cohesionCount);
			force += (cohesionCenter - p.pos) * (cohW * cohSpeed);
		}
		force += separation * 30.0f * sepW;

		if ((i & 1) == frameParity) {
			force += computeFieldForce(p.pos, ns);
		}

		float distFromOriginSq = glm::dot(p.pos, p.pos);
		if (distFromOriginSq > maxDistSq) {
			float distFromOrigin = sqrtf(distFromOriginSq);
			force -= p.pos / distFromOrigin * (distFromOrigin - maxDist) * 0.5f;
		}

		p.velocity += force * dt;
		p.velocity *= damping;
		p.pos      += p.velocity * dt;
	}

	// ─── 2. Spawn ───
	int aliveNow = 0;
	for (auto& p : particles) if (p.alive) aliveNow++;

	int targetAlive    = (int)(n * flockMinAlive.get());
	int baseSpawn      = (int)(flockSpawnRate.get() * dt);
	int deficit        = targetAlive - aliveNow;
	int spawnsThisFrame = std::max(baseSpawn, deficit);
	int perFrameCap     = std::max(1, n / 15);
	if (spawnsThisFrame > perFrameCap) spawnsThisFrame = perFrameCap;

	int spawned = 0;
	for (int i = 0; i < n && spawned < spawnsThisFrame; i++) {
		if (!particles[i].alive) {
			respawnFlockParticle(particles[i]);
			spawned++;
		}
	}

	// ─── 3. Lifecycle ───
	if (lifecycleMode) {
		for (auto& p : particles) {
			if (!p.alive) continue;
			if (p.fadeOutTimer >= 0) continue;
			p.lifetime--;
			if (p.lifetime <= 0) {
				p.fadeOutTimer = fadeOutFrames;
			}
		}
	}

	// ─── 4. Fade + flash tick ───
	for (auto& p : particles) {
		if (!p.alive) continue;
		if (p.fadeInTimer > 0) p.fadeInTimer--;
		if (p.flashTimer  > 0) p.flashTimer--;

		if (p.fadeOutTimer == 0) {
			p.alive = false;
		} else if (p.fadeOutTimer > 0) {
			p.fadeOutTimer--;
			if (p.fadeOutTimer == 0) {
				p.alive = false;
			}
		}
	}

	// ─── 5. Trail push：把每个活粒子当前位置推入环形 buffer ───
	// 用 if/-- 替代 modulo（TRAIL_MAX=24 不是 2 的幂，整数 mod 是 div 指令；
	// 每帧 20K × 1 = 20K mod，省下来不多但是干净）
	for (auto& p : particles) {
		if (!p.alive) continue;
		p.trail[p.trailWriteIdx] = p.pos;
		if (++p.trailWriteIdx >= TRAIL_MAX) p.trailWriteIdx = 0;
		if (p.trailCount < TRAIL_MAX) p.trailCount++;
	}
}

//==============================================================
//  Draw
//==============================================================
void Flock3D::draw(){
	ofBackground(8, 8, 12);

	cam.begin();
	if (autoRotate) {
		ofRotateYDeg(ofGetElapsedTimef() * 30.0f);
	}

	if (showAxis) {
		ofDrawAxis(150.0f);
	}

	// 复用 particleMesh（保留 vector capacity，clear 不释放内存）
	particleMesh.clear();
	particleMesh.setMode(OF_PRIMITIVE_POINTS);
	// 预留 capacity（粒子数为上限）
	{
		auto& v = particleMesh.getVertices();
		auto& c = particleMesh.getColors();
		auto& t = particleMesh.getTexCoords();
		if (v.capacity() < particles.size()) {
			v.reserve(particles.size());
			c.reserve(particles.size());
			t.reserve(particles.size());
		}
	}

	float fInFrames  = (float)fadeInFrames;
	float fOutFrames = (float)fadeOutFrames;
	float flFrames   = (float)flashFrames;
	float flInt      = flashIntensity;

	// ─── 视觉 EnergyStage（论文 trans-modal "warming / scale" 等映射）───
	// 跟 Synth 端 5 个 stage 对等
	float ca = conductorAmount.get();
	float energy = 0.5f * (1.0f - ca) + conductorValue * ca;
	// stageSize: 粒子 size 中后期才放大（exp，0.4..1.0）
	static const EnergyStage stageSize {0.4f, 1.0f, 1};
	// blend 0..1，再映射到 0.5..1.5 倍率（ca=0 时永远 1）
	float sizeStageOf = stageSize.stageOf(energy);
	float sizeMult = (1.0f - ca) + ca * (0.5f + sizeStageOf);    // 0.5..1.5 范围
	// stageBri: brightness 跟得早一些（0.2..1.0 linear）
	static const EnergyStage stageBri  {0.2f, 1.0f, 0};
	float effBri = stageBri.blend(energy, matBrightness.get(), ca);

	for (auto& p : particles) {
		if (!p.alive) continue;

		float fadeIn  = 1.0f;
		if (fInFrames > 0.0f && p.fadeInTimer > 0) {
			fadeIn = 1.0f - (float)p.fadeInTimer / fInFrames;
			if (fadeIn < 0.0f) fadeIn = 0.0f;
		}
		float fadeOut = 1.0f;
		if (fOutFrames > 0.0f && p.fadeOutTimer > 0) {
			fadeOut = (float)p.fadeOutTimer / fOutFrames;
			if (fadeOut < 0.0f) fadeOut = 0.0f;
		}

		// Flash：0 = 无；1 = 刚 merge，逐渐衰减到 0
		float flashAmt = 0.0f;
		if (flFrames > 0.0f && p.flashTimer > 0) {
			flashAmt = (float)p.flashTimer / flFrames;
		}
		flashAmt *= flInt;   // 用户控制强度

		particleMesh.addVertex(p.pos);

		// 颜色：闪烁时 lerp 向白 + 提升 alpha（HDR 感）
		ofFloatColor c = p.color;
		if (flashAmt > 0.0f) {
			float k = ofClamp(flashAmt, 0.0f, 1.0f);
			c.r = c.r * (1.0f - k) + 1.0f * k;
			c.g = c.g * (1.0f - k) + 1.0f * k;
			c.b = c.b * (1.0f - k) + 1.0f * k;
			c.a = std::min(1.0f, c.a * (1.0f + flashAmt));
		}
		c.a *= fadeIn * fadeOut;

		// Size：闪烁时短暂放大；accent 用 flashScale 进一步放大
		// 普通 merge: flashScale=1 → max 2.5x
		// accent merge: flashScale~2.5 → max ~6x（受 shader 上限 96px 截断）
		float displaySize = p.size * sizeMult * (1.0f + flashAmt * 1.5f * p.flashScale);

		particleMesh.addColor(c);
		particleMesh.addTexCoord(glm::vec2(displaySize, 0.0f));
	}

	ofEnableBlendMode(OF_BLENDMODE_ADD);
	glEnable(GL_PROGRAM_POINT_SIZE);

	particleShader.begin();
	particleShader.setUniform1f("uBrightness", effBri);
	particleShader.setUniform1f("uSpecular",   matSpecular);
	particleShader.setUniform1f("uAmbient",    matAmbient);
	particleShader.setUniform1f("uGlow",       matGlow);
	particleMesh.draw();
	particleShader.end();

	glDisable(GL_PROGRAM_POINT_SIZE);

	// ─── Trail（光束尾巴）───
	// effLen = baseLen × (0.5 + audioInfluence × sensitivity × 1.5)
	// audio 全静（influence=0）→ 0.5x；audio 全亮（influence=1, sens=1）→ 2.0x
	int baseLen = tailLength;
	float sens = tailAudioSensitivity;
	float scale = 0.5f + audioInfluence * sens * 1.5f;
	int effLen = (int)(baseLen * scale);
	if (effLen < 2) effLen = 0;
	if (effLen > TRAIL_MAX) effLen = TRAIL_MAX;

	if (effLen >= 2 && tailAlpha > 0.001f) {
		// 长尾自动 stride：effLen 越大 step 越大，限制总段数（避免顶点爆炸）
		// effLen 1..12 → step=1（满细节）
		// effLen 13..18 → step=2（跳点采样）
		// effLen 19..24 → step=3
		int step = 1;
		if (effLen > 18)      step = 3;
		else if (effLen > 12) step = 2;
		int segmentsPerParticle = (effLen - 1) / step;

		// 复用 trailMesh
		trailMesh.clear();
		trailMesh.setMode(OF_PRIMITIVE_LINES);

		auto& tv = trailMesh.getVertices();
		auto& tc = trailMesh.getColors();

		// reserve 上限 = particle 数 × 段数 × 2 顶点
		size_t maxVerts = particles.size() * (size_t)segmentsPerParticle * 2;
		if (tv.capacity() < maxVerts) {
			tv.reserve(maxVerts);
			tc.reserve(maxVerts);
		}

		float tailA = tailAlpha;

		for (const auto& p : particles) {
			if (!p.alive) continue;
			int count = (p.trailCount < effLen) ? p.trailCount : effLen;
			if (count < 2) continue;

			int actualSegs = (count - 1) / step;
			if (actualSegs < 1) continue;
			float invSegs = 1.0f / (float)actualSegs;

			// 预乘 tailA 到 color（每粒子一次）
			ofFloatColor baseCol = p.color;
			float baseAlpha = baseCol.a * tailA;

			// 起始索引：count 步走回 oldest
			int idx = p.trailWriteIdx - count;
			while (idx < 0) idx += TRAIL_MAX;
			int nextIdx = idx + step;
			if (nextIdx >= TRAIL_MAX) nextIdx -= TRAIL_MAX;

			// 内循环：用 stride 跳点 + 无 modulo
			for (int s = 0; s < actualSegs; s++) {
				float fadeOld = (float)s * invSegs;
				float fadeNew = (float)(s + 1) * invSegs;

				ofFloatColor c0(baseCol.r, baseCol.g, baseCol.b, baseAlpha * fadeOld);
				ofFloatColor c1(baseCol.r, baseCol.g, baseCol.b, baseAlpha * fadeNew);

				tv.push_back(p.trail[idx]);
				tv.push_back(p.trail[nextIdx]);
				tc.push_back(c0);
				tc.push_back(c1);

				// stride 推进（无 modulo）
				idx = nextIdx;
				nextIdx += step;
				if (nextIdx >= TRAIL_MAX) nextIdx -= TRAIL_MAX;
			}
		}

		trailMesh.draw();
	}

	ofDisableBlendMode();

	// ─── Debug：cluster grid 可视化 ───
	if (showClusterGrid && lastBboxValid && !lastCellCounts.empty()) {
		ofPushStyle();
		ofNoFill();

		// 外框：粒子 bbox（白色淡）
		glm::vec3 bboxCenter = (lastBboxMin + lastBboxMax) * 0.5f;
		glm::vec3 bboxSize   = lastBboxMax - lastBboxMin;
		ofSetColor(255, 255, 255, 70);
		ofDrawBox(bboxCenter, bboxSize.x, bboxSize.y, bboxSize.z);

		// 每个 cell 的可视化
		glm::vec3 cellSize = bboxSize / (float)lastGridRes;
		int thresh = std::max(1, (int)clusterMinFlash);
		int totalCells = lastGridRes * lastGridRes * lastGridRes;
		for (int idx = 0; idx < totalCells; idx++) {
			int cnt = lastCellCounts[idx];
			if (cnt <= 0) continue;   // 空 cell 不画

			int cz = idx / (lastGridRes * lastGridRes);
			int cy = (idx / lastGridRes) % lastGridRes;
			int cx = idx % lastGridRes;
			glm::vec3 cellCenter = lastBboxMin + cellSize * glm::vec3(cx + 0.5f, cy + 0.5f, cz + 0.5f);

			if (cnt >= thresh) {
				// 达阈值：红色 cluster cell
				ofSetColor(255, 70, 70, 220);
			} else {
				// 有闪烁但未达阈值：蓝色
				ofSetColor(120, 140, 255, 110);
			}
			ofDrawBox(cellCenter, cellSize.x * 0.95f, cellSize.y * 0.95f, cellSize.z * 0.95f);
		}

		ofPopStyle();
	}

	cam.end();

	// HUD
	ofSetColor(255, 200);
	int aliveCount = 0;
	for (auto& p : particles) if (p.alive) aliveCount++;
	int target = (int)(particles.size() * flockMinAlive);
	std::string perfStr = "fps: " + ofToString(ofGetFrameRate(), 1) +
	                      "   alive: " + ofToString(aliveCount) + " / " + ofToString((int)particles.size()) +
	                      "   collisions/frame: " + ofToString((int)collisionsThisFrame.size()) +
	                      "   (target ≥ " + ofToString(target) + ")";
	ofDrawBitmapString(perfStr, 20, ofGetHeight() - 60);
}

//==============================================================
//  Keyboard
//==============================================================
void Flock3D::keyPressed(int /*key*/){
}

//==============================================================
//  Stats / Clusters — 给音频合成器消费
//==============================================================
Flock3D::Stats Flock3D::getStats() const {
	Stats s;
	int n = (int)particles.size();
	if (n == 0) return s;

	int aliveCount = 0;
	float speedSum = 0;
	float distSum  = 0;
	glm::vec3 posSum(0);

	for (const auto& p : particles) {
		if (!p.alive) continue;
		aliveCount++;
		speedSum += glm::length(p.velocity);
		distSum  += glm::length(p.pos);
		posSum   += p.pos;
	}

	if (aliveCount > 0) {
		s.aliveRatio = (float)aliveCount / n;
		s.meanSpeed  = speedSum / aliveCount;
		s.spread     = distSum  / aliveCount;
		s.meanPos    = posSum   / (float)aliveCount;
	}

	s.collisionCount = (int)collisionsThisFrame.size();

	// 归一化 field amp（让 6 个 amp 总和 = 1，相对权重）
	float total = noiseAmplitude + vortexAmp + spiralAmp + curlAmp + attractorAmp + repellerAmp;
	if (total > 0.001f) {
		s.fieldNoise     = noiseAmplitude / total;
		s.fieldVortex    = vortexAmp / total;
		s.fieldSpiral    = spiralAmp / total;
		s.fieldCurl      = curlAmp / total;
		s.fieldAttractor = attractorAmp / total;
		s.fieldRepeller  = repellerAmp / total;
	}
	return s;
}

//--------------------------------------------------------------
//  Cluster detection（极简版：只数闪烁粒子 + 直接阈值）
//  - 世界切成 gridRes³ 个 cell（GUI 控制）
//  - 只数"正在闪烁"的粒子（flashTimer > 0，刚 merge 后几帧内）
//  - cell 内闪烁粒子数 ≥ minFlash → 该 cell 是一个 cluster
//
//  为什么只数闪烁粒子：
//    - 闪烁 = 刚发生 merge → 直接指示"聚集活跃区"
//    - 排除背景均匀分布的干扰 → 信噪比极高
//    - 数量少（merge 多时几百，一般几十）→ 简单阈值就清晰
//--------------------------------------------------------------
//  getVisualEnergyMeasured —— "系统看见自己"
//  返回 [0..1] 综合视觉能量，给 Synchresis 自感知用
//
//  composite = 0.4 × density + 0.4 × speedNorm + 0.2 × brightness
//    - density:    alive / total particle count (0..1)
//    - speedNorm:  mean particle speed / 200 (0..1，200 是磁数 ~ 满 field 速度)
//    - brightness: mean color RGB 平均 (0..1)
//--------------------------------------------------------------
float Flock3D::getVisualEnergyMeasured() const {
	if (particles.empty()) return 0.0f;

	int aliveCount = 0;
	float speedSum = 0.0f;
	float briSum   = 0.0f;
	for (const auto& p : particles) {
		if (!p.alive) continue;
		aliveCount++;
		speedSum += glm::length(p.velocity);
		briSum   += (p.color.r + p.color.g + p.color.b) * (1.0f / 3.0f);
	}
	if (aliveCount == 0) return 0.0f;

	float density  = float(aliveCount) / float(particles.size());
	float meanSpd  = speedSum / float(aliveCount);
	float speedN   = meanSpd / 200.0f;
	if (speedN > 1.0f) speedN = 1.0f;
	float meanBri  = briSum / float(aliveCount);

	float energy = 0.4f * density + 0.4f * speedN + 0.2f * meanBri;
	if (energy < 0.0f) energy = 0.0f;
	if (energy > 1.0f) energy = 1.0f;
	return energy;
}

std::vector<Flock3D::Cluster> Flock3D::getClusters(int maxK) const {
	int gridRes = clusterGridRes;
	if (gridRes < 2) gridRes = 2;
	int totalCells = gridRes * gridRes * gridRes;

	// 步骤 0：算 alive 粒子的 axis-aligned bounding box（grid 紧贴粒子区域）
	glm::vec3 minPos(1e9f), maxPos(-1e9f);
	bool any = false;
	for (const auto& p : particles) {
		if (!p.alive) continue;
		if (p.fadeOutTimer >= 0) continue;
		if (p.pos.x < minPos.x) minPos.x = p.pos.x;
		if (p.pos.y < minPos.y) minPos.y = p.pos.y;
		if (p.pos.z < minPos.z) minPos.z = p.pos.z;
		if (p.pos.x > maxPos.x) maxPos.x = p.pos.x;
		if (p.pos.y > maxPos.y) maxPos.y = p.pos.y;
		if (p.pos.z > maxPos.z) maxPos.z = p.pos.z;
		any = true;
	}

	if (!any) {
		lastBboxValid = false;
		lastCellCounts.clear();
		return {};
	}

	// 边缘 padding（避免 bbox 边界粒子映射到 cell 越界）
	glm::vec3 pad(2.0f);
	minPos -= pad;
	maxPos += pad;
	glm::vec3 bboxSize = maxPos - minPos;
	if (bboxSize.x < 1.0f) bboxSize.x = 1.0f;
	if (bboxSize.y < 1.0f) bboxSize.y = 1.0f;
	if (bboxSize.z < 1.0f) bboxSize.z = 1.0f;

	struct Cell {
		float mass = 0.0f;
		int   count = 0;
		glm::vec3 posSum{0};
		glm::vec3 velSum{0};
		float colR = 0, colG = 0, colB = 0;
	};
	std::vector<Cell> grid(totalCells);

	// Grid 紧贴 bbox（不再用整个 world）
	glm::vec3 invScale(
		(float)gridRes / bboxSize.x,
		(float)gridRes / bboxSize.y,
		(float)gridRes / bboxSize.z
	);

	// 只数闪烁粒子（merge 完后 flashTimer > 0 的）
	for (const auto& p : particles) {
		if (!p.alive) continue;
		if (p.fadeOutTimer >= 0) continue;
		if (p.flashTimer <= 0) continue;   // ← 关键：只数闪烁

		// 相对 bbox 起点的坐标 → cell index
		glm::vec3 rel = (p.pos - minPos);
		int ix = (int)(rel.x * invScale.x);
		int iy = (int)(rel.y * invScale.y);
		int iz = (int)(rel.z * invScale.z);
		if (ix < 0) ix = 0; if (ix >= gridRes) ix = gridRes - 1;
		if (iy < 0) iy = 0; if (iy >= gridRes) iy = gridRes - 1;
		if (iz < 0) iz = 0; if (iz >= gridRes) iz = gridRes - 1;

		int idx = ix + iy * gridRes + iz * gridRes * gridRes;
		Cell& cell = grid[idx];
		cell.mass  += p.mass;
		cell.count++;
		cell.posSum += p.pos;
		cell.velSum += p.velocity;
		cell.colR += p.color.r;
		cell.colG += p.color.g;
		cell.colB += p.color.b;
	}

	// 缓存 grid 信息给 draw() 做可视化
	lastBboxMin = minPos;
	lastBboxMax = maxPos;
	lastGridRes = gridRes;
	lastBboxValid = true;
	lastCellCounts.resize(totalCells);
	for (int i = 0; i < totalCells; i++) lastCellCounts[i] = grid[i].count;

	int cellThreshold = std::max(1, (int)clusterMinFlash);

	// 每个超阈 cell 直接成为一个 cluster（不合并、不 BFS）
	std::vector<Cluster> clusters;
	clusters.reserve(maxK + 8);

	for (int idx = 0; idx < totalCells; idx++) {
		const Cell& cell = grid[idx];
		if (cell.count < cellThreshold) continue;

		Cluster c;
		c.particleCount = cell.count;
		c.totalMass     = cell.mass;
		float invN = 1.0f / (float)cell.count;
		c.centroid = cell.posSum * invN;
		c.velocity = cell.velSum * invN;
		c.avgColor = ofFloatColor(cell.colR * invN, cell.colG * invN, cell.colB * invN, 1.0f);
		clusters.push_back(c);
	}

	// 按质量降序，取 top-K（drone voice 池上限是 4）
	std::sort(clusters.begin(), clusters.end(),
		[](const Cluster& a, const Cluster& b){ return a.totalMass > b.totalMass; });
	if ((int)clusters.size() > maxK) clusters.resize(maxK);
	return clusters;
}

//--------------------------------------------------------------
std::vector<Flock3D::ClusterCandidate> Flock3D::getTopByMass(int K) const {
	// 简单 O(N log K)：维护一个最小堆（用 vector + partial_sort）取 top-K
	// K 一般 ≤ 8，O(N) 扫一次足够
	std::vector<ClusterCandidate> result;
	if (K <= 0) return result;
	result.reserve(K);

	// 用一个排好序的小数组维护当前 top-K（按 mass 升序，最小的在前）
	for (const auto& p : particles) {
		if (!p.alive) continue;
		if (p.fadeOutTimer >= 0) continue;   // 排除正在死的

		if ((int)result.size() < K) {
			ClusterCandidate c{p.pos, p.velocity, p.mass, p.color};
			// 插入并保持升序
			auto it = std::lower_bound(result.begin(), result.end(), c,
				[](const ClusterCandidate& a, const ClusterCandidate& b){ return a.mass < b.mass; });
			result.insert(it, c);
		} else if (p.mass > result.front().mass) {
			// 比最小的还大 → 替换最小，重新排序
			result.front() = ClusterCandidate{p.pos, p.velocity, p.mass, p.color};
			std::sort(result.begin(), result.end(),
				[](const ClusterCandidate& a, const ClusterCandidate& b){ return a.mass < b.mass; });
		}
	}

	// 返回前按 mass 降序（最大的在 [0]）
	std::reverse(result.begin(), result.end());
	return result;
}

//==============================================================
//  Shader
//==============================================================
void Flock3D::loadShaderInline(){
	// Vertex: 传 base color + 距离衰减给 frag
	std::string vert = R"(
		#version 150
		uniform mat4 modelViewProjectionMatrix;
		uniform mat4 modelViewMatrix;
		in vec4 position;
		in vec4 color;
		in vec2 texcoord;

		out vec4 vColor;
		out float vDepthFade;

		void main(){
			vec4 viewPos = modelViewMatrix * position;
			float dist   = max(1.0, length(viewPos.xyz));

			gl_Position  = modelViewProjectionMatrix * position;
			gl_PointSize = clamp(texcoord.x * 1000.0 / dist, 0.8, 96.0);

			vDepthFade = clamp(1100.0 / dist, 0.20, 1.5);
			vColor     = color;
		}
	)";

	// Fragment:
	//   非 flash 时：sphere（内 70%）+ halo（外 30%）
	//   flash 时：切到 rp-24 风格 — 简单 smoothstep 软盘（整面发光，无 shading）
	//   两种模式按 flashWeight 平滑 crossfade
	//
	// 抗锯齿（毛边消除）：
	//   GL_POINTS 不受 MSAA 影响，所以用 fwidth-based screen-space AA。
	//   - 外圈边缘用 fwidth 软渐变（替代硬 discard）
	//   - sphere → halo 过渡用 smoothstep 软化
	std::string frag = R"(
		#version 150
		uniform float uBrightness;
		uniform float uSpecular;
		uniform float uAmbient;
		uniform float uGlow;
		in  vec4  vColor;
		in  float vDepthFade;
		out vec4 fragColor;

		void main(){
			vec2 c = gl_PointCoord - vec2(0.5);
			float d2 = dot(c, c);
			// 早期 discard 留出 anti-aliased 边缘宽度（0.5 + 2px ≈ 0.55²）
			if (d2 > 0.30) discard;
			float d = sqrt(d2);

			// 屏幕空间像素宽度（自适应 zoom level，1-2 像素的软渐变）
			// 用 clamp 防止小粒子（< 4px）fwidth 过大反而被完全吞掉
			float aaW = clamp(fwidth(d), 0.001, 0.08);

			// Flash detect（color → 白时高）
			float vBright = (vColor.r + vColor.g + vColor.b) / 3.0;
			float flashWeight = smoothstep(0.65, 0.95, vBright);

			// ════════════════════════════════════════════════════════════
			// Mode A — Sphere + halo（非 flash）
			// ════════════════════════════════════════════════════════════
			const float sphereR  = 0.35;
			const float sphereR2 = sphereR * sphereR;

			// Sphere 软边（sphereR 处 1.0 → sphereR+aa 处 0.0），消除内外硬边
			float sphereMask = 1.0 - smoothstep(sphereR - aaW, sphereR + aaW, d);

			// Sphere shading：始终算（不用 if 分支，更稳定 + GPU 友好）
			// d > sphereR 区域被 sphereMask 软化为 0，不会产生瑕疵
			float z = sqrt(max(sphereR2 - d2, 0.0));
			vec3 N = vec3(c.x, c.y, z) / sphereR;
			vec3 L = normalize(vec3(-0.4, -0.5, 0.7));
			vec3 V = vec3(0.0, 0.0, 1.0);
			vec3 H = normalize(L + V);
			float NdotL = max(0.0, dot(N, L));
			float diffuse = NdotL * (1.0 - uAmbient) + uAmbient;
			float NdotH = max(0.0, dot(N, H));
			float spec = pow(NdotH, 28.0) * uSpecular;
			vec3 baseCol = vColor.rgb * vDepthFade * uBrightness;
			vec3 sphereCol = (baseCol * diffuse + vec3(spec) * vDepthFade) * sphereMask;
			float sphereA = vColor.a * sphereMask;

			// halo zone — 从 sphereR 渐变到 0.5（外圈）
			if (uGlow > 0.001) {
				float t = smoothstep(0.5, sphereR * 0.85, d);
				float haloI = t * t * uGlow;
				sphereCol += vColor.rgb * vDepthFade * uBrightness * haloI;
				sphereA = max(sphereA, haloI * vColor.a * 0.6);
			}

			// ════════════════════════════════════════════════════════════
			// Mode B — rp-24 风格软盘（flash 时使用）
			// 简单 smoothstep alpha 软盘，颜色不加处理
			// ════════════════════════════════════════════════════════════
			float discAlpha = smoothstep(0.5, 0.0, d);
			vec3 discCol = vColor.rgb;
			float discA = vColor.a * discAlpha;

			// 按 flashWeight crossfade 两种模式
			vec3 outCol = mix(sphereCol, discCol, flashWeight);
			float outA  = mix(sphereA, discA, flashWeight);

			// ★ 外圈边缘 anti-aliasing：fwidth-based 软渐变（自适应距离）
			// 在边缘 0.5 处过渡 1→0，宽度 ~1.5 像素，自动消除锯齿
			float edgeAA = 1.0 - smoothstep(0.5 - aaW * 1.5, 0.5, d);
			outA *= edgeAA;

			fragColor = vec4(outCol, outA);
		}
	)";

	particleShader.setupShaderFromSource(GL_VERTEX_SHADER,   vert);
	particleShader.setupShaderFromSource(GL_FRAGMENT_SHADER, frag);
	particleShader.bindDefaults();
	particleShader.linkProgram();
}
