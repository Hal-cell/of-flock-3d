#include "Flock3D.h"

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

	// ─── Cluster detection（DBSCAN + 持久化追踪 + 平滑）───
	// DBSCAN 检测当前帧：每粒子半径 R 内邻居数 ≥ minNeighbors → 核心点 → BFS 合并
	// 跨帧追踪：匹配后 age++；没匹配则 grace++；超出 grace 才彻底死亡
	// 平滑：centroid 用 EMA 防抖动
	group.add(clusterRadius.set("cluster radius",         25.0f, 5.0f, 100.0f));
	group.add(clusterMinNeighbors.set("cluster minNeighbors", 12,   3,    50));
	group.add(clusterMinCount.set("cluster minCount",      30,   5,    300));
	group.add(clusterMinAge.set("cluster minAge",          8,    1,    60));
	group.add(clusterGracePeriod.set("cluster gracePeriod", 30,   1,    120));
	group.add(clusterSmoothing.set("cluster smoothing",   0.7f, 0.0f, 0.95f));
	group.add(clusterTrackRadius.set("cluster trackRadius", 80.0f, 10.0f, 400.0f));

	// ─── Trail（光束尾巴）───
	// length = baseLen × (0.5 + audio_influence × sensitivity × 1.5)
	// audio_influence 综合 event decay / FM ratio / cluster cutoff
	group.add(tailLength.set("tail length",          8,    0, int(TRAIL_MAX)));
	group.add(tailAudioSensitivity.set("tail audio sens", 1.0f, 0.0f, 2.0f));
	group.add(tailAlpha.set("tail alpha",            0.45f, 0.0f, 1.0f));
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
			int j = std::rand() % n;
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
	for (auto& p : particles) {
		if (!p.alive) continue;
		p.trail[p.trailWriteIdx] = p.pos;
		p.trailWriteIdx = (p.trailWriteIdx + 1) % TRAIL_MAX;
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
		float displaySize = p.size * (1.0f + flashAmt * 1.5f * p.flashScale);

		particleMesh.addColor(c);
		particleMesh.addTexCoord(glm::vec2(displaySize, 0.0f));
	}

	ofEnableBlendMode(OF_BLENDMODE_ADD);
	glEnable(GL_PROGRAM_POINT_SIZE);

	particleShader.begin();
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
//  Cluster detection — DBSCAN + 持久化追踪 + 时间平滑
//
//  3 阶段：
//    A. DBSCAN（当前帧）：spatial hash → 邻居计数 → 核心点 → BFS 合并
//    B. 跨帧匹配：用 predict + greedy 把 raw 匹配到 tracked
//                未匹配的 tracked → gracePeriod++
//                未匹配的 raw → 新建 tracked
//                超出 grace → 真正死亡
//    C. 平滑输出：centroid 用 EMA，过滤 age 不够的（hysteresis）
//--------------------------------------------------------------
std::vector<Flock3D::Cluster> Flock3D::getClusters(int maxK) {
	int n = (int)particles.size();

	float wr = worldRadius.get();
	float radius = clusterRadius;
	float radiusSq = radius * radius;
	int   minNbrs = clusterMinNeighbors;
	int   minSize = clusterMinCount;

	// ────────────────────────────────────────
	// A. DBSCAN：当前帧的 raw clusters（没 ID，没历史）
	// ────────────────────────────────────────
	struct RawCluster {
		glm::vec3    centroid;
		glm::vec3    velocity;
		float        totalMass;
		ofFloatColor avgColor;
		int          particleCount;
	};
	std::vector<RawCluster> rawClusters;

	if (n > 0) {
		// Grid 自动按半径选大小，3×3×3 cells 覆盖搜索区
		int gridRes = std::max(4, std::min(40, (int)((wr * 2.0f) / radius)));
		int totalCells = gridRes * gridRes * gridRes;
		float invScale = (float)gridRes / (wr * 2.0f);

		// 复用 cached buffers
		if ((int)clusterGrid.size() != totalCells) {
			clusterGrid.resize(totalCells);
		}
		for (auto& cell : clusterGrid) cell.clear();

		if ((int)particleCellCache.size() != n) {
			particleCellCache.assign(n, -1);
		} else {
			std::fill(particleCellCache.begin(), particleCellCache.end(), -1);
		}

		// spatial hash
		for (int i = 0; i < n; i++) {
			const auto& p = particles[i];
			if (!p.alive) continue;
			if (p.fadeOutTimer >= 0) continue;

			int ix = (int)((p.pos.x + wr) * invScale);
			int iy = (int)((p.pos.y + wr) * invScale);
			int iz = (int)((p.pos.z + wr) * invScale);
			if (ix < 0) ix = 0; if (ix >= gridRes) ix = gridRes - 1;
			if (iy < 0) iy = 0; if (iy >= gridRes) iy = gridRes - 1;
			if (iz < 0) iz = 0; if (iz >= gridRes) iz = gridRes - 1;
			int idx = ix + iy * gridRes + iz * gridRes * gridRes;
			clusterGrid[idx].push_back(i);
			particleCellCache[i] = idx;
		}

		// helper：收集粒子 i 半径内的邻居
		auto collectNeighbors = [&](int i, std::vector<int>& out) {
			out.clear();
			int cellIdx = particleCellCache[i];
			if (cellIdx < 0) return;
			const auto& p = particles[i];
			int cz = cellIdx / (gridRes * gridRes);
			int cy = (cellIdx / gridRes) % gridRes;
			int cx = cellIdx % gridRes;
			for (int dz = -1; dz <= 1; dz++)
			for (int dy = -1; dy <= 1; dy++)
			for (int dx = -1; dx <= 1; dx++) {
				int nx = cx + dx, ny = cy + dy, nz = cz + dz;
				if (nx < 0 || nx >= gridRes) continue;
				if (ny < 0 || ny >= gridRes) continue;
				if (nz < 0 || nz >= gridRes) continue;
				int nIdx = nx + ny * gridRes + nz * gridRes * gridRes;
				for (int j : clusterGrid[nIdx]) {
					if (j == i) continue;
					glm::vec3 diff = particles[j].pos - p.pos;
					if (glm::dot(diff, diff) < radiusSq) out.push_back(j);
				}
			}
		};

		// 标记核心点
		if ((int)isCoreCache.size() != n) isCoreCache.assign(n, 0);
		else std::fill(isCoreCache.begin(), isCoreCache.end(), 0);
		tmpNbrsCache.clear();
		tmpNbrsCache.reserve(128);

		for (int i = 0; i < n; i++) {
			if (particleCellCache[i] < 0) continue;
			collectNeighbors(i, tmpNbrsCache);
			if ((int)tmpNbrsCache.size() >= minNbrs) isCoreCache[i] = 1;
		}

		// BFS 把核心点连成 cluster
		if ((int)particleClusterCache.size() != n) particleClusterCache.assign(n, -1);
		else std::fill(particleClusterCache.begin(), particleClusterCache.end(), -1);
		bfsStackCache.clear();

		for (int seed = 0; seed < n; seed++) {
			if (!isCoreCache[seed]) continue;
			if (particleClusterCache[seed] >= 0) continue;

			int curId = (int)rawClusters.size();
			particleClusterCache[seed] = curId;
			bfsStackCache.clear();
			bfsStackCache.push_back(seed);

			glm::vec3 sumPos(0), sumVel(0);
			float sumMass = 0, sumR = 0, sumG = 0, sumB = 0;
			int sumCount = 0;

			while (!bfsStackCache.empty()) {
				int cur = bfsStackCache.back();
				bfsStackCache.pop_back();
				const auto& p = particles[cur];
				sumPos += p.pos;       sumVel += p.velocity;  sumMass += p.mass;
				sumR += p.color.r;     sumG += p.color.g;     sumB += p.color.b;
				sumCount++;

				collectNeighbors(cur, tmpNbrsCache);
				for (int j : tmpNbrsCache) {
					if (particleClusterCache[j] >= 0) continue;
					particleClusterCache[j] = curId;
					if (isCoreCache[j]) {
						bfsStackCache.push_back(j);
					} else {
						const auto& pj = particles[j];
						sumPos += pj.pos;     sumVel += pj.velocity; sumMass += pj.mass;
						sumR += pj.color.r;   sumG += pj.color.g;    sumB += pj.color.b;
						sumCount++;
					}
				}
			}

			if (sumCount < minSize) continue;

			RawCluster rc;
			rc.particleCount = sumCount;
			rc.totalMass     = sumMass;
			float invN = 1.0f / (float)sumCount;
			rc.centroid = sumPos * invN;
			rc.velocity = sumVel * invN;
			rc.avgColor = ofFloatColor(sumR * invN, sumG * invN, sumB * invN, 1.0f);
			rawClusters.push_back(rc);
		}
	}

	// ────────────────────────────────────────
	// B. 跨帧匹配：raw clusters ↔ tracked clusters
	// ────────────────────────────────────────
	float trackR    = clusterTrackRadius;
	float trackRSq  = trackR * trackR;
	float smoothing = ofClamp(clusterSmoothing.get(), 0.0f, 0.95f);
	float dt = ofGetLastFrameTime();
	if (dt > 0.1f) dt = 0.1f;

	std::vector<bool> rawMatched(rawClusters.size(), false);

	// 给每个 tracked 找最近的 raw（greedy；按 mass 降序优先）
	std::vector<int> trackedOrder(trackedClusters.size());
	for (int i = 0; i < (int)trackedClusters.size(); i++) trackedOrder[i] = i;
	std::sort(trackedOrder.begin(), trackedOrder.end(),
		[this](int a, int b){ return trackedClusters[a].totalMass > trackedClusters[b].totalMass; });

	for (int ti : trackedOrder) {
		auto& tc = trackedClusters[ti];
		// 预测下一帧位置（基于速度）
		glm::vec3 predicted = tc.centroid + tc.velocity * dt;

		int bestRaw = -1;
		float bestDistSq = trackRSq;
		for (int ri = 0; ri < (int)rawClusters.size(); ri++) {
			if (rawMatched[ri]) continue;
			glm::vec3 d = rawClusters[ri].centroid - predicted;
			float ds = glm::dot(d, d);
			if (ds < bestDistSq) { bestDistSq = ds; bestRaw = ri; }
		}

		if (bestRaw >= 0) {
			const auto& rc = rawClusters[bestRaw];
			// 估计新速度 = (newPos - oldPos) / dt，平滑后
			glm::vec3 newVel = (rc.centroid - tc.centroid) / std::max(dt, 1e-4f);
			tc.velocity = glm::mix(tc.velocity, newVel, 0.3f);
			// centroid 用 EMA（smoothing 高 → 更稳定但反应慢）
			tc.centroid = glm::mix(rc.centroid, tc.centroid, smoothing);
			// 其他属性 lighter smoothing
			tc.totalMass = glm::mix(rc.totalMass, tc.totalMass, smoothing * 0.5f);
			tc.particleCount = rc.particleCount;
			tc.avgColor = ofFloatColor(
				glm::mix(rc.avgColor.r, tc.avgColor.r, smoothing * 0.5f),
				glm::mix(rc.avgColor.g, tc.avgColor.g, smoothing * 0.5f),
				glm::mix(rc.avgColor.b, tc.avgColor.b, smoothing * 0.5f),
				1.0f
			);
			tc.age++;
			tc.gracePeriod = 0;
			rawMatched[bestRaw] = true;
		} else {
			tc.gracePeriod++;
		}
	}

	// 没匹配的 raw → 新建 tracked
	for (int ri = 0; ri < (int)rawClusters.size(); ri++) {
		if (rawMatched[ri]) continue;
		const auto& rc = rawClusters[ri];
		TrackedCluster nt;
		nt.id            = nextClusterId++;
		nt.centroid      = rc.centroid;
		nt.velocity      = rc.velocity;
		nt.totalMass     = rc.totalMass;
		nt.particleCount = rc.particleCount;
		nt.avgColor      = rc.avgColor;
		nt.age           = 1;
		nt.gracePeriod   = 0;
		trackedClusters.push_back(nt);
	}

	// 超出 grace 的 tracked → 移除
	int maxGrace = clusterGracePeriod;
	trackedClusters.erase(
		std::remove_if(trackedClusters.begin(), trackedClusters.end(),
			[maxGrace](const TrackedCluster& t){ return t.gracePeriod > maxGrace; }),
		trackedClusters.end()
	);

	// ────────────────────────────────────────
	// C. 输出：只返回 age >= minAge 的 tracked（hysteresis）
	// ────────────────────────────────────────
	int minAge = clusterMinAge;
	std::vector<Cluster> output;
	output.reserve(trackedClusters.size());
	for (const auto& tc : trackedClusters) {
		if (tc.age < minAge) continue;       // 还在"暖机"，先不报告
		if (tc.gracePeriod > 0) {
			// 在 grace 中，age 维持但还在输出（让 drone 继续保持）
		}
		Cluster c;
		c.centroid      = tc.centroid;
		c.velocity      = tc.velocity;
		c.totalMass     = tc.totalMass;
		c.particleCount = tc.particleCount;
		c.avgColor      = tc.avgColor;
		output.push_back(c);
	}

	// 按 mass 排序，取 top-K
	std::sort(output.begin(), output.end(),
		[](const Cluster& a, const Cluster& b){ return a.totalMass > b.totalMass; });
	if ((int)output.size() > maxK) output.resize(maxK);
	return output;
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
	std::string vert = R"(
		#version 150
		uniform mat4 modelViewProjectionMatrix;
		uniform mat4 modelViewMatrix;
		in vec4 position;
		in vec4 color;
		in vec2 texcoord;

		out vec4 vColor;

		void main(){
			vec4 viewPos = modelViewMatrix * position;
			float dist   = max(1.0, length(viewPos.xyz));

			gl_Position  = modelViewProjectionMatrix * position;
			gl_PointSize = clamp(texcoord.x * 1000.0 / dist, 0.8, 96.0);

			float depthFade = clamp(1100.0 / dist, 0.10, 2.0);
			float alphaFade = clamp(900.0 / dist, 0.20, 1.5);
			vColor = vec4(color.rgb * depthFade, color.a * alphaFade);
		}
	)";

	std::string frag = R"(
		#version 150
		in  vec4 vColor;
		out vec4 fragColor;
		void main(){
			vec2 c = gl_PointCoord - vec2(0.5);
			float d = length(c);
			if (d > 0.5) discard;
			float alpha = smoothstep(0.5, 0.0, d);
			fragColor = vec4(vColor.rgb, vColor.a * alpha);
		}
	)";

	particleShader.setupShaderFromSource(GL_VERTEX_SHADER,   vert);
	particleShader.setupShaderFromSource(GL_FRAGMENT_SHADER, frag);
	particleShader.bindDefaults();
	particleShader.linkProgram();
}
