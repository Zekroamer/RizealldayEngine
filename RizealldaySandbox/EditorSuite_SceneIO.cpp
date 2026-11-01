#include "EditorSuiteInternal.h"
#include "SceneJson.h"
#include <chrono>

using EditorSuiteDetail::SceneNameFromPath;
using EditorSuiteDetail::IsSceneFile;

namespace {
	// Writes `guidKey` next to an asset path when the asset is managed by the asset database (inside Assets/)
	void WriteAssetGuid(AssetDatabase& db, SceneJson::Json& o, const char* guidKey, const std::string& path)
	{
		if (path.empty() || path == "None") return;
		if (const std::string guid = db.GuidForPath(path); !guid.empty()) o[guidKey] = guid;
	}

	// Reads `pathKey`; a known `guidKey` wins, so a renamed or moved asset is still found
	void ReadAssetReference(const AssetDatabase& db, const SceneJson::Json& j, const char* pathKey, const char* guidKey, std::string& path)
	{
		SceneJson::Read(j, pathKey, path);
		std::string guid;
		SceneJson::Read(j, guidKey, guid);
		if (guid.empty()) return;
		if (const std::string current = db.PathForGuid(guid); !current.empty()) path = current;
	}
}

std::string EditorSuite::SerializeSceneToString() const
{
	using SceneJson::Json;
	using SceneJson::Num;
	using SceneJson::Arr;

	Json root = Json::object();
	root["sceneName"] = currentSceneName_;

	// Scene-level settings are written by WriteSceneSettingsJson (shared with the undo system)
	Json settings = Json::object();
	WriteSceneSettingsJson(&settings);
	root["skybox"] = std::move(settings["skybox"]);
	root["postProcess"] = std::move(settings["postProcess"]);

	Json objects = Json::array();
	for (const auto& obj : gameObjects_) {
		Json o = Json::object();
		WriteGameObjectJson(obj, &o);
		objects.push_back(std::move(o));
	}
	root["gameObjects"] = std::move(objects);
	root["mapchip"] = std::move(settings["mapchip"]);

	return SceneJson::DumpCompact(root);
}

// `outJson` is a SceneJson::Json* (kept opaque so EditorSuite.h does not include nlohmann/json)
void EditorSuite::WriteSceneSettingsJson(void* outJson) const
{
	using SceneJson::Json;
	using SceneJson::Num;
	using SceneJson::Arr;

	Json& root = *static_cast<Json*>(outJson);

	Json sky = Json::object();
	sky["mode"] = static_cast<int>(skybox_.GetMode());
	sky["topColor"] = Arr(skybox_.GetTopColor());
	sky["horizonColor"] = Arr(skybox_.GetHorizonColor());
	sky["bottomColor"] = Arr(skybox_.GetBottomColor());
	sky["solidColor"] = Arr(skybox_.GetSolidColor());
	sky["cubemapPath"] = skybox_.GetCubemapPath();
	WriteAssetGuid(AssetDb(), sky, "cubemapGuid", skybox_.GetCubemapPath());
	sky["rotation"] = Num(skybox_.GetRotation());
	sky["exposure"] = Num(skybox_.GetExposure());
	root["skybox"] = std::move(sky);

	Json post = Json::object();
	post["enabled"] = postSettings_.enabled;
	post["toneMapping"] = postSettings_.toneMapping;
	post["bloom"] = postSettings_.bloom;
	post["fxaa"] = postSettings_.fxaa;
	post["exposure"] = Num(postSettings_.exposure);
	post["bloomThreshold"] = Num(postSettings_.bloomThreshold);
	post["bloomIntensity"] = Num(postSettings_.bloomIntensity);
	post["contrast"] = Num(postSettings_.contrast);
	post["saturation"] = Num(postSettings_.saturation);
	post["tint"] = Arr(postSettings_.tint);
	post["vignette"] = Num(postSettings_.vignette);
	post["letterbox"] = Num(postSettings_.letterbox);
	post["ssao"] = postSettings_.ssao;
	post["ssaoRadius"] = Num(postSettings_.ssaoRadius);
	post["ssaoIntensity"] = Num(postSettings_.ssaoIntensity);
	post["shadowCascades"] = postSettings_.shadowCascades;
	post["shadowDistance"] = Num(postSettings_.shadowDistance);
	post["environmentIntensity"] = Num(postSettings_.environmentIntensity);
	root["postProcess"] = std::move(post);

	// Mapchip Data
	Json mapchip = Json::object();
	mapchip["rows"] = mapRows_;
	mapchip["cols"] = mapCols_;
	mapchip["tileSize"] = Num(mapTileSize_);
	Json data = Json::array();
	for (int r = 0; r < mapRows_; ++r) {
		Json row = Json::array();
		for (int c = 0; c < mapCols_; ++c) {
			const bool inRange = r < static_cast<int>(mapGrid_.size()) && c < static_cast<int>(mapGrid_[r].size());
			row.push_back(inRange ? mapGrid_[r][c] : 0);
		}
		data.push_back(std::move(row));
	}
	mapchip["data"] = std::move(data);
	root["mapchip"] = std::move(mapchip);
}

