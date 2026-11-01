#include "EditorSuiteInternal.h"

#include "EngineUpdate.h"

GameObject::GameObject() = default;
GameObject::~GameObject() = default;

GameObject::GameObject(const GameObject& other) {
	id = other.id;
	name = other.name;
	isActive = other.isActive;
	isStatic = other.isStatic;
	tag = other.tag;
	layer = other.layer;
	collisionMask = other.collisionMask;
	prefabSource = other.prefabSource;
	prefabNodeIndex = other.prefabNodeIndex;
	type = other.type;
	parentIndex = other.parentIndex;
	parentId = other.parentId;
	childrenIds = other.childrenIds;
	translation = other.translation;
	rotation = other.rotation;
	scale = other.scale;
	rotationQuat = other.rotationQuat;
	worldMatrix = other.worldMatrix;
	hasMesh = other.hasMesh;
	meshEnabled = other.meshEnabled;
	meshType = other.meshType;
	materialColor = other.materialColor;
	textureHandle = other.textureHandle;
	isLit = other.isLit;
	lightingType = other.lightingType;
	shininess = other.shininess;
	specularColor = other.specularColor;
	useGeometryShader = other.useGeometryShader;
	geometryShaderEffect = other.geometryShaderEffect;
	explodeAmount = other.explodeAmount;
	castShadow = other.castShadow;
	receiveShadow = other.receiveShadow;
	texturePath = other.texturePath;
	modelPath = other.modelPath;
	uvOffset = other.uvOffset;
	uvTiling = other.uvTiling;
	uvRotation = other.uvRotation;
	outlineWidth = other.outlineWidth;
	outlineColor = other.outlineColor;
	animationIndex = other.animationIndex;
	animationSpeed = other.animationSpeed;
	animationLoop = other.animationLoop;
	animationPlaying = other.animationPlaying;
	hasBoxCollider = other.hasBoxCollider;
	boxColliderEnabled = other.boxColliderEnabled;
	isTrigger = other.isTrigger;
	providesContacts = other.providesContacts;
	boxCenter = other.boxCenter;
	boxSize = other.boxSize;
	isColliding = other.isColliding;
	hasSphereCollider = other.hasSphereCollider;
	sphereColliderEnabled = other.sphereColliderEnabled;
	sphereIsTrigger = other.sphereIsTrigger;
	sphereCenter = other.sphereCenter;
	sphereRadius = other.sphereRadius;
	hasCamera = other.hasCamera;
	cameraEnabled = other.cameraEnabled;
	fov = other.fov;
	nearClip = other.nearClip;
	farClip = other.farClip;
	isOrthographic = other.isOrthographic;
	hasLight = other.hasLight;
	lightEnabled = other.lightEnabled;
	lightType = other.lightType;
	lightColor = other.lightColor;
	lightIntensity = other.lightIntensity;
	lightDirection = other.lightDirection;
	lightRange = other.lightRange;
	lightDecay = other.lightDecay;
	lightSpotAngle = other.lightSpotAngle;
	lightSpotFalloffStart = other.lightSpotFalloffStart;
	lightCastShadow = other.lightCastShadow;
	lightShadowBias = other.lightShadowBias;
	lightShadowStrength = other.lightShadowStrength;
	lightShadowNormalBias = other.lightShadowNormalBias;
	hasParticleSystem = other.hasParticleSystem;
	attachToIndex = other.attachToIndex;
	attachedParentId = other.attachedParentId;
	localOffset = other.localOffset;
	particlePreset = other.particlePreset;
	particleShape = other.particleShape;
	particleSpawnRadius = other.particleSpawnRadius;
	particleBoxExtents = other.particleBoxExtents;
	particleConeAngle = other.particleConeAngle;
	particleSpawnRate = other.particleSpawnRate;
	particleMaxParticles = other.particleMaxParticles;
	particleBurstCount = other.particleBurstCount;
	particleEmitContinuously = other.particleEmitContinuously;
	particleDirection = other.particleDirection;
	particleSpreadAngle = other.particleSpreadAngle;
	particleSpeedMin = other.particleSpeedMin;
	particleSpeedMax = other.particleSpeedMax;
	particleGravity = other.particleGravity;
	particleDrag = other.particleDrag;
	particleWindEnabled = other.particleWindEnabled;
	particleWindAcceleration = other.particleWindAcceleration;
	particleLifetimeMin = other.particleLifetimeMin;
	particleLifetimeMax = other.particleLifetimeMax;
	particleStartScale = other.particleStartScale;
	particleEndScale = other.particleEndScale;
	particleStartColor = other.particleStartColor;
	particleEndColor = other.particleEndColor;
	particleBlendMode = other.particleBlendMode;
	particleBillboardMode = other.particleBillboardMode;
	particleQuad = nullptr;
	hasMapchip = other.hasMapchip;
	hasAudioSource = other.hasAudioSource;
	audioClipName = other.audioClipName;
	audioVolume = other.audioVolume;
	audioPlayOnAwake = other.audioPlayOnAwake;
	audioLoop = other.audioLoop;
	hasScript = other.hasScript;
	scriptClassName = other.scriptClassName;
	scripts = other.scripts;
	components = other.components;
	hasRigidbody = other.hasRigidbody;
	rbEnabled = other.rbEnabled;
	mass = other.mass;
	drag = other.drag;
	gravityScale = other.gravityScale;
	useGravity = other.useGravity;
	isKinematic = other.isKinematic;
	bounciness = other.bounciness;
	friction = other.friction;
	freezeRotation = other.freezeRotation;
	continuousCollision = other.continuousCollision;
	velocity = other.velocity;
	isGrounded = other.isGrounded;
}

