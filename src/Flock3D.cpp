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

	// ─── 关键：把碰撞事件抛给外部（ofApp / 音频模块）───
	CollisionEvent ev;
	ev.pos        = winner.pos;
	ev.newMass    = totalMass;
	ev.winnerSize = prevWinnerSize;
	ev.loserSize  = loserSize;
	ev.color      = winner.color;
	collisionsThisFrame.push_back(ev);

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

	// ─── 4. Fade tick ───
	for (auto& p : particles) {
		if (!p.alive) continue;
		if (p.fadeInTimer > 0) p.fadeInTimer--;

		if (p.fadeOutTimer == 0) {
			p.alive = false;
		} else if (p.fadeOutTimer > 0) {
			p.fadeOutTimer--;
			if (p.fadeOutTimer == 0) {
				p.alive = false;
			}
		}
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

	ofMesh mesh;
	mesh.setMode(OF_PRIMITIVE_POINTS);
	float fInFrames  = (float)fadeInFrames;
	float fOutFrames = (float)fadeOutFrames;

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

		mesh.addVertex(p.pos);

		ofFloatColor c = p.color;
		c.a *= fadeIn * fadeOut;
		mesh.addColor(c);
		mesh.addTexCoord(glm::vec2(p.size, 0.0f));
	}

	ofEnableBlendMode(OF_BLENDMODE_ADD);
	glEnable(GL_PROGRAM_POINT_SIZE);

	particleShader.begin();
	mesh.draw();
	particleShader.end();

	glDisable(GL_PROGRAM_POINT_SIZE);
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