// `outJson` is a SceneJson::Json* receiving one GameObject entry of the scene file
void EditorSuite::WriteGameObjectJson(const GameObject& obj, void* outJson) const
{
	using SceneJson::Json;
	using SceneJson::Num;
	using SceneJson::Arr;

	Json& o = *static_cast<Json*>(outJson);
	o = Json::object();
	o["id"] = obj.id;
	o["name"] = obj.name;
	o["type"] = static_cast<int>(obj.type);
	o["isActive"] = obj.isActive;
	o["isStatic"] = obj.isStatic;
	o["tag"] = obj.tag;
	o["layer"] = obj.layer;
	o["collisionMask"] = obj.collisionMask;
	o["parentId"] = obj.parentId;
	o["translation"] = Arr(obj.translation);
	o["rotation"] = Arr(obj.rotation);
	o["rotationQuat"] = Arr(obj.rotationQuat);
	o["scale"] = Arr(obj.scale);

	// Mesh
	o["hasMesh"] = obj.hasMesh;
	o["meshEnabled"] = obj.meshEnabled;
	o["meshType"] = static_cast<int>(obj.meshType);
	o["materialColor"] = Arr(obj.materialColor);
	o["isLit"] = obj.isLit;
	o["lightingType"] = obj.lightingType;
	o["shininess"] = Num(obj.shininess);
	o["specularColor"] = Arr(obj.specularColor);
	o["castShadow"] = obj.castShadow;
	o["receiveShadow"] = obj.receiveShadow;
	o["useGeometryShader"] = obj.useGeometryShader;
	o["geometryShaderEffect"] = obj.geometryShaderEffect;
	o["explodeAmount"] = Num(obj.explodeAmount);
	o["texturePath"] = obj.texturePath;
	o["modelPath"] = obj.modelPath;
	WriteAssetGuid(AssetDb(), o, "textureGuid", obj.texturePath);
	WriteAssetGuid(AssetDb(), o, "modelGuid", obj.modelPath);
	o["uvOffset"] = Arr(obj.uvOffset);
	o["uvTiling"] = Arr(obj.uvTiling);
	o["uvRotation"] = Num(obj.uvRotation);
	if (obj.outlineWidth > 0.0f) {
		o["outlineWidth"] = Num(obj.outlineWidth);
		o["outlineColor"] = Arr(obj.outlineColor);
	}

	// Colliders
	o["hasBoxCollider"] = obj.hasBoxCollider;
	o["boxColliderEnabled"] = obj.boxColliderEnabled;
	o["isTrigger"] = obj.isTrigger;
	o["providesContacts"] = obj.providesContacts;
	o["boxCenter"] = Arr(obj.boxCenter);
	o["boxSize"] = Arr(obj.boxSize);
	o["hasSphereCollider"] = obj.hasSphereCollider;
	o["sphereColliderEnabled"] = obj.sphereColliderEnabled;
	o["sphereIsTrigger"] = obj.sphereIsTrigger;
	o["sphereCenter"] = Arr(obj.sphereCenter);
	o["sphereRadius"] = Num(obj.sphereRadius);

	// Rigidbody
	o["hasRigidbody"] = obj.hasRigidbody;
	o["rbEnabled"] = obj.rbEnabled;
	o["mass"] = Num(obj.mass);
	o["drag"] = Num(obj.drag);
	o["gravityScale"] = Num(obj.gravityScale);
	o["useGravity"] = obj.useGravity;
	o["isKinematic"] = obj.isKinematic;
	o["bounciness"] = Num(obj.bounciness);
	o["friction"] = Num(obj.friction);
	o["freezeRotation"] = obj.freezeRotation;
	o["continuousCollision"] = obj.continuousCollision;

	// Camera
	o["hasCamera"] = obj.hasCamera;
	o["cameraEnabled"] = obj.cameraEnabled;
	o["fov"] = Num(obj.fov);
	o["nearClip"] = Num(obj.nearClip);
	o["farClip"] = Num(obj.farClip);
	o["isOrthographic"] = obj.isOrthographic;

	// Light
	o["hasLight"] = obj.hasLight;
	o["lightEnabled"] = obj.lightEnabled;
	o["lightType"] = static_cast<int>(obj.lightType);
	o["lightColor"] = Arr(obj.lightColor);
	o["lightIntensity"] = Num(obj.lightIntensity);
	o["lightDirection"] = Arr(obj.lightDirection);
	o["lightRange"] = Num(obj.lightRange);
	o["lightDecay"] = Num(obj.lightDecay);
	o["lightSpotAngle"] = Num(obj.lightSpotAngle);
	o["lightSpotFalloffStart"] = Num(obj.lightSpotFalloffStart);
	o["lightCastShadow"] = obj.lightCastShadow;
	o["lightShadowBias"] = Num(obj.lightShadowBias);
	o["lightShadowStrength"] = Num(obj.lightShadowStrength);
	o["lightShadowNormalBias"] = Num(obj.lightShadowNormalBias);

	// Particle System
	o["hasParticleSystem"] = obj.hasParticleSystem;
	o["attachedParentId"] = obj.attachedParentId;
	o["localOffset"] = Arr(obj.localOffset);
	o["particlePreset"] = obj.particlePreset;
	o["particleShape"] = obj.particleShape;
	o["particleSpawnRadius"] = Num(obj.particleSpawnRadius);
	o["particleBoxExtents"] = Arr(obj.particleBoxExtents);
	o["particleConeAngle"] = Num(obj.particleConeAngle);
	o["particleSpawnRate"] = Num(obj.particleSpawnRate);
	o["particleMaxParticles"] = obj.particleMaxParticles;
	o["particleBurstCount"] = obj.particleBurstCount;
	o["particleEmitContinuously"] = obj.particleEmitContinuously;
	o["particleDirection"] = Arr(obj.particleDirection);
	o["particleSpreadAngle"] = Num(obj.particleSpreadAngle);
	o["particleSpeedMin"] = Num(obj.particleSpeedMin);
	o["particleSpeedMax"] = Num(obj.particleSpeedMax);
	o["particleGravity"] = Num(obj.particleGravity);
	o["particleDrag"] = Num(obj.particleDrag);
	o["particleWindEnabled"] = obj.particleWindEnabled;
	o["particleWindAcceleration"] = Arr(obj.particleWindAcceleration);
	o["particleLifetimeMin"] = Num(obj.particleLifetimeMin);
	o["particleLifetimeMax"] = Num(obj.particleLifetimeMax);
	o["particleStartScale"] = Num(obj.particleStartScale);
	o["particleEndScale"] = Num(obj.particleEndScale);
	o["particleStartColor"] = Arr(obj.particleStartColor);
	o["particleEndColor"] = Arr(obj.particleEndColor);
	o["particleBlendMode"] = obj.particleBlendMode;
	o["particleBillboardMode"] = obj.particleBillboardMode;

	// Mapchip, Audio, Animation & Scripts
	o["hasMapchip"] = obj.hasMapchip;
	o["hasAudioSource"] = obj.hasAudioSource;
	o["audioClipName"] = obj.audioClipName;
	WriteAssetGuid(AssetDb(), o, "audioClipGuid", obj.audioClipName);
	o["audioVolume"] = Num(obj.audioVolume);
	o["audioPlayOnAwake"] = obj.audioPlayOnAwake;
	o["audioLoop"] = obj.audioLoop;
	o["animationIndex"] = obj.animationIndex;
	o["animationSpeed"] = Num(obj.animationSpeed);
	o["animationLoop"] = obj.animationLoop;
	o["animationPlaying"] = obj.animationPlaying;
	o["hasScript"] = (!obj.scripts.empty() || obj.hasScript);
	o["scriptClassName"] = !obj.scripts.empty() ? obj.scripts.front().className : obj.scriptClassName;

	Json scriptNames = Json::array();
	for (const auto& sc : obj.scripts) {
		scriptNames.push_back(sc.className);
	}
	o["scripts"] = std::move(scriptNames);
	WriteDataComponentsJson(obj, &o); // "components" (registry data components)

	Json scriptComponents = Json::array();
	for (const auto& sc : obj.scripts) {
		// Live instance values override the saved snapshot
		std::map<std::string, GameObject::ScriptSavedFieldValue> fieldsToWrite = sc.savedFields;
		if (sc.instance) {
			int fCount = scriptModule_.GetFieldCount(sc.instance.get());
			for (int fIdx = 0; fIdx < fCount; ++fIdx) {
				ScriptFieldInfo info{};
				if (!scriptModule_.GetFieldInfo(sc.instance.get(), fIdx, info) || !info.dataPtr || !info.name || info.name[0] == '\0') continue;
				GameObject::ScriptSavedFieldValue val{};
				val.type = info.type;
				switch (info.type) {
				case ScriptFieldType::Float: val.floatVal = *static_cast<const float*>(info.dataPtr); break;
				case ScriptFieldType::Int: val.intVal = *static_cast<const int*>(info.dataPtr); break;
				case ScriptFieldType::Bool: val.boolVal = *static_cast<const bool*>(info.dataPtr); break;
				case ScriptFieldType::Vec3: val.vec3Val = *static_cast<const Vector3*>(info.dataPtr); break;
				case ScriptFieldType::CharBuffer: val.strVal = static_cast<const char*>(info.dataPtr); break;
				}
				fieldsToWrite[info.name] = val;
			}
		}

		Json fields = Json::array();
		for (const auto& [fName, fVal] : fieldsToWrite) {
			Json f = Json::object();
			f["name"] = fName;
			f["type"] = static_cast<int>(fVal.type);
			switch (fVal.type) {
			case ScriptFieldType::Float:
				f["v0"] = Num(fVal.floatVal);
				break;
			case ScriptFieldType::Int:
				f["v0"] = fVal.intVal;
				break;
			case ScriptFieldType::Bool:
				f["v0"] = fVal.boolVal ? 1 : 0;
				break;
			case ScriptFieldType::Vec3:
				f["v0"] = Num(fVal.vec3Val.x);
				f["v1"] = Num(fVal.vec3Val.y);
				f["v2"] = Num(fVal.vec3Val.z);
				break;
			case ScriptFieldType::CharBuffer:
				f["str"] = fVal.strVal;
				break;
			}
			fields.push_back(std::move(f));
		}

		Json comp = Json::object();
		comp["className"] = sc.className;
		comp["fields"] = std::move(fields);
		scriptComponents.push_back(std::move(comp));
	}
	o["scriptComponents"] = std::move(scriptComponents);
}