GameObject::GameObject(GameObject&& other) noexcept = default;

GameObject& GameObject::operator=(const GameObject& other) {
	if (this != &other) {
		id = other.id;
		name = other.name;
		isActive = other.isActive;
		isStatic = other.isStatic;
		tag = other.tag;
		layer = other.layer;
		collisionMask = other.collisionMask;
		prefabSource = other.prefabSource;
		prefabNodeIndex = other.prefabNodeIndex;
		type = other.type;
		parentIndex = other.parentIndex;
		parentId = other.parentId;
		childrenIds = other.childrenIds;
		translation = other.translation;
		rotation = other.rotation;
		scale = other.scale;
		rotationQuat = other.rotationQuat;
		worldMatrix = other.worldMatrix;
		hasMesh = other.hasMesh;
		meshEnabled = other.meshEnabled;
		meshType = other.meshType;
		materialColor = other.materialColor;
		textureHandle = other.textureHandle;
		isLit = other.isLit;
		lightingType = other.lightingType;
		shininess = other.shininess;
		specularColor = other.specularColor;
		useGeometryShader = other.useGeometryShader;
		geometryShaderEffect = other.geometryShaderEffect;
		explodeAmount = other.explodeAmount;
		castShadow = other.castShadow;
		receiveShadow = other.receiveShadow;
		texturePath = other.texturePath;
		modelPath = other.modelPath;
		uvOffset = other.uvOffset;
		uvTiling = other.uvTiling;
		uvRotation = other.uvRotation;
		outlineWidth = other.outlineWidth;
		outlineColor = other.outlineColor;
		animationIndex = other.animationIndex;
		animationSpeed = other.animationSpeed;
		animationLoop = other.animationLoop;
		animationPlaying = other.animationPlaying;
		hasBoxCollider = other.hasBoxCollider;
		boxColliderEnabled = other.boxColliderEnabled;
		isTrigger = other.isTrigger;
		providesContacts = other.providesContacts;
		boxCenter = other.boxCenter;
		boxSize = other.boxSize;
		isColliding = other.isColliding;
		hasSphereCollider = other.hasSphereCollider;
		sphereColliderEnabled = other.sphereColliderEnabled;
		sphereIsTrigger = other.sphereIsTrigger;
		sphereCenter = other.sphereCenter;
		sphereRadius = other.sphereRadius;
		hasCamera = other.hasCamera;
		cameraEnabled = other.cameraEnabled;
		fov = other.fov;
		nearClip = other.nearClip;
		farClip = other.farClip;
		isOrthographic = other.isOrthographic;
		hasLight = other.hasLight;
		lightEnabled = other.lightEnabled;
		lightType = other.lightType;
		lightColor = other.lightColor;
		lightIntensity = other.lightIntensity;
		lightDirection = other.lightDirection;
		lightRange = other.lightRange;
		lightDecay = other.lightDecay;
		lightSpotAngle = other.lightSpotAngle;
		lightSpotFalloffStart = other.lightSpotFalloffStart;
		lightCastShadow = other.lightCastShadow;
		lightShadowBias = other.lightShadowBias;
		lightShadowStrength = other.lightShadowStrength;
		lightShadowNormalBias = other.lightShadowNormalBias;
		hasParticleSystem = other.hasParticleSystem;
		attachToIndex = other.attachToIndex;
		attachedParentId = other.attachedParentId;
		localOffset = other.localOffset;
		particlePreset = other.particlePreset;
		particleShape = other.particleShape;
		particleSpawnRadius = other.particleSpawnRadius;
		particleBoxExtents = other.particleBoxExtents;
		particleConeAngle = other.particleConeAngle;
		particleSpawnRate = other.particleSpawnRate;
		particleMaxParticles = other.particleMaxParticles;
		particleBurstCount = other.particleBurstCount;
		particleEmitContinuously = other.particleEmitContinuously;
		particleDirection = other.particleDirection;
		particleSpreadAngle = other.particleSpreadAngle;
		particleSpeedMin = other.particleSpeedMin;
		particleSpeedMax = other.particleSpeedMax;
		particleGravity = other.particleGravity;
		particleDrag = other.particleDrag;
		particleWindEnabled = other.particleWindEnabled;
		particleWindAcceleration = other.particleWindAcceleration;
		particleLifetimeMin = other.particleLifetimeMin;
		particleLifetimeMax = other.particleLifetimeMax;
		particleStartScale = other.particleStartScale;
		particleEndScale = other.particleEndScale;
		particleStartColor = other.particleStartColor;
		particleEndColor = other.particleEndColor;
		particleBlendMode = other.particleBlendMode;
		particleBillboardMode = other.particleBillboardMode;
		particleQuad = nullptr;
		hasMapchip = other.hasMapchip;
		hasAudioSource = other.hasAudioSource;
		audioClipName = other.audioClipName;
		audioVolume = other.audioVolume;
		audioPlayOnAwake = other.audioPlayOnAwake;
		audioLoop = other.audioLoop;
		hasScript = other.hasScript;
		scriptClassName = other.scriptClassName;
		scripts = other.scripts;
		components = other.components;
		hasRigidbody = other.hasRigidbody;
		rbEnabled = other.rbEnabled;
		mass = other.mass;
		drag = other.drag;
		gravityScale = other.gravityScale;
		useGravity = other.useGravity;
		isKinematic = other.isKinematic;
		bounciness = other.bounciness;
		friction = other.friction;
		freezeRotation = other.freezeRotation;
		continuousCollision = other.continuousCollision;
		velocity = other.velocity;
		isGrounded = other.isGrounded;
		editObject3d.reset();
		gameObject3d.reset();
		shadowTransform.reset();
		runtimeSoundHandle = 0;
		runtimeVoiceHandle = 0;
	}
	return *this;
}

