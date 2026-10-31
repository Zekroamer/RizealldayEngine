#include "EditorSuiteInternal.h"

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

void EditorSuite::RenderShadowPass(ID3D12GraphicsCommandList* cmdList)
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

	CalculateLightViewProjection();

	if (lights_) {
		lights_->SetLightViewProjection(lightViewProjectionMatrix_);
		lights_->SetShadowActive(1);
		lights_->SetShadowBias(shadowBias);
		lights_->SetShadowStrength(shadowStrength);
		lights_->SetShadowNormalBias(shadowNormalBias);
		lights_->Update();
	}

	shadowMap_.BeginShadowPass(cmdList);

	Frustum shadowFrustum = Frustum::ExtractFromViewProjection(lightViewProjectionMatrix_);

	for (auto& obj : gameObjects_) {
		if (!obj.isActive || !obj.hasMesh || !obj.meshEnabled || !obj.castShadow) {
			continue;
		}

		Model* model = nullptr;
		if (obj.meshType == GameObject::MeshType::Cube) {
			model = Model::CreateCube();
		} else if (obj.meshType == GameObject::MeshType::Sphere) {
			model = Model::CreateSphere();
		} else if (obj.meshType == GameObject::MeshType::Plane) {
			model = Model::CreatePlane();
		} else if (obj.meshType == GameObject::MeshType::Quad) {
			model = Model::CreateQuad();
		} else if (obj.meshType == GameObject::MeshType::CustomModel && !obj.modelPath.empty()) {
			std::filesystem::path mp(ResolveAssetPath(obj.modelPath));
			model = Model::CreateFromFile(mp.parent_path().string(), mp.filename().string());
		}

		if (!model) continue;

		if (!obj.shadowTransform) {
			obj.shadowTransform = std::make_unique<WorldTransform>();
			obj.shadowTransform->Initialize();
		}
		{
			Vector3 wPos{};
			Quaternion wRot = Quaternion::Identity();
			Vector3 wScale{};
			GetWorldTRS(obj, wPos, wRot, wScale);
			obj.shadowTransform->translation = wPos;
			obj.shadowTransform->rotation = (obj.parentId == -1) ? obj.rotation : wRot.ToEuler();
			obj.shadowTransform->scale = wScale;
			obj.shadowTransform->rotationQuat = wRot;
		}
		obj.shadowTransform->UpdateMatrix(lightViewProjectionMatrix_);

		if (enableFrustumCulling_) {
			AABB worldBox = Frustum::TransformAABB(model->GetLocalAABB(), obj.shadowTransform->matWorld);
			if (!shadowFrustum.IsAABBVisible(worldBox)) {
				continue;
			}
		}

		const D3D12_GPU_VIRTUAL_ADDRESS shadowTransformAddr = obj.shadowTransform->Upload();
		if (shadowTransformAddr == 0) continue;
		cmdList->SetGraphicsRootConstantBufferView(1, shadowTransformAddr);
		// Skinned casters use the animated palette of the edit (or game) instance; others get the shared identity
		const Object3d* paletteSource = obj.editObject3d ? obj.editObject3d.get() : obj.gameObject3d.get();
		if (paletteSource) {
			if (const D3D12_GPU_VIRTUAL_ADDRESS boneAddr = paletteSource->UploadBonePalette()) {
				cmdList->SetGraphicsRootConstantBufferView(4, boneAddr);
			}
		}
		model->Draw(cmdList);
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
		Model* model = nullptr;
		if (obj.meshType == GameObject::MeshType::Cube) {
			model = Model::CreateCube();
		} else if (obj.meshType == GameObject::MeshType::Sphere) {
			model = Model::CreateSphere();
		} else if (obj.meshType == GameObject::MeshType::Plane) {
			model = Model::CreatePlane();
		} else if (obj.meshType == GameObject::MeshType::Quad) {
			model = Model::CreateQuad();
		} else if (obj.meshType == GameObject::MeshType::CustomModel && !obj.modelPath.empty()) {
			std::filesystem::path mp(ResolveAssetPath(obj.modelPath));
			model = Model::CreateFromFile(mp.parent_path().string(), mp.filename().string());
		}

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

		obj3d->SetTextureHandle(obj.textureHandle);
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
			AABB localBox = model->GetLocalAABB();
			if (model->GetAnimationCount() > 0) {
				// Expand bounding box slightly for animated meshes so bone motion does not clip prematurely
				Vector3 halfExt = (localBox.max - localBox.min) * 0.5f;
				localBox.min = localBox.min - halfExt;
				localBox.max = localBox.max + halfExt;
			}
			AABB worldBox = Frustum::TransformAABB(localBox, obj3d->worldTransform.matWorld);
			if (!camFrustum.IsAABBVisible(worldBox)) {
				culledCount++;
				continue;
			}
		}

		visibleCount++;
		obj3d->Draw(cmdList);
	}

	if (isEditView) {
		lastVisibleObjects_ = visibleCount;
		lastCulledObjects_ = culledCount;
	}
}