bool EditorSuite::DeserializeSceneFromString(const std::string& content)
{
	using SceneJson::Json;

	if (content.empty()) return false;

	// Parse first: a broken file must never wipe the scene that is currently open
	std::string parseError;
	const std::optional<Json> parsed = SceneJson::Parse(content, &parseError);
	if (!parsed) {
		LogMessage(std::format("[Scene] Failed to parse scene JSON: {}", parseError), 2);
		return false;
	}
	const Json& root = *parsed;
	if (!root.is_object()) {
		LogMessage("[Scene] Failed to parse scene JSON: root is not an object", 2);
		return false;
	}

	// Destroy any active script instances before clearing scene objects
	RefreshAllScriptObjectViews();
	const bool inPlay = (sceneState_ == EditorSceneState::Play || sceneState_ == EditorSceneState::Pause || IsStandaloneMode());
	for (auto& obj : gameObjects_) {
		for (auto& sc : obj.scripts) {
			if (sc.instance) {
				if (inPlay) {
					sc.instance->SetOwner(GetOrUpdateScriptViewById(obj.id), obj.id);
					sc.instance->SetEngineAPI(&engineApi_);
					sc.instance->SetHost(this, this);
					ScriptBase* instance = sc.instance.get();
					CallScript(instance, obj.id, "OnDestroy", [&] { instance->OnDestroy(); });
				}
				sc.instance.reset();
			}
		}
	}
	gameObjects_.clear();
	pendingSpawns_.clear();
	ResetPrefabPools();
	ResetScriptRuntimeState();
	UIRenderer::GetInstance()->Clear();

	// Read sceneName with fallback to SampleScene
	if (!SceneJson::Read(root, "sceneName", currentSceneName_)) {
		currentSceneName_ = "SampleScene";
	}

	ReadSceneSettingsJson(&root);

	// Parse individual GameObjects
	const Json* objectsJson = SceneJson::Find(root, "gameObjects");
	if (objectsJson && objectsJson->is_array()) {
		// Pass 1: claim the saved IDs so links (parentId / attachedParentId) survive the round trip.
		// 0, missing and duplicate IDs get a fresh one in pass 2.
		std::vector<uint32_t> savedIds(objectsJson->size(), 0);
		std::unordered_set<uint32_t> usedIds;
		uint32_t maxLoadedId = 0;
		for (size_t i = 0; i < objectsJson->size(); ++i) {
			const Json& oj = (*objectsJson)[i];
			if (!oj.is_object()) continue;
			uint32_t savedId = 0;
			SceneJson::Read(oj, "id", savedId);
			if (savedId != 0 && usedIds.insert(savedId).second) {
				savedIds[i] = savedId;
				maxLoadedId = (std::max)(maxLoadedId, savedId);
			}
		}
		nextObjectId_ = (std::max)(nextObjectId_, maxLoadedId + 1);

		// Pass 2: build the objects
		for (size_t objIndex = 0; objIndex < objectsJson->size(); ++objIndex) {
			const Json& j = (*objectsJson)[objIndex];
			if (!j.is_object()) continue;

			// Default-initialized GameObject guarantees safe fallback for any missing field
			GameObject obj;
			if (savedIds[objIndex] != 0) {
				obj.id = savedIds[objIndex];
			} else {
				obj.id = nextObjectId_++;
			}

			ReadGameObjectJson(&j, obj);

			gameObjects_.push_back(std::move(obj));
		}
	}

	// Sync parentIndex, childrenIds, and attachToIndex for loaded objects
	RebuildHierarchyLinks();
	UpdateWorldMatrices();
	for (size_t i = 0; i < gameObjects_.size(); ++i) {
		gameObjects_[i].attachToIndex = -1;
	}
	for (size_t i = 0; i < gameObjects_.size(); ++i) {
		if (gameObjects_[i].attachedParentId != -1) {
			for (size_t p = 0; p < gameObjects_.size(); ++p) {
				if (gameObjects_[p].id == static_cast<uint32_t>(gameObjects_[i].attachedParentId)) {
					gameObjects_[i].attachToIndex = static_cast<int>(p);
					break;
				}
			}
		}
	}


	EnsureEditModeScriptInstances();
	selectedObjectIndex_ = gameObjects_.empty() ? -1 : 0;
	return true;
}