GameObject& GameObject::operator=(GameObject&& other) noexcept = default;

EditorSuite::EditorSuite() = default;

std::filesystem::path EditorSuite::GetExecutableDirectory()
{
	char exePathBuf[MAX_PATH] = {};
	if (GetModuleFileNameA(NULL, exePathBuf, MAX_PATH) == 0) {
		return {};
	}
	return std::filesystem::path(exePathBuf).parent_path();
}

bool EditorSuite::IsExportedGameFolder(const std::filesystem::path& dir)
{
	std::error_code ec;
	return !dir.empty() && std::filesystem::is_regular_file(dir / kExportedGameConfigFile, ec);
}

bool EditorSuite::IsExportedGameLaunch()
{
	return IsExportedGameFolder(".") || IsExportedGameFolder(GetExecutableDirectory());
}

EditorSuite::~EditorSuite()
{
	// The handler captures `this`; the splash screen (TitleScene) can close without asking
	WinApp::GetInstance()->SetCloseRequestHandler(nullptr);
	WinApp::GetInstance()->SetFileDropHandler(nullptr);

	if (scriptBuildThread_.joinable()) {
		scriptBuildThread_.join();
	}
	DestroyAllScriptInstances();
	scriptModule_.Unload();
	Object3d::StaticSetShadowMap(nullptr);
	Object3d::StaticSetEnvironment({});
	Object3d::StaticSetLightGroup(nullptr);
}

