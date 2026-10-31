#pragma once
#include "base/ScriptAPI.h"

// Load for the performance benchmark (`--sample benchmark`, Tools/SmokeTestExport.ps1 -Sample benchmark):
// typical survivors work with a constant object count.

class BenchSpawner : public ScriptBase {
public:
	EXPOSE_FIELD(int, enemies, 300);
	EXPOSE_FIELD(int, bullets, 500);

	void Initialize() override;
	void Update(float) override {}
};

class BenchEnemy : public ScriptBase {
public:
	void Update(float deltaTime) override;
};

class BenchBullet : public ScriptBase {
public:
	void Update(float deltaTime) override;
};