// `json` is a const SceneJson::Json* holding the scene root (reads "skybox" and "mapchip" if present)
void EditorSuite::ReadSceneSettingsJson(const void* json)
{
	using SceneJson::Json;
	const Json& root = *static_cast<const Json*>(json);

	// Scenes saved before post-processing existed keep their look: no key = off
	postSettings_ = PostProcessSettings{};
	if (const Json* post = SceneJson::Find(root, "postProcess"); post && post->is_object()) {
		SceneJson::Read(*post, "enabled", postSettings_.enabled);
		SceneJson::Read(*post, "toneMapping", postSettings_.toneMapping);
		SceneJson::Read(*post, "bloom", postSettings_.bloom);
		SceneJson::Read(*post, "fxaa", postSettings_.fxaa);
		SceneJson::Read(*post, "exposure", postSettings_.exposure);
		SceneJson::Read(*post, "bloomThreshold", postSettings_.bloomThreshold);
		SceneJson::Read(*post, "bloomIntensity", postSettings_.bloomIntensity);
		SceneJson::Read(*post, "contrast", postSettings_.contrast);
		SceneJson::Read(*post, "saturation", postSettings_.saturation);
		SceneJson::Read(*post, "tint", postSettings_.tint);
		SceneJson::Read(*post, "vignette", postSettings_.vignette);
		SceneJson::Read(*post, "letterbox", postSettings_.letterbox);
		SceneJson::Read(*post, "ssao", postSettings_.ssao);
		SceneJson::Read(*post, "ssaoRadius", postSettings_.ssaoRadius);
		SceneJson::Read(*post, "ssaoIntensity", postSettings_.ssaoIntensity);
		SceneJson::Read(*post, "shadowCascades", postSettings_.shadowCascades);
		SceneJson::Read(*post, "shadowDistance", postSettings_.shadowDistance);
		SceneJson::Read(*post, "environmentIntensity", postSettings_.environmentIntensity);
	}

	// Skybox
	if (const Json* sky = SceneJson::Find(root, "skybox"); sky && sky->is_object()) {
		int modeInt = 0;
		SceneJson::Read(*sky, "mode", modeInt);
		skybox_.SetMode(static_cast<SkyboxMode>(modeInt));

		Vector4 top = skybox_.GetTopColor();
		SceneJson::Read(*sky, "topColor", top);
		skybox_.SetTopColor(top);

		Vector4 horiz = skybox_.GetHorizonColor();
		SceneJson::Read(*sky, "horizonColor", horiz);
		skybox_.SetHorizonColor(horiz);

		Vector4 bot = skybox_.GetBottomColor();
		SceneJson::Read(*sky, "bottomColor", bot);
		skybox_.SetBottomColor(bot);

		Vector4 solid = skybox_.GetSolidColor();
		SceneJson::Read(*sky, "solidColor", solid);
		skybox_.SetSolidColor(solid);

		std::string cubemap = skybox_.GetCubemapPath();
		ReadAssetReference(AssetDb(), *sky, "cubemapPath", "cubemapGuid", cubemap);
		skybox_.SetCubemapPath(cubemap);
		if (skybox_.GetMode() == SkyboxMode::Cubemap) {
			skybox_.LoadCubemap(cubemap);
		}

		float rot = skybox_.GetRotation();
		SceneJson::Read(*sky, "rotation", rot);
		skybox_.SetRotation(rot);

		float exp = skybox_.GetExposure();
		SceneJson::Read(*sky, "exposure", exp);
		skybox_.SetExposure(exp);
	}

	// Mapchip section
	if (const Json* mapJson = SceneJson::Find(root, "mapchip"); mapJson && mapJson->is_object()) {
		SceneJson::Read(*mapJson, "rows", mapRows_);
		SceneJson::Read(*mapJson, "cols", mapCols_);
		SceneJson::Read(*mapJson, "tileSize", mapTileSize_);
		mapRows_ = (std::max)(mapRows_, 0);
		mapCols_ = (std::max)(mapCols_, 0);

		const Json* dataJson = SceneJson::Find(*mapJson, "data");
		if (dataJson && dataJson->is_array()) {
			mapGrid_.assign(mapRows_, std::vector<int>(mapCols_, 0));
			for (int r = 0; r < mapRows_ && r < static_cast<int>(dataJson->size()); ++r) {
				const Json& rowJson = (*dataJson)[static_cast<size_t>(r)];
				if (!rowJson.is_array()) continue;
				for (int c = 0; c < mapCols_ && c < static_cast<int>(rowJson.size()); ++c) {
					const Json& cell = rowJson[static_cast<size_t>(c)];
					if (cell.is_number()) mapGrid_[r][c] = static_cast<int>(cell.get<double>());
				}
			}
		}
	}
}