void EditorSuite::Initialize(
	DirectXCommon* dxCommon,
	Camera* gameCamera,
	LightGroup* lights,
	ParticleQuad* particleSystem)
{
	RegisterBuiltinComponents();
	smokeFramesLeft_ = _wtoi(EngineUpdate::CommandLineValue(L"--smoke-frames").c_str());
#ifdef USE_IMGUI
	Localization::LoadPreference(); // editor UI language chosen in Options > Language
#endif
	dxCommon_ = dxCommon;
	gameCamera_ = gameCamera;
	lights_ = lights;
	particleSystem_ = particleSystem;

	if (lights_) {
		Object3d::StaticSetLightGroup(lights_);
	}

	// If launched from an exported standalone game directory containing game_build.cfg, align working directory
	{
		const std::filesystem::path exeDir = GetExecutableDirectory();
		if (!IsExportedGameFolder(".") && IsExportedGameFolder(exeDir)) {
			std::error_code ec;
			std::filesystem::current_path(exeDir, ec);
		}
	}

	// Ensure standard asset browser subfolders exist (an exported game ships only what it uses)
	if (!IsExportedGameFolder("."))
	{
		std::error_code ec;
		std::filesystem::create_directories("Resources/Texture", ec);
		std::filesystem::create_directories("Resources/Model", ec);
		std::filesystem::create_directories("Resources/Sound", ec);
		std::filesystem::create_directories("Resources/levels", ec);
		std::filesystem::create_directories("shaders", ec);

		if (std::filesystem::exists("Resources/circle.png") && !std::filesystem::exists("Resources/Texture/circle.png")) {
			std::filesystem::copy_file("Resources/circle.png", "Resources/Texture/circle.png", std::filesystem::copy_options::overwrite_existing, ec);
		}
		if (!std::filesystem::exists("Resources/Model/cube.obj")) {
			std::ofstream objFile("Resources/Model/cube.obj");
			if (objFile.is_open()) {
				objFile << "# Sample 3D Cube Mesh\n";
				objFile << "v -0.5 -0.5  0.5\nv  0.5 -0.5  0.5\nv  0.5  0.5  0.5\nv -0.5  0.5  0.5\n";
				objFile << "v -0.5 -0.5 -0.5\nv  0.5 -0.5 -0.5\nv  0.5  0.5 -0.5\nv -0.5  0.5 -0.5\n";
				objFile << "vn  0.0  0.0  1.0\nvn  0.0  0.0 -1.0\nvn  0.0  1.0  0.0\n";
				objFile << "vn  0.0 -1.0  0.0\nvn  1.0  0.0  0.0\nvn -1.0  0.0  0.0\n";
				objFile << "vt 0.0 0.0\nvt 1.0 0.0\nvt 1.0 1.0\nvt 0.0 1.0\n";
				objFile << "f 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n";
				objFile << "f 6/1/2 5/2/2 8/3/2\nf 6/1/2 8/3/2 7/4/2\n";
				objFile << "f 4/1/3 3/2/3 7/3/3\nf 4/1/3 7/3/3 8/4/3\n";
				objFile << "f 5/1/4 6/2/4 2/3/4\nf 5/1/4 2/3/4 1/4/4\n";
				objFile << "f 2/1/5 6/2/5 7/3/5\nf 2/1/5 7/3/5 3/4/5\n";
				objFile << "f 5/1/6 1/2/6 4/3/6\nf 5/1/6 4/3/6 8/4/6\n";
			}
		}
	}

	dxc_.Initialize();
	particleTextureHandle_ = TextureManager::GetInstance()->LoadTexture("Resources/circle.png");

	// Setup Editor Camera (Free navigation camera)
	editorCamera_.Initialize();
	editorCamera_.translation = { 0.0f, 3.5f, -10.0f };
	editorCamera_.rotation = { 0.25f, 0.0f, 0.0f };

	// Setup Offscreen RenderTargets for Scene, Game, and 3D Model Preview views
	const float editClearColor[4] = { 0.157f, 0.157f, 0.157f, 1.0f }; // Unity 6 background
	const float gameClearColor[4] = { 0.08f, 0.18f, 0.35f, 1.0f };
	const float previewClearColor[4] = { 0.14f, 0.15f, 0.18f, 1.0f };
	editViewRT_.Initialize(dxCommon_, 1920, 1080, 2, 1, 128, editClearColor);
	gameViewRT_.Initialize(dxCommon_, 1920, 1080, 3, 2, 129, gameClearColor);
	modelPreviewRT_.Initialize(dxCommon_, 256, 256, 4, 4, 132, previewClearColor);
	postProcess_.Initialize(dxCommon_, &dxc_);
#ifdef USE_IMGUI
	editSceneRT_.Initialize(dxCommon_, 1920, 1080, DirectXCommon::kSceneRtvBaseIndex, DirectXCommon::kSceneDsvBaseIndex, dxCommon_->AllocateSrvIndex(), editClearColor, true);
	previewSceneRT_.Initialize(dxCommon_, 256, 256, DirectXCommon::kSceneRtvBaseIndex + 2, DirectXCommon::kSceneDsvBaseIndex + 2, dxCommon_->AllocateSrvIndex(), previewClearColor, true);
	InitializeGameSceneTarget(1920, 1080);
#else
	InitializeGameSceneTarget(dxCommon_->GetClientWidth(), dxCommon_->GetClientHeight());
#endif

	previewCamera_.Initialize();
	previewCamera_.fovY = 0.45f;
	previewCamera_.aspectRatio = 1.0f;
	previewCamera_.nearClip = 0.05f;
	previewCamera_.farClip = 500.0f;
	previewCamera_.translation = { 0.0f, 0.0f, -4.5f };
	previewCamera_.rotation = { 0.0f, 0.0f, 0.0f };
	previewCamera_.Update();

	previewLights_ = LightGroup::Create();
	previewLights_->SetDefaultLighting();
	previewLights_->SetAmbientColor({ 0.42f, 0.42f, 0.46f });
	previewLights_->SetAmbientIntensity(0.80f);
	previewLights_->GetDirectionalLight().direction = { 0.35f, -0.65f, 0.65f };
	previewLights_->GetDirectionalLight().color = { 1.0f, 0.98f, 0.95f, 1.0f };
	previewLights_->GetDirectionalLight().intensity = 1.15f;
	previewLights_->SetShadowActive(0);
	previewLights_->SetCameraPosition(previewCamera_.translation);
	previewLights_->Update();

	previewObject3d_ = std::make_unique<Object3d>();
	previewObject3d_->Initialize();
	previewObject3d_->SetCamera(&previewCamera_);
	if (Material* mat = previewObject3d_->GetMaterial()) {
		mat->color = { 1.0f, 1.0f, 1.0f, 1.0f };
		mat->enableLighting = 1;
		mat->lightingType = 4;
		mat->shininess = 32.0f;
		mat->specularColor = { 0.6f, 0.6f, 0.6f };
		mat->receiveShadows = 0;
	}

	// Setup Directional Light Shadow Map (2048x2048 depth texture at DSV #3, SRV #130)
	shadowWorldTransform_.Initialize();
	shadowMap_.Initialize(dxCommon_, &dxc_, RizeEngine::GetGraphicPipeline()->GetRootSignature(), 3, 130);
	Object3d::StaticSetShadowMap(&shadowMap_);

	// Setup Skybox & Environment
	TextureManager::CreateProceduralSkyboxDDS("Resources/skybox.dds");
	skybox_.Initialize(dxCommon_, &dxc_);
	environmentProbe_.Initialize(dxCommon_, &dxc_);
	Object3d::StaticSetEnvironment(environmentProbe_.GetSrv());
	RefreshAvailableCubemaps();

	// Initialize Mapchip Grid
	mapGrid_.assign(mapRows_, std::vector<int>(mapCols_, 0));
	for (int c = 0; c < mapCols_; ++c) {
		mapGrid_[mapRows_ - 1][c] = 1; // Floor
	}

	// Initialize GameScripts DLL hot-reload module and EngineAPI table
	SetupEngineAPI();
	// An exported game may sit inside a project folder: it must load its own DLL, not look for a workspace
	if (IsExportedGameFolder(".")) scriptModule_.SetDllPath(std::filesystem::absolute(ScriptModule::kScriptsDllSubpath));
	scriptModule_.Initialize(&engineApi_);
	if (!scriptModule_.GetLastLoadError().empty()) {
		LogMessage("[Scripts] " + scriptModule_.GetLastLoadError(), 2);
	}
	{
		std::filesystem::path scriptsFolder = ResolveGameScriptsDir() / "Scripts";
		strncpy_s(scriptOutputDirBuf_, scriptsFolder.generic_string().c_str(), sizeof(scriptOutputDirBuf_) - 1);
	}

	// Initialize Default Scene GameObjects first as baseline
	NewScene();

	// Optional: open a scene at startup with `--scene <path>` (useful for automated visual checks)
	{
		const std::string cmdLine = GetCommandLineA();
		const size_t argPos = cmdLine.find("--scene ");
		if (argPos != std::string::npos) {
			std::string scenePath = cmdLine.substr(argPos + 8);
			scenePath.erase(0, scenePath.find_first_not_of(' '));
			if (!scenePath.empty() && scenePath.front() == '"') {
				scenePath = scenePath.substr(1, scenePath.find('"', 1) - 1);
			} else {
				scenePath = scenePath.substr(0, scenePath.find(' '));
			}
			if (!scenePath.empty() && std::filesystem::exists(scenePath)) {
				// Without an open project, use the folder that contains the scene's "Assets" directory
				// so prefabs, textures and save data resolve like they do inside the project
				if (currentProjectRoot_.empty()) {
					for (std::filesystem::path dir = std::filesystem::absolute(scenePath).parent_path(); dir.has_relative_path(); dir = dir.parent_path()) {
						if (dir.filename() == "Assets") {
							currentProjectRoot_ = dir.parent_path();
							break;
						}
					}
				}
				SyncScriptModuleWithProject();
				LoadScene(scenePath);
#ifdef USE_IMGUI
				// `--select <name>` / `--window animator|profiler|open-project`: open the editor in a given state (automated visual checks)
				auto argValue = [&](const char* flag) -> std::string {
					const size_t at = cmdLine.find(flag);
					if (at == std::string::npos) return {};
					std::string value = cmdLine.substr(at + std::strlen(flag));
					value.erase(0, value.find_first_not_of(' '));
					if (!value.empty() && value.front() == '"') return value.substr(1, value.find('"', 1) - 1);
					return value.substr(0, value.find(' '));
				};
				if (const std::string name = argValue("--select "); !name.empty()) {
					for (size_t i = 0; i < gameObjects_.size(); ++i) {
						if (gameObjects_[i].name == name) selectedObjectIndex_ = static_cast<int>(i);
					}
				}
				if (const std::string window = argValue("--window "); window == "animator") showAnimatorWindow_ = true;
				else if (window == "profiler") showProfiler_ = true;
				else if (window == "open-project") showOpenProjectModal_ = true;
#endif
				// `--play`: enter Play mode right away (automated visual checks of gameplay / game UI)
				if (cmdLine.find("--play") != std::string::npos) {
#ifdef USE_IMGUI
					ToggleStandalonePreview(); // starts Play in the full-window Game view, like F11
#else
					StartPlayMode();
#endif
				}
			} else {
				LogMessage(std::format("[EditorSuite] --scene: file not found: {}", scenePath), 1);
			}
		}
	}

#ifdef USE_IMGUI
	ApplyUnity6DarkTheme();

	// Optional: `--run-tests [filter]` runs the headless self-tests, then quits with the result as exit code
	{
		const std::string cmdLine = GetCommandLineA();
		const size_t argPos = cmdLine.find("--run-tests");
		if (argPos != std::string::npos) {
			std::string filter = cmdLine.substr(argPos + std::strlen("--run-tests"));
			filter.erase(0, filter.find_first_not_of(' '));
			if (!filter.empty() && filter.front() == '"') {
				const size_t closeQuote = filter.find('"', 1);
				filter = filter.substr(1, closeQuote == std::string::npos ? std::string::npos : closeQuote - 1);
			} else if (filter.rfind("--", 0) == 0) {
				filter.clear();
			} else {
				filter = filter.substr(0, filter.find(' '));
			}
			sSelfTestExitCode = (RunSelfTests(filter) == 0) ? 0 : 1;
			PostQuitMessage(0);
		}
	}

	// Automation (Tools/SmokeTestExport.ps1): `--new-project <folder> [--sample survivors]`, `--export <folder>`. Exit code 0 = done.
	{
		const std::filesystem::path newProject = EngineUpdate::CommandLineValue(L"--new-project");
		const std::filesystem::path exportDir = EngineUpdate::CommandLineValue(L"--export");
		if (!newProject.empty() || !exportDir.empty()) {
			bool ok = true;
			if (!newProject.empty()) {
				newProjectInitGit_ = false;
				ok = CreateNewProject(newProject.parent_path().string(), newProject.filename().string());
				if (!ok) LogMessage("[Automation] " + projectModalError_, 2);
				const std::wstring sample = EngineUpdate::CommandLineValue(L"--sample");
				if (ok && sample == L"survivors") ok = CreateSurvivorsDemo();
				if (ok && sample == L"benchmark") ok = CreateBenchmarkScene();
				if (ok && sample == L"thirdperson") ok = CreateThirdPersonDemo();
			}
			if (ok && !exportDir.empty()) {
				strncpy_s(buildOutputDirBuf_, exportDir.string().c_str(), sizeof(buildOutputDirBuf_) - 1);
				strncpy_s(buildStartupSceneBuf_, currentScenePath_.c_str(), sizeof(buildStartupSceneBuf_) - 1);
				strncpy_s(buildGameTitleBuf_, currentProjectRoot_.filename().string().c_str(), sizeof(buildGameTitleBuf_) - 1);
				ok = ExportStandaloneGame();
			}
			sSelfTestExitCode = ok ? 0 : 1;
			PostQuitMessage(0);
		}
	}
#endif

	// Check if running as a standalone build export via game_build.cfg
	if (IsExportedGameFolder(".")) {
		std::ifstream cfg(kExportedGameConfigFile);
		std::string line;
		std::string startupScene = "";
		std::string gameTitle = "";
		bool fullscreen = false;

		auto TrimStr = [](std::string s) {
			while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.erase(s.begin());
			while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
			return s;
		};

		while (std::getline(cfg, line)) {
			size_t eqPos = line.find('=');
			if (eqPos == std::string::npos) continue;
			std::string key = TrimStr(line.substr(0, eqPos));
			std::string val = TrimStr(line.substr(eqPos + 1));

			if (key == "standaloneGameMode" && (val == "true" || val == "1")) {
				isStandaloneGameMode_ = true;
			} else if (key == "startupScene") {
				startupScene = val;
			} else if (key == "gameTitle") {
				gameTitle = val;
			} else if (key == "fullscreen" && (val == "true" || val == "1")) {
				fullscreen = true;
			}
		}
		cfg.close();

		if (isStandaloneGameMode_) {
			HWND activeHwnd = GetActiveWindow();
			if (!gameTitle.empty() && activeHwnd) {
				SetWindowTextA(activeHwnd, gameTitle.c_str());
			}
			if (fullscreen && activeHwnd) {
				ShowWindow(activeHwnd, SW_MAXIMIZE);
			}
			// The game folder is its project root: input.json, GUIDs and save data resolve like in the editor
			currentProjectRoot_ = std::filesystem::absolute(".");
			if (startupScene.empty() || !std::filesystem::exists(startupScene)) {
				smokeError_ = "Startup scene not found: " + startupScene;
			} else if (!LoadScene(startupScene)) {
				smokeError_ = "Startup scene could not be loaded: " + startupScene;
			}
			StartPlayMode();
			LogMessage(std::format("[Standalone] Engine launched in Standalone Game Mode. Loaded {}", startupScene), 0);
			return;
		}
	}

	// The editor always works inside a project. When EditorSuite is re-created during this session
	// (Title <-> Editor transition) re-open the same project; otherwise show the Project Hub.
	{
		const std::string cmdLine = GetCommandLineA();
		const bool sceneFromCommandLine = cmdLine.find("--scene ") != std::string::npos;
		bool automatedRun = cmdLine.find("--test-scene-transitions") != std::string::npos;
#ifdef USE_IMGUI
		automatedRun = automatedRun || cmdLine.find("--run-tests") != std::string::npos;
#endif
		LoadRecentProjects(); // File > Open Recent and Open Project list it even when the hub is skipped
		if (!sSessionProjectRoot_.empty() && std::filesystem::exists(sSessionProjectRoot_)) {
			OpenProject(sSessionProjectRoot_.string());
		} else if (!sceneFromCommandLine && !automatedRun) {
			LoadRecentProjects();
			showProjectHub_ = true;
		}
	}

	// Ask before closing the window while the scene has unsaved changes
	WinApp::GetInstance()->SetCloseRequestHandler([this]() { return OnCloseRequested(); });
#ifdef USE_IMGUI
	// Files dragged from Explorer onto the editor are imported into the project (next Update, not inside WndProc)
	WinApp::GetInstance()->SetFileDropHandler([this](const std::vector<std::string>& paths) {
		pendingImports_.insert(pendingImports_.end(), paths.begin(), paths.end());
	});
#endif

	LogMessage("[EditorSuite] Initialized Unity 6 Dark Workspace & 5-Region Layout.", 0);
	LogMessage("[EditorSuite] Viewports Ready: # Scene (SRV #128) & Game (SRV #129).", 0);
}

