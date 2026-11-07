#include "EditorSuiteInternal.h"
#include <cfloat>

void EditorSuite::CalculateLightViewProjection()
{
	Vector3 lightDir = { 0.5f, -1.0f, 0.5f };
	for (const auto& obj : gameObjects_) {
		if (obj.isActive && obj.hasLight && obj.lightEnabled && obj.lightType == GameObject::LightType::Directional) {
			lightDir = obj.lightDirection;
			break;
		}
	}
	float len = std::sqrt(lightDir.x * lightDir.x + lightDir.y * lightDir.y + lightDir.z * lightDir.z);
	if (len > 0.0001f) {
		lightDir = { lightDir.x / len, lightDir.y / len, lightDir.z / len };
	} else {
		lightDir = { 0.0f, -1.0f, 0.0f };
	}

	Vector3 minBound = { 1e9f, 1e9f, 1e9f };
	Vector3 maxBound = { -1e9f, -1e9f, -1e9f };
	bool hasCasters = false;

	for (const auto& obj : gameObjects_) {
		if (obj.isActive && obj.hasMesh && obj.meshEnabled && obj.castShadow) {
			hasCasters = true;
			Vector3 wPos{};
			Quaternion wRot = Quaternion::Identity();
			Vector3 wScale{};
			GetWorldTRS(obj, wPos, wRot, wScale);
			float maxExtent = (std::max)({ wScale.x, wScale.y, wScale.z });
			minBound.x = (std::min)(minBound.x, wPos.x - maxExtent);
			minBound.y = (std::min)(minBound.y, wPos.y - maxExtent);
			minBound.z = (std::min)(minBound.z, wPos.z - maxExtent);

			maxBound.x = (std::max)(maxBound.x, wPos.x + maxExtent);
			maxBound.y = (std::max)(maxBound.y, wPos.y + maxExtent);
			maxBound.z = (std::max)(maxBound.z, wPos.z + maxExtent);
		}
	}

	Vector3 center = { 0.0f, 0.0f, 0.0f };
	float orthoSize = 25.0f;
	if (hasCasters) {
		center = { (minBound.x + maxBound.x) * 0.5f, (minBound.y + maxBound.y) * 0.5f, (minBound.z + maxBound.z) * 0.5f };
		float dx = (maxBound.x - minBound.x) * 0.5f;
		float dy = (maxBound.y - minBound.y) * 0.5f;
		float dz = (maxBound.z - minBound.z) * 0.5f;
		float radius = std::sqrt(dx * dx + dy * dy + dz * dz);
		orthoSize = (std::max)(radius * 1.2f, 20.0f);
	}

	float lightDist = 50.0f;
	Vector3 lightPos = { center.x - lightDir.x * lightDist, center.y - lightDir.y * lightDist, center.z - lightDir.z * lightDist };

	Vector3 fwd = lightDir;
	Vector3 up = { 0.0f, 1.0f, 0.0f };
	if (std::abs(fwd.y) > 0.99f) {
		up = { 0.0f, 0.0f, 1.0f };
	}

	Vector3 right = {
		up.y * fwd.z - up.z * fwd.y,
		up.z * fwd.x - up.x * fwd.z,
		up.x * fwd.y - up.y * fwd.x
	};
	float rightLen = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
	if (rightLen > 0.0001f) {
		right = { right.x / rightLen, right.y / rightLen, right.z / rightLen };
	} else {
		right = { 1.0f, 0.0f, 0.0f };
	}

	Vector3 realUp = {
		fwd.y * right.z - fwd.z * right.y,
		fwd.z * right.x - fwd.x * right.z,
		fwd.x * right.y - fwd.y * right.x
	};

	Matrix4x4 viewMat = {};
	viewMat.m[0][0] = right.x;
	viewMat.m[1][0] = right.y;
	viewMat.m[2][0] = right.z;
	viewMat.m[3][0] = -(lightPos.x * right.x + lightPos.y * right.y + lightPos.z * right.z);

	viewMat.m[0][1] = realUp.x;
	viewMat.m[1][1] = realUp.y;
	viewMat.m[2][1] = realUp.z;
	viewMat.m[3][1] = -(lightPos.x * realUp.x + lightPos.y * realUp.y + lightPos.z * realUp.z);

	viewMat.m[0][2] = fwd.x;
	viewMat.m[1][2] = fwd.y;
	viewMat.m[2][2] = fwd.z;
	viewMat.m[3][2] = -(lightPos.x * fwd.x + lightPos.y * fwd.y + lightPos.z * fwd.z);

	viewMat.m[3][3] = 1.0f;

	float nearClip = 0.1f;
	float farClip = 120.0f;
	Matrix4x4 projMat = Mathematics::MakeOrthographicMatrix(-orthoSize, orthoSize, orthoSize, -orthoSize, nearClip, farClip);

	lightViewProjectionMatrix_ = Mathematics::Multiply(viewMat, projMat);
}