// `json` is a const SceneJson::Json* for one GameObject entry. The caller assigns obj.id.
void EditorSuite::ReadGameObjectJson(const void* json, GameObject& obj)
{
	using SceneJson::Json;
	const Json& j = *static_cast<const Json*>(json);

	SceneJson::ReadEnum(j, "type", obj.type);

	SceneJson::Read(j, "name", obj.name);
	SceneJson::Read(j, "isActive", obj.isActive);
	SceneJson::Read(j, "isStatic", obj.isStatic);
	SceneJson::Read(j, "tag", obj.tag);
	SceneJson::Read(j, "layer", obj.layer);
	obj.layer &= 31u;
	SceneJson::Read(j, "collisionMask", obj.collisionMask);
	SceneJson::Read(j, "parentId", obj.parentId);

	// Transform
	SceneJson::Read(j, "translation", obj.translation);
	SceneJson::Read(j, "rotation", obj.rotation);
	SceneJson::Read(j, "scale", obj.scale);

	obj.rotationQuat = { 0.0f, 0.0f, 0.0f, 1.0f };
	SceneJson::Read(j, "rotationQuat", obj.rotationQuat);
	if (obj.rotationQuat.x == 0.0f && obj.rotationQuat.y == 0.0f && obj.rotationQuat.z == 0.0f && obj.rotationQuat.w == 0.0f) {
		obj.rotationQuat = Quaternion::FromEuler(obj.rotation);
	}

	// Mesh
	SceneJson::Read(j, "hasMesh", obj.hasMesh);
	SceneJson::Read(j, "meshEnabled", obj.meshEnabled);
	obj.meshType = static_cast<GameObject::MeshType>(0);
	SceneJson::ReadEnum(j, "meshType", obj.meshType);
	SceneJson::Read(j, "materialColor", obj.materialColor);
	SceneJson::Read(j, "isLit", obj.isLit);
	SceneJson::Read(j, "lightingType", obj.lightingType);
	SceneJson::Read(j, "shininess", obj.shininess);
	SceneJson::Read(j, "specularColor", obj.specularColor);
	SceneJson::Read(j, "castShadow", obj.castShadow);
	SceneJson::Read(j, "receiveShadow", obj.receiveShadow);
	SceneJson::Read(j, "useGeometryShader", obj.useGeometryShader);
	SceneJson::Read(j, "geometryShaderEffect", obj.geometryShaderEffect);
	SceneJson::Read(j, "explodeAmount", obj.explodeAmount);
	if (obj.geometryShaderEffect == 0 && obj.useGeometryShader) {
		obj.geometryShaderEffect = 1;
	}
	ReadAssetReference(AssetDb(), j, "texturePath", "textureGuid", obj.texturePath);
	if (!obj.texturePath.empty()) {
		obj.textureHandle = TextureManager::GetInstance()->LoadTexture(ResolveAssetPath(obj.texturePath));
	}
	ReadAssetReference(AssetDb(), j, "modelPath", "modelGuid", obj.modelPath);
	SceneJson::Read(j, "uvOffset", obj.uvOffset);
	SceneJson::Read(j, "uvTiling", obj.uvTiling);
	SceneJson::Read(j, "uvRotation", obj.uvRotation);
	SceneJson::Read(j, "outlineWidth", obj.outlineWidth);
	SceneJson::Read(j, "outlineColor", obj.outlineColor);

	// Box Collider
	SceneJson::Read(j, "hasBoxCollider", obj.hasBoxCollider);
	SceneJson::Read(j, "boxColliderEnabled", obj.boxColliderEnabled);
	SceneJson::Read(j, "isTrigger", obj.isTrigger);
	SceneJson::Read(j, "providesContacts", obj.providesContacts);
	SceneJson::Read(j, "boxCenter", obj.boxCenter);
	SceneJson::Read(j, "boxSize", obj.boxSize);

	// Sphere Collider
	SceneJson::Read(j, "hasSphereCollider", obj.hasSphereCollider);
	SceneJson::Read(j, "sphereColliderEnabled", obj.sphereColliderEnabled);
	SceneJson::Read(j, "sphereIsTrigger", obj.sphereIsTrigger);
	SceneJson::Read(j, "sphereCenter", obj.sphereCenter);
	SceneJson::Read(j, "sphereRadius", obj.sphereRadius);

	// Rigidbody
	SceneJson::Read(j, "hasRigidbody", obj.hasRigidbody);
	SceneJson::Read(j, "rbEnabled", obj.rbEnabled);
	SceneJson::Read(j, "mass", obj.mass);
	SceneJson::Read(j, "drag", obj.drag);
	SceneJson::Read(j, "gravityScale", obj.gravityScale);
	SceneJson::Read(j, "useGravity", obj.useGravity);
	SceneJson::Read(j, "isKinematic", obj.isKinematic);
	SceneJson::Read(j, "bounciness", obj.bounciness);
	// Scenes saved before Jolt keep the old behaviour: no friction, never rotated by physics
	obj.friction = 0.0f;
	obj.freezeRotation = true;
	SceneJson::Read(j, "friction", obj.friction);
	SceneJson::Read(j, "freezeRotation", obj.freezeRotation);
	SceneJson::Read(j, "continuousCollision", obj.continuousCollision);

	// Camera
	SceneJson::Read(j, "hasCamera", obj.hasCamera);
	SceneJson::Read(j, "cameraEnabled", obj.cameraEnabled);
	SceneJson::Read(j, "fov", obj.fov);
	SceneJson::Read(j, "nearClip", obj.nearClip);
	SceneJson::Read(j, "farClip", obj.farClip);
	SceneJson::Read(j, "isOrthographic", obj.isOrthographic);

	// Light
	SceneJson::Read(j, "hasLight", obj.hasLight);
	SceneJson::Read(j, "lightEnabled", obj.lightEnabled);
	obj.lightType = static_cast<GameObject::LightType>(0);
	SceneJson::ReadEnum(j, "lightType", obj.lightType);
	SceneJson::Read(j, "lightColor", obj.lightColor);
	SceneJson::Read(j, "lightIntensity", obj.lightIntensity);
	SceneJson::Read(j, "lightDirection", obj.lightDirection);
	SceneJson::Read(j, "lightRange", obj.lightRange);
	SceneJson::Read(j, "lightDecay", obj.lightDecay);
	SceneJson::Read(j, "lightSpotAngle", obj.lightSpotAngle);
	SceneJson::Read(j, "lightSpotFalloffStart", obj.lightSpotFalloffStart);
	SceneJson::Read(j, "lightCastShadow", obj.lightCastShadow);
	SceneJson::Read(j, "lightShadowBias", obj.lightShadowBias);
	SceneJson::Read(j, "lightShadowStrength", obj.lightShadowStrength);
	SceneJson::Read(j, "lightShadowNormalBias", obj.lightShadowNormalBias);
	if (obj.hasLight && (obj.lightType == GameObject::LightType::Directional || obj.lightType == GameObject::LightType::Spot)) {
		if (std::abs(obj.rotation.x) < 0.0001f && std::abs(obj.rotation.y) < 0.0001f && std::abs(obj.rotation.z) < 0.0001f) {
			Vector3 d = obj.lightDirection;
			float dLen = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
			if (dLen > 0.0001f) {
				d = { d.x / dLen, d.y / dLen, d.z / dLen };
				obj.rotation.x = std::asin(std::clamp(-d.y, -1.0f, 1.0f));
				obj.rotation.y = std::atan2(d.x, d.z);
				obj.rotation.z = 0.0f;
				obj.rotationQuat = Quaternion::FromEuler(obj.rotation);
			}
		}
	}

	// Particle System
	SceneJson::Read(j, "hasParticleSystem", obj.hasParticleSystem);
	SceneJson::Read(j, "attachedParentId", obj.attachedParentId);
	SceneJson::Read(j, "localOffset", obj.localOffset);
	SceneJson::Read(j, "particlePreset", obj.particlePreset);
	SceneJson::Read(j, "particleShape", obj.particleShape);
	SceneJson::Read(j, "particleSpawnRadius", obj.particleSpawnRadius);
	SceneJson::Read(j, "particleBoxExtents", obj.particleBoxExtents);
	SceneJson::Read(j, "particleConeAngle", obj.particleConeAngle);
	SceneJson::Read(j, "particleSpawnRate", obj.particleSpawnRate);
	SceneJson::Read(j, "particleMaxParticles", obj.particleMaxParticles);
	SceneJson::Read(j, "particleBurstCount", obj.particleBurstCount);
	SceneJson::Read(j, "particleEmitContinuously", obj.particleEmitContinuously);
	SceneJson::Read(j, "particleDirection", obj.particleDirection);
	SceneJson::Read(j, "particleSpreadAngle", obj.particleSpreadAngle);
	SceneJson::Read(j, "particleSpeedMin", obj.particleSpeedMin);
	SceneJson::Read(j, "particleSpeedMax", obj.particleSpeedMax);
	SceneJson::Read(j, "particleGravity", obj.particleGravity);
	SceneJson::Read(j, "particleDrag", obj.particleDrag);
	SceneJson::Read(j, "particleWindEnabled", obj.particleWindEnabled);
	SceneJson::Read(j, "particleWindAcceleration", obj.particleWindAcceleration);
	SceneJson::Read(j, "particleLifetimeMin", obj.particleLifetimeMin);
	SceneJson::Read(j, "particleLifetimeMax", obj.particleLifetimeMax);
	SceneJson::Read(j, "particleStartScale", obj.particleStartScale);
	SceneJson::Read(j, "particleEndScale", obj.particleEndScale);
	SceneJson::Read(j, "particleStartColor", obj.particleStartColor);
	SceneJson::Read(j, "particleEndColor", obj.particleEndColor);
	SceneJson::Read(j, "particleBlendMode", obj.particleBlendMode);
	SceneJson::Read(j, "particleBillboardMode", obj.particleBillboardMode);

	// Mapchip, Audio, Animation & Scripts
	SceneJson::Read(j, "hasMapchip", obj.hasMapchip);
	SceneJson::Read(j, "hasAudioSource", obj.hasAudioSource);
	ReadAssetReference(AssetDb(), j, "audioClipName", "audioClipGuid", obj.audioClipName);
	SceneJson::Read(j, "audioVolume", obj.audioVolume);
	SceneJson::Read(j, "audioPlayOnAwake", obj.audioPlayOnAwake);
	SceneJson::Read(j, "audioLoop", obj.audioLoop);
	SceneJson::Read(j, "animationIndex", obj.animationIndex);
	SceneJson::Read(j, "animationSpeed", obj.animationSpeed);
	SceneJson::Read(j, "animationLoop", obj.animationLoop);
	SceneJson::Read(j, "animationPlaying", obj.animationPlaying);
	SceneJson::Read(j, "hasScript", obj.hasScript);
	SceneJson::Read(j, "scriptClassName", obj.scriptClassName);
	ReadDataComponentsJson(json, obj);

	const Json* scComponents = SceneJson::Find(j, "scriptComponents");
	const bool parsedScriptComponents = (scComponents && scComponents->is_array());
	if (parsedScriptComponents) {
		for (const Json& cj : *scComponents) {
			if (!cj.is_object()) continue;
			std::string clsName;
			SceneJson::Read(cj, "className", clsName);
			if (clsName.empty()) continue;

			GameObject::ScriptComponent sc(clsName);
			sc.isMissingScript = !scriptModule_.IsRegistered(clsName);
			if (sc.isMissingScript) {
				LogMessage(std::format("[Script] Warning: Missing Script '{}' on GameObject '{}'. Preserving saved field data.", clsName, obj.name), 1);
			}

			const Json* fieldsJson = SceneJson::Find(cj, "fields");
			if (fieldsJson && fieldsJson->is_array()) {
				for (const Json& fj : *fieldsJson) {
					if (!fj.is_object()) continue;
					std::string fName;
					if (!SceneJson::Read(fj, "name", fName)) continue;

					int fTypeInt = 0;
					SceneJson::Read(fj, "type", fTypeInt);
					GameObject::ScriptSavedFieldValue fVal{};
					fVal.type = static_cast<ScriptFieldType>(fTypeInt);
					switch (fVal.type) {
					case ScriptFieldType::Float:
						SceneJson::Read(fj, "v0", fVal.floatVal);
						break;
					case ScriptFieldType::Int:
						SceneJson::Read(fj, "v0", fVal.intVal);
						break;
					case ScriptFieldType::Bool:
						SceneJson::Read(fj, "v0", fVal.boolVal);
						break;
					case ScriptFieldType::Vec3:
						SceneJson::Read(fj, "v0", fVal.vec3Val.x);
						SceneJson::Read(fj, "v1", fVal.vec3Val.y);
						SceneJson::Read(fj, "v2", fVal.vec3Val.z);
						break;
					case ScriptFieldType::CharBuffer:
						SceneJson::Read(fj, "str", fVal.strVal);
						break;
					}
					sc.savedFields[fName] = fVal;
				}
			}
			obj.scripts.push_back(std::move(sc));
		}
	}

	if (!parsedScriptComponents) {
		const Json* scriptsJson = SceneJson::Find(j, "scripts");
		if (scriptsJson && scriptsJson->is_array()) {
			for (const Json& nameJson : *scriptsJson) {
				if (!nameJson.is_string()) continue;
				const std::string clsName = nameJson.get<std::string>();
				if (clsName.empty()) continue;
				GameObject::ScriptComponent sc(clsName);
				sc.isMissingScript = !scriptModule_.IsRegistered(clsName);
				if (sc.isMissingScript) {
					LogMessage(std::format("[Script] Warning: Missing Script '{}' on GameObject '{}'. Preserving script name on save.", clsName, obj.name), 1);
				}
				obj.scripts.push_back(std::move(sc));
			}
		} else if (obj.hasScript && !obj.scriptClassName.empty()) {
			GameObject::ScriptComponent sc(obj.scriptClassName);
			sc.isMissingScript = !scriptModule_.IsRegistered(obj.scriptClassName);
			if (sc.isMissingScript) {
				LogMessage(std::format("[Script] Warning: Missing Script '{}' on GameObject '{}'. Preserving script name on save.", obj.scriptClassName, obj.name), 1);
			}
			obj.scripts.push_back(std::move(sc));
		}
	}
	obj.hasScript = !obj.scripts.empty();
	if (!obj.scripts.empty()) {
		obj.scriptClassName = obj.scripts.front().className;
	}

	if (obj.hasParticleSystem) {
		EnsureParticleQuadInitialized(obj);
	}
}