void EditorSuite::Update(float deltaTime, Input* input)
{
	RIZE_PROFILE_CPU("Editor Update");
	currentInput_ = input;
	currentDeltaTime_ = deltaTime;
	unscaledDeltaTime_ = deltaTime;
	ProcessPendingImports(); // files dropped from Explorer
	if (smokeFramesLeft_ > 0) {
		RecordSmokeFrame();
		if (--smokeFramesLeft_ == 0) FinishSmokeRun();
	}

	// Record edits deferred during the previous frame (e.g. object creation) as one undo command
	if (undoPushPending_) {
		PushUndoSnapshot();
	}

	// Drain any MSBuild output lines produced by background script compilation
	DrainScriptBuildLogs();

	// Poll GameScripts/Scripts/*.cpp|*.h for file saves when Auto Build Scripts is enabled
	if (!IsStandaloneMode()) {
		PollScriptSourceWatcher(deltaTime);
#ifdef USE_IMGUI
		PollShaderWatcher(deltaTime);
#endif
	}

	// Poll bin/Scripts/GameScripts.dll for timestamp changes every 0.5s; hot-reload at a safe point between frames
	if (scriptModule_.PollDllChanged(deltaTime, isBuildingScripts_.load())) {
		HotReloadScripts();
	}

	ProcessPendingDeletes();

#ifdef USE_IMGUI
	ImGuiIO& io = ImGui::GetIO();

	// Global Hotkeys
	// No editor hotkeys while the Project Hub is up (e.g. Ctrl+S would save outside any project)
	if (!io.WantTextInput && !showProjectHub_) {
		if (ImGui::IsKeyPressed(ImGuiKey_F11)) {
			ToggleStandalonePreview();
		}
		if (isStandalonePreview_ && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
			ToggleStandalonePreview();
		}

		// Ctrl+P: toggle the Profiler window
		if (!IsStandaloneMode() && io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_P, false)) {
			showProfiler_ = !showProfiler_;
		}

		// Ctrl+Shift+B: Build Scripts (only when !io.WantTextInput)
		if (!IsStandaloneMode() && io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_B)) {
			TriggerScriptBuild();
		}

		// Undo / Redo (Disabled in Play mode)
		if (sceneState_ != EditorSceneState::Play && !IsStandaloneMode()) {
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) Undo();
			if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Y) || (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)))) Redo();
		}

		if (!IsStandaloneMode()) {
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) SaveScene(currentScenePath_);
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N)) NewScene();
			if (ImGui::IsKeyPressed(ImGuiKey_Delete)) DeleteSelectedGameObject();

			if (!isFlightNavigating_) {
				if (ImGui::IsKeyPressed(ImGuiKey_Q)) currentGizmo_ = GizmoType::Hand;
				if (ImGui::IsKeyPressed(ImGuiKey_W)) currentGizmo_ = GizmoType::Move;
				if (ImGui::IsKeyPressed(ImGuiKey_E)) currentGizmo_ = GizmoType::Rotate;
				if (ImGui::IsKeyPressed(ImGuiKey_R)) currentGizmo_ = GizmoType::Scale;
				if (ImGui::IsKeyPressed(ImGuiKey_T)) currentGizmo_ = GizmoType::Bounds;
				if (ImGui::IsKeyPressed(ImGuiKey_C)) isEditingColliderBounds_ = !isEditingColliderBounds_;
			}
		}
	}