void EditorSuite::RenderViewports(ID3D12GraphicsCommandList* cmdList, bool showParticles)
{
	if (!dxCommon_ || dxCommon_->IsDeviceLost() || !cmdList) {
		return;
	}

	RIZE_PROFILE_CPU("Record 3D");

	UpdateWorldMatrices();

	bool activeParticles = HasActiveParticleSystem() && showParticles;

	// 0. Directional Light Shadow Pass (Render active casters into shadow map)
	{
		RIZE_PROFILE_GPU(cmdList, "Shadow Pass");
		RenderShadowPass(cmdList);
	}

#ifdef USE_IMGUI
	// 1. Render to EDIT VIEW (Scene Camera)
	const uint32_t sceneViewScope = Profiler::GetInstance()->BeginGpu(cmdList, "Scene View");
	{
	RIZE_PROFILE_CPU("Record Scene View");
	editSceneRT_.BeginRender();
	editorCamera_.Update();
	skybox_.Draw(cmdList, editorCamera_);
	RenderScene3D(cmdList, editorCamera_, true);
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
	if (activeParticles) {
		for (auto& obj : gameObjects_) {
			if (obj.isActive && obj.hasParticleSystem && obj.particleQuad) {
				obj.particleQuad->PrepareForCamera(editorCamera_, ParticleQuad::kPassScene);
				obj.particleQuad->Draw(cmdList, ParticleQuad::kPassScene);
			}
		}
	}
	editSceneRT_.EndRender();
	// The Scene view shows the scene itself: post-processing is the game's look
	editViewRT_.BeginRender(false);
	postProcess_.Resolve(cmdList, editSceneRT_);
	editViewRT_.EndRender();
	}
	Profiler::GetInstance()->EndGpu(cmdList, sceneViewScope);

	// 2. Render to GAME VIEW (Player Game Camera)
	const uint32_t gameViewScope = Profiler::GetInstance()->BeginGpu(cmdList, "Game View");
	{
	RIZE_PROFILE_CPU("Record Game View");
	gameSceneRT_.BeginRender();
	if (gameCamera_) {
		gameCamera_->Update();
		skybox_.Draw(cmdList, *gameCamera_);
		RenderScene3D(cmdList, *gameCamera_, false);
		if (activeParticles) {
			for (auto& obj : gameObjects_) {
				if (obj.isActive && obj.hasParticleSystem && obj.particleQuad) {
					obj.particleQuad->PrepareForCamera(*gameCamera_, ParticleQuad::kPassGame);
					obj.particleQuad->Draw(cmdList, ParticleQuad::kPassGame);
				}
			}
		}
	}
	gameSceneRT_.EndRender();
	{
		RIZE_PROFILE_CPU("Post Process");
		RIZE_PROFILE_GPU(cmdList, "Post Process");
		postProcess_.Prepare(cmdList, gameSceneRT_, postSettings_, static_cast<float>(gameTime_));
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
			previewLights_->Update();
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

	// 4. Restore to swapchain backbuffer for ImGui UI drawing
	dxCommon_->BindMainRenderTarget();
#else
	// Release build without ImGui: same scene -> post-process -> UI path as the editor's Game view, at the window's size
	if (dxCommon_->GetClientWidth() != gameSceneRT_.GetWidth() || dxCommon_->GetClientHeight() != gameSceneRT_.GetHeight()) {
		dxCommon_->SignalAndWaitFence(); // the scene descriptors are rewritten in place
		InitializeGameSceneTarget(dxCommon_->GetClientWidth(), dxCommon_->GetClientHeight());
	}
	RIZE_PROFILE_GPU(cmdList, "Game View");
	gameSceneRT_.BeginRender();
	if (gameCamera_) {
		gameCamera_->Update();
		skybox_.Draw(cmdList, *gameCamera_);
		RenderScene3D(cmdList, *gameCamera_, true);
		if (activeParticles) {
			for (auto& obj : gameObjects_) {
				if (obj.isActive && obj.hasParticleSystem && obj.particleQuad) {
					obj.particleQuad->PrepareForCamera(*gameCamera_, ParticleQuad::kPassGame);
					obj.particleQuad->Draw(cmdList, ParticleQuad::kPassGame);
				}
			}
		}
	}
	gameSceneRT_.EndRender();
	{
		RIZE_PROFILE_CPU("Post Process");
		postProcess_.Prepare(cmdList, gameSceneRT_, postSettings_, static_cast<float>(gameTime_));
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