bool EditorSuite::SaveScene(const std::string& filePath)
{
	std::string targetPath = filePath.empty() ? currentScenePath_ : filePath;
	if (targetPath.empty()) {
		showSaveSceneAsModal_ = true;
		return false;
	}

	std::filesystem::path p(targetPath);
	if (p.has_parent_path()) {
		std::filesystem::create_directories(p.parent_path());
	}

	std::ofstream file(targetPath);
	if (!file.is_open()) return false;

	file << SerializeSceneToString();
	file.close();

	isSceneDirty_ = false;
	currentScenePath_ = targetPath;
	currentSceneName_ = SceneNameFromPath(p);
	LogMessage(std::format("[Scene] Saved scene to: {}", targetPath), 0);
	return true;
}

bool EditorSuite::LoadScene(const std::string& filePath)
{
	if (sceneState_ != EditorSceneState::Edit && !isStandaloneGameMode_) {
		StopPlayMode();
	}

	std::ifstream file(filePath);
	if (!file.is_open()) return false;

	std::stringstream buffer;
	buffer << file.rdbuf();
	file.close();

	if (!DeserializeSceneFromString(buffer.str())) return false;

	currentScenePath_ = filePath;
	currentSceneName_ = SceneNameFromPath(filePath);
	isSceneDirty_ = false;

	ResetUndoHistory();
	isSceneDirty_ = false;

	LogMessage(std::format("[Scene] Loaded scene from: {} ({} objects)", filePath, gameObjects_.size()), 0);
	return true;
}