#endif

	// Slowly rotate 3D model preview if active
	if (previewModel_) {
		previewRotationY_ += deltaTime * 1.1f;
		if (previewRotationY_ > 6.2831853f) {
			previewRotationY_ -= 6.2831853f;
		}
	}

	// 1. Particle System Parent Attachment & World-Space Simulation Update
	for (auto& obj : gameObjects_) {
		if (obj.isActive && obj.hasParticleSystem) {
			if (obj.attachedParentId != -1) {
				bool parentFound = false;
				for (const auto& parent : gameObjects_) {
					if (parent.id == static_cast<uint32_t>(obj.attachedParentId)) {
						obj.translation = parent.translation + obj.localOffset;
						parentFound = true;
						break;
					}
				}
				if (!parentFound) {
					// Fall back to None (World) safely if parent was deleted
					obj.attachedParentId = -1;
					obj.localOffset = { 0.0f, 0.0f, 0.0f };
				}
			}
			EnsureParticleQuadInitialized(obj);
			// Emit from the world position (hierarchy children: translation is local to the parent)
			const Vector3 emitterPos = (obj.parentId != -1)
				? Vector3{ obj.worldMatrix.m[3][0], obj.worldMatrix.m[3][1], obj.worldMatrix.m[3][2] }
				: obj.translation;
			if (obj.particleQuad) {
				obj.particleQuad->emitter.translate = emitterPos;
				obj.particleQuad->Simulate(deltaTime);
			}
			if (particleSystem_) {
				particleSystem_->emitter.translate = emitterPos;
			}
		}
	}

	// 2. 3D Tetris Game Loop
	if (isTetrisActive_) {
		UpdateTetrisGame(deltaTime, input);
	}

	// World matrices must be current before the camera / light sync below
	UpdateWorldMatrices();

	// 3. Authoritative Main Camera Sync to gameCamera_
	for (const auto& obj : gameObjects_) {
		if (obj.isActive && (obj.hasCamera || obj.type == GameObjectType::Camera)) {
			if (gameCamera_) {
				Vector3 worldPos{};
				Quaternion worldRot = Quaternion::Identity();
				Vector3 worldScale{};
				GetWorldTRS(obj, worldPos, worldRot, worldScale);
				gameCamera_->translation = worldPos + CameraShakeOffset();
				// Roots keep their exact Euler angles; children derive Euler from the world rotation
				gameCamera_->rotation = (obj.parentId == -1) ? obj.rotation : worldRot.ToEuler();
				gameCamera_->fovY = obj.fov * 3.14159265f / 180.0f;
				gameCamera_->nearClip = obj.nearClip;
				gameCamera_->farClip = obj.farClip;
				gameCamera_->Update();
			}
			break;
		}
	}

	// 4. Authoritative Punctual Lights Sync to lights_ (LightGroup)
	if (lights_) {
		bool hasDirectional = false;
		// Every light slot starts disabled each frame; only lights that exist in the scene re-enable one.
		// (The directional light used to stay on after its object was switched to Point/Spot, so the
		// sphere received a second highlight from the stale directional light.)
		lights_->GetDirectionalLight().active = 0;
		lights_->SetShadowActive(0);
		lights_->ClearLights();

		// Forward (+Z) of the object's world rotation: the same rotation the object/gizmo is drawn with.
		// (Building it from Euler X*Y*Z disagreed with Quaternion::FromEuler's Z-X-Y order when Z != 0.)
		auto worldForward = [](const GameObject& o) {
			Vector3 d = TransformUtil::TransformDirection({ 0.0f, 0.0f, 1.0f }, o.worldMatrix);
			const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
			if (len > 0.0001f) { d.x /= len; d.y /= len; d.z /= len; }
			return d;
		};

		for (const auto& obj : gameObjects_) {
			if (!obj.isActive || !obj.hasLight) continue;

			Vector3 worldPos{};
			Quaternion worldRot = Quaternion::Identity();
			Vector3 worldScale{};
			GetWorldTRS(obj, worldPos, worldRot, worldScale);

			if (obj.lightType == GameObject::LightType::Directional) {
				// One directional light (the first one in the hierarchy) drives the sun and the shadow map
				if (hasDirectional) continue;
				hasDirectional = true;

				// Direction follows the Transform rotation; the position of a directional light has no effect
				const Vector3 dir = worldForward(obj);
				const_cast<GameObject&>(obj).lightDirection = dir;
				lights_->GetDirectionalLight().direction = dir;
				lights_->GetDirectionalLight().color = { obj.lightColor.x, obj.lightColor.y, obj.lightColor.z, 1.0f };
				lights_->GetDirectionalLight().intensity = obj.lightIntensity;
				lights_->GetDirectionalLight().active = obj.lightEnabled ? 1 : 0;
				lights_->SetShadowActive((obj.lightEnabled && obj.lightCastShadow) ? 1 : 0);
				lights_->SetShadowBias(obj.lightShadowBias);
				lights_->SetShadowStrength(obj.lightShadowStrength);
				lights_->SetShadowNormalBias(obj.lightShadowNormalBias);
			} else if (obj.lightType == GameObject::LightType::Point) {
				PointLightData pl;
				pl.position = worldPos;
				pl.color = { obj.lightColor.x, obj.lightColor.y, obj.lightColor.z, 1.0f };
				pl.intensity = obj.lightIntensity;
				pl.radius = obj.lightRange;
				pl.decay = obj.lightDecay;
				if (obj.lightEnabled) lights_->AddPointLight(pl);
			} else if (obj.lightType == GameObject::LightType::Spot) {
				SpotLightData sl;
				sl.position = worldPos;

				// Cone direction follows the Transform rotation (local forward is +Z)
				const Vector3 fwd = worldForward(obj);
				sl.direction = fwd;
				const_cast<GameObject&>(obj).lightDirection = fwd;

				sl.color = { obj.lightColor.x, obj.lightColor.y, obj.lightColor.z, 1.0f };
				sl.intensity = obj.lightIntensity;
				sl.distance = obj.lightRange;
				sl.decay = obj.lightDecay;
				sl.cosAngle = std::cos(obj.lightSpotAngle * 3.14159265f / 180.0f);
				sl.cosFalloffStart = std::cos(obj.lightSpotFalloffStart * 3.14159265f / 180.0f);
				// In C++ enforce cosFalloffStart > cosAngle + 0.001f
				if (sl.cosFalloffStart <= sl.cosAngle + 0.001f) {
					sl.cosFalloffStart = sl.cosAngle + 0.001f;
				}
				if (obj.lightEnabled) lights_->AddSpotLight(sl);
			}
		}
		lights_->Update();
	}

	// 5. Script & Physics Simulation (Play mode only for Scripts; respects Pause and Step)
	const bool advancePlayFrame =
		(sceneState_ == EditorSceneState::Play) ||
		(sceneState_ == EditorSceneState::Pause && stepSingleFrame_) ||
		IsStandaloneMode();
	stepSingleFrame_ = false;

	// Game time: scripts, physics and animations see deltaTime * timeScale (0 = paused, e.g. a level-up menu)
	float gameDeltaTime = advancePlayFrame ? deltaTime * timeScale_ : deltaTime;
	if (advancePlayFrame) {
		// Camera shake and hitstop run on real time, so a hitstop can still shake the frozen frame
		if (shakeLeft_ > 0.0f) {
			shakeLeft_ = (std::max)(0.0f, shakeLeft_ - deltaTime);
			shakeTime_ += deltaTime;
		}
		if (hitstopLeft_ > 0.0f) {
			hitstopLeft_ -= deltaTime;
			gameDeltaTime = 0.0f;
		}
	}
	if (advancePlayFrame) {
		currentDeltaTime_ = gameDeltaTime;
		gameTime_ += gameDeltaTime;
		RIZE_PROFILE_CPU("Scripts");
		UpdateScripts(gameDeltaTime);
	}

	if (advancePlayFrame || (sceneState_ == EditorSceneState::Edit && simulatePhysicsInEditMode_)) {
		RIZE_PROFILE_CPU("Physics");
		StepPhysics(gameDeltaTime); // fixed 60 Hz steps, independent of the frame rate
	}

	// Skeletal animation runs on game time: scaled by timeScale, frozen while paused (Step advances one frame)
	{
		const bool inPlay = (sceneState_ != EditorSceneState::Edit) || IsStandaloneMode();
		const float animationDeltaTime = advancePlayFrame ? gameDeltaTime : (inPlay ? 0.0f : deltaTime);
		UpdateAnimations(animationDeltaTime, inPlay);
	}

	// Process any deferred GameObject spawns/deletions requested by scripts during Update/OnCollision
	ProcessPendingSpawns();
	if (!pendingDeletes_.empty()) {
		ProcessPendingDeletes();
	}

	// LoadScene() from a script: swap scenes between frames, never inside a script callback
	ProcessPendingSceneLoad();