AABB EditorSuite::CullingBounds(const GameObject& obj, const Model& model, const Matrix4x4& world, bool animated) const
{
	if (const auto ragdoll = ragdolls_.find(obj.id); ragdoll != ragdolls_.end() && ragdoll->second.hasBounds) {
		// Body centres plus room for the limbs around them
		constexpr float kMargin = 0.6f;
		return { ragdoll->second.boundsMin - Vector3{ kMargin, kMargin, kMargin }, ragdoll->second.boundsMax + Vector3{ kMargin, kMargin, kMargin } };
	}
	AABB localBox = model.GetLocalAABB();
	if (animated) {
		// Expand bounding box slightly for animated meshes so bone motion does not clip prematurely
		const Vector3 halfExt = (localBox.max - localBox.min) * 0.5f;
		localBox.min = localBox.min - halfExt;
		localBox.max = localBox.max + halfExt;
	}
	return Frustum::TransformAABB(localBox, world);
}
Model* EditorSuite::ResolveMeshModel(const GameObject& obj)
{
	switch (obj.meshType) {
	case GameObject::MeshType::Cube: return Model::CreateCube();
	case GameObject::MeshType::Sphere: return Model::CreateSphere();
	case GameObject::MeshType::Plane: return Model::CreatePlane();
	case GameObject::MeshType::Quad: return Model::CreateQuad();
	case GameObject::MeshType::CustomModel:
		if (!obj.modelPath.empty()) {
			std::filesystem::path mp(ResolveAssetPath(obj.modelPath));
			return Model::CreateFromFile(mp.parent_path().string(), mp.filename().string());
		}
		return nullptr;
	default: return nullptr;
	}
}