void EditorSuite::NewScene()
{
	if (sceneState_ != EditorSceneState::Edit) {
		StopPlayMode();
	}
	pendingDeletes_.clear();
	pendingSpawns_.clear();
	ResetPrefabPools();
	ResetScriptRuntimeState();
	UIRenderer::GetInstance()->Clear();
	pendingDuplicateIndex_ = -1;
	gameObjects_.clear();
	currentSceneName_ = "SampleScene";
	currentScenePath_ = (GetScenesDirectory() / "SampleScene.scene.json").string();
	isSceneDirty_ = false;

	skybox_.SetMode(SkyboxMode::Gradient);
	skybox_.SetTopColor({ 0.22f, 0.45f, 0.78f, 1.0f });
	skybox_.SetHorizonColor({ 0.72f, 0.82f, 0.92f, 1.0f });
	skybox_.SetBottomColor({ 0.35f, 0.37f, 0.40f, 1.0f });
	skybox_.SetSolidColor({ 0.157f, 0.157f, 0.157f, 1.0f });
	skybox_.SetCubemapPath("Resources/skybox.dds");
	skybox_.SetRotation(0.0f);
	skybox_.SetExposure(1.0f);

	postSettings_ = PostProcessSettings{};
	postSettings_.enabled = true; // new scenes start with the filmic look
	postSettings_.ssao = true;
	postSettings_.shadowCascades = 3;

	// 1. Main Camera
	{
		auto& cam = CreateGameObject("Main Camera", GameObjectType::Camera);
		cam.tag = "MainCamera";
		cam.translation = gameCamera_ ? gameCamera_->translation : Vector3{ 0.0f, 1.5f, -8.0f };
		cam.rotation = gameCamera_ ? gameCamera_->rotation : Vector3{ 0.15f, 0.0f, 0.0f };
		cam.hasCamera = true;
		cam.fov = 45.0f;
		cam.nearClip = 0.1f;
		cam.farClip = 1000.0f;
	}

	// 2. Directional Light
	{
		auto& light = CreateGameObject("Directional Light", GameObjectType::DirectionalLight);
		light.translation = { 0.0f, 10.0f, 0.0f };
		light.hasLight = true;
		light.lightType = GameObject::LightType::Directional;
		light.lightDirection = { 0.5f, -1.0f, 0.5f };
		{
			Vector3 d = light.lightDirection;
			float dLen = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
			if (dLen > 0.0001f) { d.x /= dLen; d.y /= dLen; d.z /= dLen; }
			light.rotation = { std::asin(std::clamp(-d.y, -1.0f, 1.0f)), std::atan2(d.x, d.z), 0.0f };
			light.rotationQuat = Quaternion::FromEuler(light.rotation);
		}
		light.lightColor = { 1.0f, 0.95f, 0.9f };
		light.lightIntensity = 1.0f;
		light.lightCastShadow = true;
		light.lightShadowBias = 0.002f;
		light.lightShadowStrength = 1.0f;
		light.lightShadowNormalBias = 0.02f;
	}

	// 3. Ground Plane
	{
		auto& plane = CreatePlane("Ground Plane", { 0.0f, -0.5f, 0.0f });
		plane.scale = { 20.0f, 1.0f, 20.0f };
		plane.isStatic = true;
		plane.materialColor = { 0.25f, 0.27f, 0.30f, 1.0f };
		plane.castShadow = false;
		plane.receiveShadow = true;
	}

	selectedObjectIndex_ = 2; // Ground Plane
	ResetUndoHistory();
	isSceneDirty_ = false;

	LogMessage("[Scene] Created Clean Default Scene (SampleScene).", 0);
}

void EditorSuite::DiscardUnsavedChanges()
{
	if (std::filesystem::exists(currentScenePath_)) {
		LoadScene(currentScenePath_);
	} else {
		NewScene();
	}
	LogMessage("[Scene] Discarded unsaved changes.", 0);
}

// ---------------------------------------------------------------------------
// Command-based undo
// ---------------------------------------------------------------------------

namespace {
	std::string DumpForUndo(const SceneJson::Json& j)
	{
		// Same UTF-8 tolerance as scene files (ANSI paths must not throw)
		return j.dump(-1, ' ', false, SceneJson::Json::error_handler_t::replace);
	}

	double NowSeconds()
	{
		using namespace std::chrono;
		return duration<double>(steady_clock::now().time_since_epoch()).count();
	}
}

EditorSuite::UndoSceneState EditorSuite::CaptureUndoState() const
{
	using SceneJson::Json;
	UndoSceneState state;
	state.order.reserve(gameObjects_.size());
	state.objects.reserve(gameObjects_.size());
	for (const auto& obj : gameObjects_) {
		Json o = Json::object();
		WriteGameObjectJson(obj, &o);
		state.order.push_back(obj.id);
		state.objects[obj.id] = DumpForUndo(o);
	}
	Json settings = Json::object();
	WriteSceneSettingsJson(&settings);
	state.settings = DumpForUndo(settings);
	return state;
}