#ifdef USE_IMGUI
	if (scriptFaultPending_) HandleScriptFault();
#endif
	UpdateCursorLock();

	// Action states of this frame, for IsActionTriggered / IsActionReleased next frame
	GetInputActions();
	inputActions_.EndFrame(InputSource());
}

void EditorSuite::LogMessage(const std::string& msg, int logType)
{
	LogEntry entry;
	entry.message = msg;
	entry.type = logType;

	if (logType == 0) infoCount_++;
	else if (logType == 1) warningCount_++;
	else if (logType == 2) errorCount_++;

	auto now = std::time(nullptr);
	tm ltm{};
	localtime_s(&ltm, &now);
	std::ostringstream ss;
	ss << std::put_time(&ltm, "%H:%M:%S");
	entry.timestamp = ss.str();
	entry.message = std::format("[{}] {}", entry.timestamp, msg);

	consoleLogs_.push_back(entry);
	if (consoleLogs_.size() > 500) {
		consoleLogs_.erase(consoleLogs_.begin());
	}
}

void EditorSuite::RefreshAssetBrowser()
{
	if (!std::filesystem::exists(currentDirectory_)) {
		currentDirectory_ = "Resources";
	}
}

void EditorSuite::DrawDirectoryTreeNode(const std::filesystem::path& dirPath)
{
	if (!std::filesystem::exists(dirPath)) return;
}

bool EditorSuite::HasActiveParticleSystem(Vector3* outEmitterPos) const
{
	for (const auto& obj : gameObjects_) {
		if (obj.isActive && obj.hasParticleSystem) {
			if (outEmitterPos) {
				*outEmitterPos = (obj.parentId != -1)
					? Vector3{ obj.worldMatrix.m[3][0], obj.worldMatrix.m[3][1], obj.worldMatrix.m[3][2] }
					: obj.translation;
			}
			return true;
		}
	}
	return false;
}