int EditorSuite::ComputeShadowCascades(const Camera& camera, ShadowCascade outCascades[LightGroup::kMaxCascades])
{
	if (postSettings_.shadowCascades <= 0) {
		CalculateLightViewProjection();
		outCascades[0] = { lightViewProjectionMatrix_, FLT_MAX, 1.0f, 1.0f };
		return 1;
	}

	Vector3 lightDir = { 0.0f, -1.0f, 0.0f };
	for (const auto& obj : gameObjects_) {
		if (obj.isActive && obj.hasLight && obj.lightEnabled && obj.lightType == GameObject::LightType::Directional) {
			const float len = std::sqrt(obj.lightDirection.x * obj.lightDirection.x + obj.lightDirection.y * obj.lightDirection.y + obj.lightDirection.z * obj.lightDirection.z);
			if (len > 0.0001f) lightDir = { obj.lightDirection.x / len, obj.lightDirection.y / len, obj.lightDirection.z / len };
			break;
		}
	}
	const int count = (std::min)(postSettings_.shadowCascades, LightGroup::kMaxCascades);
	const float nearZ = (std::max)(camera.nearClip, 0.01f);
	const float farZ = std::clamp(postSettings_.shadowDistance, nearZ * 2.0f, (std::max)(camera.farClip, nearZ * 2.0f));
	const Matrix4x4& proj = camera.GetProjectionMatrix();
	const Matrix4x4 invViewProj = Mathematics::Inverse(camera.GetViewProjectionMatrix());
	const auto ndcDepth = [&](float z) { return (proj.m[2][2] * z + proj.m[3][2]) / (proj.m[2][3] * z + proj.m[3][3]); };
	const auto unproject = [&](float x, float y, float ndcZ) {
		const float wx = x * invViewProj.m[0][0] + y * invViewProj.m[1][0] + ndcZ * invViewProj.m[2][0] + invViewProj.m[3][0];
		const float wy = x * invViewProj.m[0][1] + y * invViewProj.m[1][1] + ndcZ * invViewProj.m[2][1] + invViewProj.m[3][1];
		const float wz = x * invViewProj.m[0][2] + y * invViewProj.m[1][2] + ndcZ * invViewProj.m[2][2] + invViewProj.m[3][2];
		const float w = x * invViewProj.m[0][3] + y * invViewProj.m[1][3] + ndcZ * invViewProj.m[2][3] + invViewProj.m[3][3];
		return Vector3{ wx / w, wy / w, wz / w };
	};

	Vector3 up = { 0.0f, 1.0f, 0.0f };
	if (std::abs(lightDir.y) > 0.99f) up = { 0.0f, 0.0f, 1.0f };
	const auto dot = [](const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
	const auto cross = [](const Vector3& a, const Vector3& b) { return Vector3{ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; };
	Vector3 right = cross(up, lightDir);
	const float rightLen = std::sqrt(dot(right, right));
	right = { right.x / rightLen, right.y / rightLen, right.z / rightLen };
	const Vector3 realUp = cross(lightDir, right);

	constexpr float kCasterReach = 100.0f; // casters this far behind a cascade still throw shadows into it
	constexpr float kLegacyDepthRange = 119.9f; // the shadow bias settings were tuned for this range
	constexpr float kLegacyOrthoSize = 20.0f;
	float previousEnd = nearZ;
	for (int i = 0; i < count; ++i) {
		const float t = static_cast<float>(i + 1) / static_cast<float>(count);
		const float uniform = nearZ + (farZ - nearZ) * t;
		const float logarithmic = nearZ * std::pow(farZ / nearZ, t);
		const float end = i == count - 1 ? farZ : logarithmic * 0.75f + uniform * 0.25f;

		Vector3 corners[8];
		int cornerIndex = 0;
		for (const float depth : { ndcDepth(previousEnd), ndcDepth(end) }) {
			for (const float x : { -1.0f, 1.0f }) {
				for (const float y : { -1.0f, 1.0f }) corners[cornerIndex++] = unproject(x, y, depth);
			}
		}
		Vector3 center{};
		for (const Vector3& c : corners) center = { center.x + c.x * 0.125f, center.y + c.y * 0.125f, center.z + c.z * 0.125f };
		float radius = 0.0f;
		for (const Vector3& c : corners) {
			const Vector3 d = { c.x - center.x, c.y - center.y, c.z - center.z };
			radius = (std::max)(radius, std::sqrt(dot(d, d)));
		}
		radius = std::ceil(radius * 16.0f) / 16.0f; // a constant size and whole-texel steps keep the edges from crawling

		const float texel = 2.0f * radius / static_cast<float>(ShadowMap::kTileSize);
		const float cx = std::round(dot(center, right) / texel) * texel;
		const float cy = std::round(dot(center, realUp) / texel) * texel;
		const float cz = dot(center, lightDir) - radius - kCasterReach;

		Matrix4x4 view = Mathematics::MakeIdentity4x4();
		view.m[0][0] = right.x; view.m[1][0] = right.y; view.m[2][0] = right.z; view.m[3][0] = -cx;
		view.m[0][1] = realUp.x; view.m[1][1] = realUp.y; view.m[2][1] = realUp.z; view.m[3][1] = -cy;
		view.m[0][2] = lightDir.x; view.m[1][2] = lightDir.y; view.m[2][2] = lightDir.z; view.m[3][2] = -cz;
		const float depthRange = 2.0f * radius + kCasterReach;
		const Matrix4x4 ortho = Mathematics::MakeOrthographicMatrix(-radius, radius, radius, -radius, 0.0f, depthRange);
		outCascades[i] = { Mathematics::Multiply(view, ortho), end, kLegacyDepthRange / depthRange, radius / kLegacyOrthoSize };
		previousEnd = end;
	}
	return count;
}

void EditorSuite::RenderShadowPass(ID3D12GraphicsCommandList* cmdList, const Camera& camera)
{
	bool dirFound = false;
	bool castShadows = true;
	float shadowBias = 0.002f;
	float shadowStrength = 1.0f;
	float shadowNormalBias = 0.02f;

	for (const auto& obj : gameObjects_) {
		if (obj.isActive && obj.hasLight && obj.lightEnabled && obj.lightType == GameObject::LightType::Directional) {
			dirFound = true;
			castShadows = obj.lightCastShadow;
			shadowBias = obj.lightShadowBias;
			shadowStrength = obj.lightShadowStrength;
			shadowNormalBias = obj.lightShadowNormalBias;
			break;
		}
	}

	if (!dirFound || !castShadows) {
		if (lights_) {
			lights_->SetShadowActive(0);
			lights_->Update();
		}
		shadowMap_.BeginShadowPass(cmdList);
		shadowMap_.EndShadowPass(cmdList);
		return;
	}

	ShadowCascade cascades[LightGroup::kMaxCascades];
	const int cascadeCount = ComputeShadowCascades(camera, cascades);

	if (lights_) {
		lights_->ClearCascades();
		for (int i = 0; i < cascadeCount; ++i) {
			lights_->SetCascade(i, cascades[i].lightViewProjection, cascades[i].end, cascades[i].depthScale, cascades[i].normalScale);
		}
		lights_->SetShadowActive(1);
		lights_->SetShadowBias(shadowBias);
		lights_->SetShadowStrength(shadowStrength);
		lights_->SetShadowNormalBias(shadowNormalBias);
		lights_->Update();
	}

	struct Caster {
		GameObject* obj = nullptr;
		Model* model = nullptr;
		AABB bounds{};
	};
	std::vector<Caster> casters;
	casters.reserve(gameObjects_.size());
	for (auto& obj : gameObjects_) {
		if (!obj.isActive || !obj.hasMesh || !obj.meshEnabled || !obj.castShadow) continue;
		Model* model = ResolveMeshModel(obj);
		if (model) casters.push_back({ &obj, model, CullingBounds(obj, *model, obj.worldMatrix, false) });
	}

	shadowMap_.BeginShadowPass(cmdList);
	std::unordered_map<Model*, std::vector<Matrix4x4>> instances;
	for (int cascade = 0; cascade < cascadeCount; ++cascade) {
		const Matrix4x4& lightViewProjection = cascades[cascade].lightViewProjection;
		shadowMap_.SetCascadeTile(cmdList, cascade);
		const Frustum frustum = Frustum::ExtractFromViewProjection(lightViewProjection);
		for (auto& [model, matrices] : instances) matrices.clear();

		cmdList->SetPipelineState(shadowMap_.GetPipelineState());
		for (const Caster& caster : casters) {
			if (enableFrustumCulling_ && !frustum.IsAABBVisible(caster.bounds)) continue;
			if (enableInstancing_ && !caster.model->HasBones()) {
				instances[caster.model].push_back(Mathematics::Multiply(caster.obj->worldMatrix, lightViewProjection));
				continue;
			}
			GameObject& obj = *caster.obj;
			if (!obj.shadowTransform) {
				obj.shadowTransform = std::make_unique<WorldTransform>();
				obj.shadowTransform->Initialize();
			}
			Vector3 wPos{};
			Quaternion wRot = Quaternion::Identity();
			Vector3 wScale{};
			GetWorldTRS(obj, wPos, wRot, wScale);
			obj.shadowTransform->translation = wPos;
			obj.shadowTransform->rotation = (obj.parentId == -1) ? obj.rotation : wRot.ToEuler();
			obj.shadowTransform->scale = wScale;
			obj.shadowTransform->rotationQuat = wRot;
			obj.shadowTransform->UpdateMatrix(lightViewProjection);

			const D3D12_GPU_VIRTUAL_ADDRESS shadowTransformAddr = obj.shadowTransform->Upload();
			if (shadowTransformAddr == 0) continue;
			cmdList->SetGraphicsRootConstantBufferView(1, shadowTransformAddr);
			// Skinned casters use the animated palette of the edit (or game) instance; others get the shared identity
			const Object3d* paletteSource = obj.editObject3d ? obj.editObject3d.get() : obj.gameObject3d.get();
			const D3D12_GPU_VIRTUAL_ADDRESS boneAddr = paletteSource ? paletteSource->UploadBonePalette() : 0;
			if (boneAddr == 0) continue; // first frame of this object: drawing without a palette would read a null constant buffer
			cmdList->SetGraphicsRootConstantBufferView(4, boneAddr);
			caster.model->DrawGeometry(cmdList);
		}

		cmdList->SetPipelineState(shadowMap_.GetInstancedPipelineState());
		for (const auto& [model, matrices] : instances) {
			if (matrices.empty()) continue;
			const D3D12_GPU_VIRTUAL_ADDRESS address = dxCommon_->UploadData(matrices.data(), matrices.size() * sizeof(Matrix4x4));
			if (address == 0) continue;
			cmdList->SetGraphicsRootShaderResourceView(14, address);
			model->DrawGeometry(cmdList, static_cast<UINT>(matrices.size()));
		}
	}

	shadowMap_.EndShadowPass(cmdList);
}

void EditorSuite::RenderScene3D(ID3D12GraphicsCommandList* cmdList, Camera& camera, bool isEditView)
{
	camera.Update();
	if (lights_) {
		lights_->SetCameraPosition(camera.translation);
	}

	Frustum camFrustum = Frustum::ExtractFromViewProjection(camera.GetViewProjectionMatrix());
	int visibleCount = 0;
	int culledCount = 0;

	// Static meshes with the same model and material become one instanced draw
	struct InstanceGroup {
		Object3d* first = nullptr;
		std::vector<TransformationMatrix> matrices;
	};
	std::unordered_map<uint64_t, size_t> groupIndex;
	std::vector<InstanceGroup> groups;
	std::vector<Object3d*> singles;
	decalExcluded_.clear();
	bool sceneHasDecals = false;
	for (const auto& obj : gameObjects_) {
		if (obj.isActive && obj.components.Has(kDecalComponent)) { sceneHasDecals = true; break; }
	}

	for (auto& obj : gameObjects_) {
		if (!obj.isActive || !obj.hasMesh || !obj.meshEnabled) {
			continue;
		}

		auto& obj3d = isEditView ? obj.editObject3d : obj.gameObject3d;
		if (!obj3d) {
			obj3d = std::make_unique<Object3d>();
			obj3d->Initialize();
		}

		// Bind Model Primitive or Custom 3D Mesh
		Model* model = ResolveMeshModel(obj);

		if (model && obj3d->GetModel() != model) {
			obj3d->SetModel(model);
		}

		Material* mat = obj3d->GetMaterial();
		if (mat) {
			mat->color = obj.materialColor;
			mat->enableLighting = (obj.isLit && enableLighting_) ? 1 : 0;
			mat->lightingType = obj.lightingType; // Unlit, Lambert, Half-Lambert, Phong, Blinn-Phong
			mat->shininess = obj.shininess;
			mat->specularColor = obj.specularColor;
			mat->receiveShadows = (obj.receiveShadow && obj.lightingType != 0) ? 1 : 0;
			mat->explodeAmount = obj.explodeAmount;
			mat->uvOffset = obj.uvOffset;
			mat->uvTiling = obj.uvTiling;
			mat->uvRotation = obj.uvRotation;
			mat->outlineWidth = obj.meshEnabled ? obj.outlineWidth : 0.0f;
			mat->outlineColor = obj.outlineColor;
		}

		MaterialMaps maps;
		uint32_t albedoHandle = obj.textureHandle;
		if (mat) ApplyPbrMaterial(obj, *mat, maps, albedoHandle);
		obj3d->SetMaterialMaps(maps);
		obj3d->SetTextureHandle(albedoHandle);
		GeometryShaderMode gsMode = static_cast<GeometryShaderMode>(obj.geometryShaderEffect);
		if (gsMode == GeometryShaderMode::None && obj.useGeometryShader) {
			gsMode = GeometryShaderMode::PassThrough;
		}
		obj3d->SetGeometryShaderMode(gsMode);
		obj3d->SetUseGeometryShader(obj.useGeometryShader);
		{
			Vector3 wPos{};
			Quaternion wRot = Quaternion::Identity();
			Vector3 wScale{};
			GetWorldTRS(obj, wPos, wRot, wScale);
			obj3d->worldTransform.translation = wPos;
			obj3d->worldTransform.rotation = (obj.parentId == -1) ? obj.rotation : wRot.ToEuler();
			obj3d->worldTransform.scale = wScale;
			obj3d->worldTransform.rotationQuat = wRot;
		}
		obj3d->SetCamera(&camera);

		// Animation time is advanced by UpdateAnimations (game time); rendering only evaluates the pose
		if (isEditView) {
			obj3d->Update(0.0f);
		} else {
			obj3d->Update(0.0f);
			if (obj.editObject3d && model && model->HasBones()) { // static meshes draw with the shared identity palette
				obj3d->CopyBonePaletteFrom(*obj.editObject3d);
			}
		}

		if (enableFrustumCulling_ && model) {
			const AABB worldBox = CullingBounds(obj, *model, obj3d->worldTransform.matWorld, model->GetAnimationCount() > 0);
			if (!camFrustum.IsAABBVisible(worldBox)) {
				culledCount++;
				continue;
			}
		}

		visibleCount++;
		if (sceneHasDecals && !ReceivesDecals(obj, model)) {
			decalExcluded_.push_back(obj3d.get());
			continue;
		}
		if (enableInstancing_ && model && mat && !model->HasBones() && gsMode == GeometryShaderMode::None && !obj.useGeometryShader &&
			obj3d->GetBlendMode() == BlendMode::Opaque && mat->outlineWidth <= 0.0f) {
			// FNV-1a over everything that must match. A 64-bit collision would only give one object the other's material.
			uint64_t hash = 14695981039346656037ull;
			const auto mix = [&hash](const void* data, size_t size) {
				const auto* bytes = static_cast<const unsigned char*>(data);
				for (size_t i = 0; i < size; ++i) { hash ^= bytes[i]; hash *= 1099511628211ull; }
			};
			mix(&model, sizeof(model));
			mix(&albedoHandle, sizeof(albedoHandle));
			mix(&maps, sizeof(maps));
			mix(mat, sizeof(Material));
			const auto [entry, isNew] = groupIndex.try_emplace(hash, groups.size());
			if (isNew) groups.push_back({ obj3d.get(), {} });
			groups[entry->second].matrices.push_back(obj3d->worldTransform.GetMatrices());
			continue;
		}
		singles.push_back(obj3d.get());
	}

	// Instanced groups are opaque, so they go first: blended singles must see them in the depth buffer
	GraphicPipeline* pipeline = RizeEngine::GetGraphicPipeline();
	for (const InstanceGroup& group : groups) {
		if (group.matrices.size() == 1 || !pipeline) {
			singles.insert(singles.begin(), group.first);
			continue;
		}
		const D3D12_GPU_VIRTUAL_ADDRESS materialAddr = dxCommon_->UploadConstants(*group.first->GetMaterial());
		const D3D12_GPU_VIRTUAL_ADDRESS instanceAddr = dxCommon_->UploadData(group.matrices.data(), group.matrices.size() * sizeof(TransformationMatrix));
		if (materialAddr == 0 || instanceAddr == 0) continue;
		pipeline->BindInstanced(cmdList);
		cmdList->SetGraphicsRootConstantBufferView(0, materialAddr);
		cmdList->SetGraphicsRootShaderResourceView(14, instanceAddr);
		Object3d::BindSceneData(cmdList, &camera);
		group.first->GetModel()->Draw(cmdList, group.first->GetTextureHandle(), &group.first->GetMaterialMaps(), static_cast<UINT>(group.matrices.size()), group.first->GetMaterial());
	}
	for (Object3d* single : singles) single->Draw(cmdList);

	if (isEditView) {
		lastVisibleObjects_ = visibleCount;
		lastCulledObjects_ = culledCount;
	}
}

bool EditorSuite::ReceivesDecals(const GameObject& obj, const Model* model)
{
	return !obj.hasRigidbody && !(model && model->HasBones());
}

void EditorSuite::RenderViewports(ID3D12GraphicsCommandList* cmdList)
{
	if (!dxCommon_ || dxCommon_->IsDeviceLost() || !cmdList) {
		return;
	}

	RIZE_PROFILE_CPU("Record 3D");

	UpdateWorldMatrices();
	UpdateGpuParticles(cmdList);

	environmentProbe_.Update(cmdList, skybox_);
	if (lights_) lights_->SetEnvironment(postSettings_.environmentIntensity, environmentProbe_.GetMaxMip());
	if (previewLights_) previewLights_->SetEnvironment(1.0f, environmentProbe_.GetMaxMip());

	// Shadows and light clusters depend on the camera, so each view prepares its own
	const auto prepareView = [&](const Camera& camera, const RenderTexture& target) {
		RIZE_PROFILE_GPU(cmdList, "Shadow Pass");
		RenderShadowPass(cmdList, camera);
		if (lights_) lights_->SetView(camera, target.GetWidth(), target.GetHeight());
	};
	const auto setPostCamera = [&](PostProcess& post, const Camera& camera) {
		const Matrix4x4& proj = camera.GetProjectionMatrix();
		post.SetCamera(1.0f / proj.m[0][0], 1.0f / proj.m[1][1], camera.nearClip, camera.farClip, proj.m[3][3] == 0.0f);
		const Matrix4x4& view = camera.GetViewMatrix();
		post.SetCameraHeight(camera.translation.y, view.m[1][0], view.m[1][1], view.m[1][2]);
	};
	const PostProcessSettings look = EffectivePostSettings();

#ifdef USE_IMGUI
	// 1. Render to EDIT VIEW (Scene Camera)
	const uint32_t sceneViewScope = Profiler::GetInstance()->BeginGpu(cmdList, "Scene View");
	{
	RIZE_PROFILE_CPU("Record Scene View");
	editorCamera_.Update();
	prepareView(editorCamera_, editSceneRT_);
	editSceneRT_.BeginRender();
	skybox_.Draw(cmdList, editorCamera_);
	RenderScene3D(cmdList, editorCamera_, true);
	DrawDecals(cmdList, editorCamera_, editSceneRT_);
	if (showGrid_) {
		// Depth-tested ground grid: hidden behind scene geometry, unlike an ImGui overlay.
		// Lifted slightly above y = 0 so it does not z-fight with a ground plane at the origin.
		const Vector3 gridCenter = { editorCamera_.translation.x, 0.002f, editorCamera_.translation.z };
		PrimitiveDrawer::GetInstance()->DrawEditorGrid(gridCenter, 20,
			{ 0.60f, 0.60f, 0.64f, 0.55f },  // grid lines
			{ 0.85f, 0.25f, 0.25f, 0.85f },  // X axis (z = 0)
			{ 0.25f, 0.45f, 0.90f, 0.85f }); // Z axis (x = 0)
		PrimitiveDrawer::GetInstance()->Render(cmdList, editorCamera_);
	}
	DrawEffects(cmdList, editorCamera_, editSceneRT_);
	editSceneRT_.EndRender();
	// The Scene view shows the scene itself, unless "Post FX" previews the game's look there (its own PostProcess)
	if (scenePostPreview_) {
		RIZE_PROFILE_GPU(cmdList, "Scene View Post");
		setPostCamera(scenePostProcess_, editorCamera_);
		scenePostProcess_.Prepare(cmdList, editSceneRT_, look, static_cast<float>(gameTime_));
		editViewRT_.BeginRender(false);
		scenePostProcess_.Composite(cmdList);
	} else {
		editViewRT_.BeginRender(false);
		postProcess_.Resolve(cmdList, editSceneRT_);
	}
	editViewRT_.EndRender();
	}
	Profiler::GetInstance()->EndGpu(cmdList, sceneViewScope);

	// 2. Render to GAME VIEW (Player Game Camera)
	const uint32_t gameViewScope = Profiler::GetInstance()->BeginGpu(cmdList, "Game View");
	{
	RIZE_PROFILE_CPU("Record Game View");
	if (gameCamera_) {
		gameCamera_->Update();
		prepareView(*gameCamera_, gameSceneRT_);
		setPostCamera(postProcess_, *gameCamera_);
	}
	gameSceneRT_.BeginRender();
	if (gameCamera_) {
		skybox_.Draw(cmdList, *gameCamera_);
		RenderScene3D(cmdList, *gameCamera_, false);
		DrawDecals(cmdList, *gameCamera_, gameSceneRT_);
		DrawEffects(cmdList, *gameCamera_, gameSceneRT_);
	}
	gameSceneRT_.EndRender();
	{
		RIZE_PROFILE_CPU("Post Process");
		RIZE_PROFILE_GPU(cmdList, "Post Process");
		postProcess_.Prepare(cmdList, gameSceneRT_, look, static_cast<float>(gameTime_));
		gameViewRT_.BeginRender(false);
		postProcess_.Composite(cmdList);
	}
	// Game UI (HUD/menus from scripts) stays sharp on top of the post-processed view
	if (sceneState_ != EditorSceneState::Edit || IsStandaloneMode()) {
		UIRenderer::GetInstance()->Render(cmdList);
	}
	gameViewRT_.EndRender();
	}
	Profiler::GetInstance()->EndGpu(cmdList, gameViewScope);

	// 3. Render 3D Asset Preview (if a 3D model is currently selected in the Asset Browser)
	if (previewModel_ && previewObject3d_ && modelPreviewRT_.IsInitialized() && !IsStandaloneMode()) {
		RIZE_PROFILE_GPU(cmdList, "Asset Preview");
		previewSceneRT_.BeginRender();
		TextureManager::GetInstance()->SetDescriptorHeaps(cmdList);
		previewCamera_.Update();

		if (previewLights_) {
			previewLights_->SetCameraPosition(previewCamera_.translation);
			previewLights_->SetView(previewCamera_, previewSceneRT_.GetWidth(), previewSceneRT_.GetHeight());
			Object3d::StaticSetLightGroup(previewLights_.get());
		}

		previewObject3d_->worldTransform.scale = { previewModelScale_, previewModelScale_, previewModelScale_ };
		previewObject3d_->worldTransform.rotation = { 0.0f, previewRotationY_, 0.0f };
		previewObject3d_->worldTransform.rotationQuat = Quaternion::FromEuler(previewObject3d_->worldTransform.rotation);

		// Center the scaled model around the origin regardless of its Y rotation angle
		const float cx = previewModelOffset_.x * previewModelScale_;
		const float cy = previewModelOffset_.y * previewModelScale_;
		const float cz = previewModelOffset_.z * previewModelScale_;
		const float cosY = std::cos(previewRotationY_);
		const float sinY = std::sin(previewRotationY_);
		const float rx = cx * cosY + cz * sinY;
		const float ry = cy;
		const float rz = -cx * sinY + cz * cosY;
		previewObject3d_->worldTransform.translation = { -rx, -ry, -rz };

		previewObject3d_->SetCamera(&previewCamera_);
		previewObject3d_->Update(currentDeltaTime_);
		previewObject3d_->Draw(cmdList);

		if (lights_) {
			Object3d::StaticSetLightGroup(lights_);
		}
		previewSceneRT_.EndRender();
		modelPreviewRT_.BeginRender(false);
		postProcess_.Resolve(cmdList, previewSceneRT_);
		modelPreviewRT_.EndRender();
	}

	// 3b. The Material Graph's preview (the same target as the model preview, which wins when both are open)
	if (MaterialPreviewActive()) {
		RIZE_PROFILE_GPU(cmdList, "Material Preview");
		RenderMaterialPreview(cmdList);
	} else if (EffectPanelPreviewActive()) {
		RIZE_PROFILE_GPU(cmdList, "Effect Preview");
		RenderEffectPreview(cmdList);
	}

	// 4. Restore to swapchain backbuffer for ImGui UI drawing
	dxCommon_->BindMainRenderTarget();
#else
	// Release build without ImGui: same scene -> post-process -> UI path as the editor's Game view, at the window's size
	if (dxCommon_->GetClientWidth() != gameSceneRT_.GetWidth() || dxCommon_->GetClientHeight() != gameSceneRT_.GetHeight()) {
		dxCommon_->SignalAndWaitFence(); // the scene descriptors are rewritten in place
		InitializeGameSceneTarget(dxCommon_->GetClientWidth(), dxCommon_->GetClientHeight());
	}
	if (gameCamera_) {
		gameCamera_->Update();
		prepareView(*gameCamera_, gameSceneRT_);
		setPostCamera(postProcess_, *gameCamera_);
	}
	RIZE_PROFILE_GPU(cmdList, "Game View");
	gameSceneRT_.BeginRender();
	if (gameCamera_) {
		skybox_.Draw(cmdList, *gameCamera_);
		RenderScene3D(cmdList, *gameCamera_, true);
		DrawDecals(cmdList, *gameCamera_, gameSceneRT_);
		DrawEffects(cmdList, *gameCamera_, gameSceneRT_);
	}
	gameSceneRT_.EndRender();
	{
		RIZE_PROFILE_CPU("Post Process");
		postProcess_.Prepare(cmdList, gameSceneRT_, look, static_cast<float>(gameTime_));
		dxCommon_->BindMainRenderTarget();
		postProcess_.Composite(cmdList);
	}
	UIRenderer::GetInstance()->Render(cmdList);
#endif
}

void EditorSuite::InitializeGameSceneTarget(uint32_t width, uint32_t height)
{
	if (gameDepthSrvIndex_ == DirectXCommon::kInvalidSrvIndex) {
		gameSceneSrvIndex_ = dxCommon_->AllocateSrvIndex();
		gameDepthSrvIndex_ = dxCommon_->AllocateSrvIndex();
	}
	const float clearColor[4] = { 0.08f, 0.18f, 0.35f, 1.0f };
	gameSceneRT_.Initialize(dxCommon_, width, height, DirectXCommon::kSceneRtvBaseIndex + 1, DirectXCommon::kSceneDsvBaseIndex + 1,
		gameSceneSrvIndex_, clearColor, true, gameDepthSrvIndex_);
}