void EditorSuite::PushUndoSnapshot()
{
	undoPushPending_ = false;

	// Play-mode changes are reverted by the play snapshot, so they never enter the history
	if (sceneState_ != EditorSceneState::Edit || IsStandaloneMode()) {
		return;
	}

	UndoSceneState current = CaptureUndoState();

	std::unordered_map<uint32_t, int> indexBefore;
	std::unordered_map<uint32_t, int> indexAfter;
	for (size_t i = 0; i < undoBaseline_.order.size(); ++i) indexBefore[undoBaseline_.order[i]] = static_cast<int>(i);
	for (size_t i = 0; i < current.order.size(); ++i) indexAfter[current.order[i]] = static_cast<int>(i);

	UndoCommand cmd;
	// Deleted objects
	for (uint32_t id : undoBaseline_.order) {
		if (current.objects.count(id) == 0) {
			cmd.changes.push_back({ id, indexBefore[id], -1, undoBaseline_.objects[id], std::string() });
		}
	}
	// Created / modified / moved objects
	for (uint32_t id : current.order) {
		auto it = undoBaseline_.objects.find(id);
		if (it == undoBaseline_.objects.end()) {
			cmd.changes.push_back({ id, -1, indexAfter[id], std::string(), current.objects[id] });
		} else if (it->second != current.objects[id] || indexBefore[id] != indexAfter[id]) {
			cmd.changes.push_back({ id, indexBefore[id], indexAfter[id], it->second, current.objects[id] });
		}
	}
	if (current.settings != undoBaseline_.settings) {
		cmd.settingsBefore = undoBaseline_.settings;
		cmd.settingsAfter = current.settings;
	}
	if (cmd.changes.empty() && cmd.settingsBefore.empty()) {
		return; // nothing changed
	}
	cmd.timeSeconds = NowSeconds();

	// Merge continuous edits of the same objects (slider/gizmo drags call this every frame)
	bool merged = false;
	if (undoMergeAllowed_ && !undoCommands_.empty()) {
		UndoCommand& prev = undoCommands_.back();
		auto onlyModifications = [](const UndoCommand& c) {
			for (const auto& ch : c.changes) {
				if (ch.before.empty() || ch.after.empty()) return false;
			}
			return true;
		};
		bool sameObjects = prev.changes.size() == cmd.changes.size();
		for (size_t i = 0; sameObjects && i < cmd.changes.size(); ++i) {
			sameObjects = prev.changes[i].id == cmd.changes[i].id;
		}
		const bool sameSettingsShape = prev.settingsBefore.empty() == cmd.settingsBefore.empty();
		if (sameObjects && sameSettingsShape && onlyModifications(prev) && onlyModifications(cmd) &&
			cmd.timeSeconds - prev.timeSeconds < kUndoMergeWindowSeconds) {
			for (size_t i = 0; i < cmd.changes.size(); ++i) {
				prev.changes[i].after = std::move(cmd.changes[i].after);
				prev.changes[i].indexAfter = cmd.changes[i].indexAfter;
			}
			if (!cmd.settingsAfter.empty()) prev.settingsAfter = std::move(cmd.settingsAfter);
			prev.timeSeconds = cmd.timeSeconds;
			merged = true;
		}
	}
	if (!merged) {
		undoCommands_.push_back(std::move(cmd));
		if (undoCommands_.size() > kMaxUndoCommands) {
			undoCommands_.erase(undoCommands_.begin());
		}
	}

	redoCommands_.clear();
	undoBaseline_ = std::move(current);
	undoMergeAllowed_ = true;
}

void EditorSuite::ApplyUndoCommand(const UndoCommand& cmd, bool undo)
{
	using SceneJson::Json;

	const uint32_t selectedId = (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < static_cast<int>(gameObjects_.size()))
		? gameObjects_[selectedObjectIndex_].id : 0;

	// Indices queued for deletion/duplication would be stale after the patch
	pendingDeletes_.clear();
	pendingDuplicateIndex_ = -1;

	auto findIndex = [&](uint32_t id) -> int {
		for (size_t i = 0; i < gameObjects_.size(); ++i) {
			if (gameObjects_[i].id == id) return static_cast<int>(i);
		}
		return -1;
	};

	// 1. Remove every object touched by the command; the ones that exist on the target side are re-inserted below
	for (const auto& ch : cmd.changes) {
		const int idx = findIndex(ch.id);
		if (idx >= 0) {
			gameObjects_.erase(gameObjects_.begin() + idx);
		}
	}

	// 2. Re-insert target-side objects in ascending target index so they land at their recorded positions
	std::vector<const UndoObjectChange*> inserts;
	for (const auto& ch : cmd.changes) {
		if (!(undo ? ch.before : ch.after).empty()) inserts.push_back(&ch);
	}
	std::sort(inserts.begin(), inserts.end(), [undo](const UndoObjectChange* a, const UndoObjectChange* b) {
		return (undo ? a->indexBefore : a->indexAfter) < (undo ? b->indexBefore : b->indexAfter);
	});
	for (const UndoObjectChange* ch : inserts) {
		const std::string& text = undo ? ch->before : ch->after;
		std::optional<Json> parsed = SceneJson::Parse(text);
		if (!parsed || !parsed->is_object()) {
			LogMessage(std::format("[Undo] Could not restore GameObject {} (invalid snapshot).", ch->id), 2);
			continue;
		}
		GameObject obj;
		obj.id = ch->id;
		ReadGameObjectJson(&*parsed, obj);
		obj.id = ch->id;
		nextObjectId_ = (std::max)(nextObjectId_, ch->id + 1);

		int target = undo ? ch->indexBefore : ch->indexAfter;
		target = std::clamp(target, 0, static_cast<int>(gameObjects_.size()));
		gameObjects_.insert(gameObjects_.begin() + target, std::move(obj));
	}

	// 3. Scene settings
	const std::string& settingsText = undo ? cmd.settingsBefore : cmd.settingsAfter;
	if (!settingsText.empty()) {
		if (std::optional<Json> settings = SceneJson::Parse(settingsText)) {
			ReadSceneSettingsJson(&*settings);
		}
	}

	// 4. Re-derive caches that depend on indices / links
	RebuildHierarchyLinks();
	for (auto& obj : gameObjects_) {
		obj.attachToIndex = (obj.attachedParentId != -1) ? findIndex(static_cast<uint32_t>(obj.attachedParentId)) : -1;
	}
	UpdateWorldMatrices();
	EnsureEditModeScriptInstances();
	RefreshAllScriptObjectViews();

	selectedObjectIndex_ = selectedId ? findIndex(selectedId) : -1;

	undoBaseline_ = CaptureUndoState();
	isSceneDirty_ = true;
	undoMergeAllowed_ = false;
	undoPushPending_ = false;
}

void EditorSuite::Undo()
{
#ifdef USE_IMGUI
	ImGuiIO& io = ImGui::GetIO();
	if (io.WantTextInput) return; // Must not run while text field is active
#endif
	if (sceneState_ != EditorSceneState::Edit) return; // Disabled in Play / Pause

	// Record a pending (not yet pushed) edit first so it can be undone too
	PushUndoSnapshot();
	if (undoCommands_.empty()) return;

	UndoCommand cmd = std::move(undoCommands_.back());
	undoCommands_.pop_back();
	ApplyUndoCommand(cmd, true);
	LogMessage(std::format("[Undo] Reverted {} object change(s).", cmd.changes.size()), 0);
	redoCommands_.push_back(std::move(cmd));
}

void EditorSuite::Redo()
{
#ifdef USE_IMGUI
	ImGuiIO& io = ImGui::GetIO();
	if (io.WantTextInput) return;
#endif
	if (sceneState_ != EditorSceneState::Edit) return;
	if (redoCommands_.empty()) return;

	UndoCommand cmd = std::move(redoCommands_.back());
	redoCommands_.pop_back();
	ApplyUndoCommand(cmd, false);
	LogMessage(std::format("[Redo] Re-applied {} object change(s).", cmd.changes.size()), 0);
	undoCommands_.push_back(std::move(cmd));
}

void EditorSuite::ResetUndoHistory()
{
	undoCommands_.clear();
	redoCommands_.clear();
	undoBaseline_ = CaptureUndoState();
	undoMergeAllowed_ = false;
	undoPushPending_ = false;
}
