#include "EditorSuiteInternal.h"
#include "audio/Audio.h"

// Inspector bodies of the built-in components. Each one is registered in EditorSuite_Components.cpp;
// DrawInspectorAndDetailsPanel() calls them through the component registry.

void EditorSuite::DrawMeshComponentInspector(GameObject& obj)
{
#ifdef USE_IMGUI
	if (DrawComponentHeader("Mesh Filter & Renderer", ComponentKind::Mesh)) {
		if (ImGui::Checkbox(TrId("Enabled##Mesh"), &obj.meshEnabled)) { isSceneDirty_ = true; PushUndoSnapshot(); }
		const char* meshTypes[] = { "Cube", "Sphere", "Plane", "Quad", "CustomModel" };
		int currentMesh = static_cast<int>(obj.meshType);
		if (ImGui::Combo(TrId("Mesh Prototype"), &currentMesh, meshTypes, 5)) {
			obj.meshType = static_cast<GameObject::MeshType>(currentMesh);
			isSceneDirty_ = true;
			PushUndoSnapshot();
		}
		if (obj.meshType == GameObject::MeshType::CustomModel) {
			char modelBuf[256] = {};
			strncpy_s(modelBuf, obj.modelPath.c_str(), sizeof(modelBuf) - 1);
			if (ImGui::InputText(TrId("Model Path"), modelBuf, sizeof(modelBuf))) {
				obj.modelPath = modelBuf;
				isSceneDirty_ = true;
			}
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				if (obj.hasBoxCollider) FitBoxColliderToMesh(obj); // a new model gets a box that fits it
				PushUndoSnapshot();
			}
		}

		if (ImGui::ColorEdit4(TrId("Material Color"), &obj.materialColor.x)) { isSceneDirty_ = true; }
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();

		if (ImGui::Checkbox(TrId("Lit Material"), &obj.isLit)) { isSceneDirty_ = true; PushUndoSnapshot(); }

		const char* reflectionModels[] = { "0: Unlit", "1: Lambert", "2: Half-Lambert", "3: Phong", "4: Blinn-Phong", "5: Toon" };
		if (ImGui::Combo(TrId("Reflection Model"), &obj.lightingType, reflectionModels, 6)) {
			isSceneDirty_ = true;
			PushUndoSnapshot();
		}
		ImGui::SetItemTooltip(Tr("Select illumination shading equation (02_00 Phong, 02_01 Blinn-Phong, Half-Lambert, Unlit, or Toon = cel-shaded bands)"));

		if (obj.lightingType >= 3) {
			if (ImGui::SliderFloat(TrId("Shininess"), &obj.shininess, 1.0f, 256.0f, "%.1f")) { isSceneDirty_ = true; }
			if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
			ImGui::SetItemTooltip(Tr("Specular exponent power factor"));

			if (ImGui::ColorEdit3(TrId("Specular Color"), &obj.specularColor.x)) { isSceneDirty_ = true; }
			if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		}

		if (ImGui::DragFloat(TrId("Outline Width"), &obj.outlineWidth, 0.001f, 0.0f, 0.5f, "%.3f")) {
			obj.outlineWidth = (std::max)(0.0f, obj.outlineWidth);
			isSceneDirty_ = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		ImGui::SetItemTooltip("%s", Tr("Cartoon outline in world units (0 = none). Pairs well with the Toon reflection model."));
		if (obj.outlineWidth > 0.0f) {
			if (ImGui::ColorEdit4(TrId("Outline Color"), &obj.outlineColor.x)) { isSceneDirty_ = true; }
			if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		}

		if (ImGui::Checkbox(TrId("Cast Shadows##Mesh"), &obj.castShadow)) { isSceneDirty_ = true; PushUndoSnapshot(); }
		ImGui::SetItemTooltip(Tr("Render this mesh into the directional light shadow map"));
		ImGui::SameLine();
		if (ImGui::Checkbox(TrId("Receive Shadows##Mesh"), &obj.receiveShadow)) { isSceneDirty_ = true; PushUndoSnapshot(); }
		ImGui::SetItemTooltip(Tr("Receive shadows cast by other geometry (Unlit objects never receive shadows)"));

		const char* gsEffects[] = { "None", "Standard Pass-Through", "Explode Faces Along Normals" };
		if (ImGui::Combo(TrId("Geometry Shader Effect"), &obj.geometryShaderEffect, gsEffects, 3)) {
			obj.useGeometryShader = (obj.geometryShaderEffect != 0);
			isSceneDirty_ = true;
			PushUndoSnapshot();
		}
		ImGui::SetItemTooltip(Tr("DirectX 12 SM 6.0 Geometry Shader pipeline variants"));

		if (obj.geometryShaderEffect == 2) {
			if (ImGui::SliderFloat(TrId("Explode Amount"), &obj.explodeAmount, 0.0f, 2.0f, "%.2f")) {
				isSceneDirty_ = true;
			}
			if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
			ImGui::SetItemTooltip(Tr("Displacement distance along polygon face normals in the geometry shader"));
		}

		ImGui::Separator();
		ImGui::Text(Tr("Textured Surface / UV Controls:"));

		char texBuf[256];
		strncpy_s(texBuf, obj.texturePath.c_str(), sizeof(texBuf));
		if (ImGui::InputText(TrId("Texture Path"), texBuf, sizeof(texBuf))) {
			obj.texturePath = texBuf;
			obj.textureHandle = TextureManager::GetInstance()->LoadTexture(ResolveAssetPath(obj.texturePath));
			isSceneDirty_ = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		ImGui::SetItemTooltip(Tr("Path to texture file (e.g. Resources/circle.png)"));

		const char* texPresets[] = { "(Choose Preset...)", "Resources/circle.png", "Resources/Model/white.png", "Resources/uvChecker.png" };
		int selectedPreset = 0;
		if (ImGui::Combo(TrId("Preset Selector"), &selectedPreset, texPresets, 4)) {
			if (selectedPreset == 1) {
				obj.texturePath = "Resources/circle.png";
				obj.textureHandle = TextureManager::GetInstance()->LoadTexture(ResolveAssetPath(obj.texturePath));
				isSceneDirty_ = true;
				PushUndoSnapshot();
			} else if (selectedPreset == 2) {
				obj.texturePath = "Resources/Model/white.png";
				obj.textureHandle = TextureManager::GetInstance()->LoadTexture(ResolveAssetPath(obj.texturePath));
				isSceneDirty_ = true;
				PushUndoSnapshot();
			} else if (selectedPreset == 3) {
				obj.texturePath = "Resources/uvChecker.png";
				obj.textureHandle = TextureManager::GetInstance()->LoadTexture(ResolveAssetPath(obj.texturePath));
				isSceneDirty_ = true;
				PushUndoSnapshot();
			}
		}

		if (ImGui::ColorEdit4(TrId("Tint Color"), &obj.materialColor.x)) { isSceneDirty_ = true; }
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		ImGui::SetItemTooltip(Tr("Color multiplier tint applied to the texture (RGBA)"));

		if (ImGui::DragFloat2(TrId("UV Offset XY"), &obj.uvOffset.x, 0.01f)) isSceneDirty_ = true;
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		ImGui::SetItemTooltip(Tr("UV texture coordinate offset (U, V)"));

		if (ImGui::DragFloat2(TrId("UV Tiling XY"), &obj.uvTiling.x, 0.01f)) isSceneDirty_ = true;
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		ImGui::SetItemTooltip(Tr("UV texture coordinate scaling / repeat (U, V)"));

		if (ImGui::DragFloat(TrId("UV Rotation"), &obj.uvRotation, 0.01f)) isSceneDirty_ = true;
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		ImGui::SetItemTooltip(Tr("UV coordinate rotation angle in radians"));
	}

	// ANIMATOR / SKELETAL ANIMATION SECTION
	Model* activeModel = obj.editObject3d ? obj.editObject3d->GetModel() : (obj.gameObject3d ? obj.gameObject3d->GetModel() : nullptr);
	if (activeModel && activeModel->GetAnimationCount() > 0) {
		if (ImGui::CollapsingHeader(TrId("Animator (FBX / glTF Animation)"), ImGuiTreeNodeFlags_DefaultOpen)) {
			uint32_t animCount = static_cast<uint32_t>(activeModel->GetAnimationCount());
			std::vector<std::string> clipNames;
			std::vector<const char*> clipNamePtrs;
			for (uint32_t a = 0; a < animCount; ++a) {
				clipNames.push_back(std::format("[{}] {}", a, activeModel->GetAnimationName(static_cast<int>(a))));
			}
			for (const auto& cn : clipNames) {
				clipNamePtrs.push_back(cn.c_str());
			}

			int curClip = std::clamp(obj.animationIndex, 0, static_cast<int>(animCount) - 1);
			if (ImGui::Combo(TrId("Animation Clip"), &curClip, clipNamePtrs.data(), static_cast<int>(clipNamePtrs.size()))) {
				obj.animationIndex = curClip;
				if (obj.editObject3d) {
					obj.editObject3d->SetCurrentAnimationIndex(static_cast<uint32_t>(curClip));
					obj.editObject3d->ResetAnimationTime();
				}
				if (obj.gameObject3d) {
					obj.gameObject3d->SetCurrentAnimationIndex(static_cast<uint32_t>(curClip));
					obj.gameObject3d->ResetAnimationTime();
				}
				isSceneDirty_ = true;
				PushUndoSnapshot();
			}

			if (ImGui::Checkbox(TrId("Play Animation"), &obj.animationPlaying)) {
				isSceneDirty_ = true;
				PushUndoSnapshot();
			}
			ImGui::SameLine();
			if (ImGui::Checkbox(TrId("Loop##Anim"), &obj.animationLoop)) {
				isSceneDirty_ = true;
				PushUndoSnapshot();
			}
			ImGui::SameLine();
			if (ImGui::Button(TrId("Restart Clip"))) {
				if (obj.editObject3d) obj.editObject3d->ResetAnimationTime();
				if (obj.gameObject3d) obj.gameObject3d->ResetAnimationTime();
			}

			if (ImGui::SliderFloat(TrId("Playback Speed"), &obj.animationSpeed, 0.0f, 3.0f, "%.2fx")) {
				isSceneDirty_ = true;
			}
			if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();

			if (obj.editObject3d) {
				ImGui::TextDisabled(Tr("Current Time: %.2f s  |  Clips: %u"), obj.editObject3d->GetAnimationTime(), animCount);
			}
		}
	}
#else
	(void)obj;
#endif
}

void EditorSuite::DrawBoxColliderComponentInspector(GameObject& obj)
{
#ifdef USE_IMGUI
	if (DrawComponentHeader("Box Collider", ComponentKind::BoxCollider)) {
		if (ImGui::Checkbox(TrId("Enabled##BoxCol"), &obj.boxColliderEnabled)) { isSceneDirty_ = true; PushUndoSnapshot(); }
		ImGui::SameLine();
		if (ImGui::Button(isEditingColliderBounds_ ? " [ Editing Bounds ] " : " Edit Bounds ")) {
			isEditingColliderBounds_ = !isEditingColliderBounds_;
		}
		ImGui::SetItemTooltip(Tr("Enable interactive handle editing of box collider extents"));
		if (obj.meshType == GameObject::MeshType::CustomModel) {
			ImGui::SameLine();
			if (ImGui::Button(TrId("Fit to Mesh")) && FitBoxColliderToMesh(obj)) {
				isSceneDirty_ = true;
				PushUndoSnapshot();
			}
			ImGui::SetItemTooltip("%s", Tr("Set Center and Size to the model's bounds (also right for models scaled to 0.01)"));
		}

		if (ImGui::Checkbox(TrId("Is Trigger"), &obj.isTrigger)) { isSceneDirty_ = true; PushUndoSnapshot(); }
		ImGui::SetItemTooltip(Tr("Trigger volume: detects overlaps without generating physical collision response"));

		ImGui::SameLine();
		if (ImGui::Checkbox(TrId("Provides Contacts"), &obj.providesContacts)) { isSceneDirty_ = true; PushUndoSnapshot(); }
		ImGui::SetItemTooltip(Tr("Generate contact points and impulse resolution"));

		DrawVec3Control(TrId("Center"), obj.boxCenter, 0.0f);
		ImGui::SetItemTooltip(Tr("Offset of box collider center relative to transform position"));

		DrawVec3Control(TrId("Size"), obj.boxSize, 1.0f);
		ImGui::SetItemTooltip(Tr("Dimensions of box collider along X, Y, Z axes"));
	}
#else
	(void)obj;
#endif
}

void EditorSuite::DrawSphereColliderComponentInspector(GameObject& obj)
{
#ifdef USE_IMGUI
	if (DrawComponentHeader("Sphere Collider", ComponentKind::SphereCollider)) {
		if (ImGui::Checkbox(TrId("Enabled##SphCol"), &obj.sphereColliderEnabled)) { isSceneDirty_ = true; PushUndoSnapshot(); }
		if (ImGui::Checkbox(TrId("Is Trigger##Sph"), &obj.sphereIsTrigger)) { isSceneDirty_ = true; PushUndoSnapshot(); }
		DrawVec3Control(TrId("Center##Sph"), obj.sphereCenter, 0.0f);
		ImGui::SetItemTooltip(Tr("Center offset of sphere collider"));

		if (ImGui::DragFloat(TrId("Radius"), &obj.sphereRadius, 0.05f, 0.01f, 100.0f)) isSceneDirty_ = true;
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		ImGui::SetItemTooltip(Tr("Radius of sphere collider"));
	}
#else
	(void)obj;
#endif
}

void EditorSuite::DrawRigidbodyComponentInspector(GameObject& obj)
{
#ifdef USE_IMGUI
	if (DrawComponentHeader("Rigidbody", ComponentKind::Rigidbody)) {
		if (ImGui::Checkbox(TrId("Enabled##RB"), &obj.rbEnabled)) { isSceneDirty_ = true; PushUndoSnapshot(); }

		if (ImGui::DragFloat(TrId("Mass (kg)"), &obj.mass, 0.1f, 0.01f, 1000.0f, "%.2f")) isSceneDirty_ = true;
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		ImGui::SetItemTooltip(Tr("Mass of the object in kilograms"));

		if (ImGui::DragFloat(TrId("Linear Drag"), &obj.drag, 0.01f, 0.0f, 10.0f, "%.2f")) isSceneDirty_ = true;
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		ImGui::SetItemTooltip(Tr("Linear air resistance damping"));

		if (ImGui::DragFloat(TrId("Gravity Scale"), &obj.gravityScale, 0.05f, -10.0f, 10.0f, "%.2f")) isSceneDirty_ = true;
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		ImGui::SetItemTooltip(Tr("Multiplier for gravitational acceleration"));

		if (ImGui::Checkbox(TrId("Use Gravity"), &obj.useGravity)) { isSceneDirty_ = true; PushUndoSnapshot(); }
		ImGui::SetItemTooltip(Tr("Enable constant downward gravitational acceleration"));

		ImGui::SameLine();
		if (ImGui::Checkbox(TrId("Is Kinematic"), &obj.isKinematic)) { isSceneDirty_ = true; PushUndoSnapshot(); }
		ImGui::SetItemTooltip(Tr("Ignore physical forces and move only via script or transform"));

		if (ImGui::SliderFloat(TrId("Bounciness"), &obj.bounciness, 0.0f, 1.0f, "%.2f")) isSceneDirty_ = true;
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		ImGui::SetItemTooltip(Tr("Coefficient of restitution for collisions"));

		if (ImGui::SliderFloat(TrId("Friction"), &obj.friction, 0.0f, 1.0f, "%.2f")) isSceneDirty_ = true;
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();

		if (ImGui::Checkbox(TrId("Freeze Rotation"), &obj.freezeRotation)) { isSceneDirty_ = true; PushUndoSnapshot(); }
		ImGui::SetItemTooltip("%s", Tr("Keep the object upright: collisions move it but never turn it"));

		if (ImGui::Checkbox(TrId("Continuous Collision"), &obj.continuousCollision)) { isSceneDirty_ = true; PushUndoSnapshot(); }
		ImGui::SetItemTooltip("%s", Tr("Stops fast objects from passing through thin colliders (costs more)"));
	}
#else
	(void)obj;
#endif
}

void EditorSuite::DrawCameraComponentInspector(GameObject& obj)
{
#ifdef USE_IMGUI
	if (DrawComponentHeader("Camera", ComponentKind::Camera)) {
		if (ImGui::Checkbox(TrId("Enabled##Cam"), &obj.cameraEnabled)) { isSceneDirty_ = true; PushUndoSnapshot(); }
		if (ImGui::Checkbox(TrId("Orthographic"), &obj.isOrthographic)) { isSceneDirty_ = true; PushUndoSnapshot(); }
		ImGui::SetItemTooltip("%s", Tr("No perspective: far objects keep their size (isometric look)"));
		if (obj.isOrthographic) {
			if (ImGui::DragFloat(TrId("Size"), &obj.orthoSize, 0.1f, 0.1f, 500.0f, "%.2f")) isSceneDirty_ = true;
			ImGui::SetItemTooltip("%s", Tr("Half of the visible height in world units"));
		} else {
			if (ImGui::SliderFloat(TrId("Field of View"), &obj.fov, 10.0f, 150.0f, "%.1f deg")) isSceneDirty_ = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		if (ImGui::DragFloat(TrId("Near Clip"), &obj.nearClip, 0.01f, 0.001f, 10.0f)) isSceneDirty_ = true;
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		if (ImGui::DragFloat(TrId("Far Clip"), &obj.farClip, 5.0f, 10.0f, 5000.0f)) isSceneDirty_ = true;
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
	}
#else
	(void)obj;
#endif
}

void EditorSuite::DrawLightComponentInspector(GameObject& obj)
{
#ifdef USE_IMGUI
	if (DrawComponentHeader("Light", ComponentKind::Light)) {
		if (ImGui::Checkbox(TrId("Enabled##Light"), &obj.lightEnabled)) { isSceneDirty_ = true; PushUndoSnapshot(); }
		const char* lightTypes[] = { "Directional", "Point", "Spot" };
		int currentLight = static_cast<int>(obj.lightType);
		if (ImGui::Combo(TrId("Light Type"), &currentLight, lightTypes, 3)) {
			obj.lightType = static_cast<GameObject::LightType>(currentLight);
			isSceneDirty_ = true;
			PushUndoSnapshot();
		}
		ImGui::SetItemTooltip(Tr("Light emitter type: Directional, Point Light (03_00), or Spot Light (03_01)"));

		if (ImGui::ColorEdit3(TrId("Color"), &obj.lightColor.x)) isSceneDirty_ = true;
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		ImGui::SetItemTooltip(Tr("RGB light emission color"));

		if (ImGui::DragFloat(TrId("Intensity"), &obj.lightIntensity, 0.05f, 0.0f, 100.0f)) isSceneDirty_ = true;
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		ImGui::SetItemTooltip(Tr("Light brightness multiplier"));

		if (obj.lightType == GameObject::LightType::Directional) {
			if (DrawVec3Control(TrId("Direction"), obj.lightDirection, 0.0f)) {
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
			ImGui::SetItemTooltip(Tr("Direction vector of light emission (linked with Transform Rotation)"));

			if (ImGui::Checkbox(TrId("Cast Shadows##Light"), &obj.lightCastShadow)) { isSceneDirty_ = true; PushUndoSnapshot(); }
			ImGui::SetItemTooltip(Tr("Enable directional light shadow map rendering and attenuation"));

			if (ImGui::SliderFloat(TrId("Shadow Bias"), &obj.lightShadowBias, 0.0001f, 0.02f, "%.4f")) isSceneDirty_ = true;
			if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
			ImGui::SetItemTooltip(Tr("Depth bias offset to prevent shadow acne (self-shadow artifacts)"));

			if (ImGui::SliderFloat(TrId("Shadow Strength"), &obj.lightShadowStrength, 0.0f, 1.0f, "%.2f")) isSceneDirty_ = true;
			if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
			ImGui::SetItemTooltip(Tr("Intensity of the shadows cast by this light (1.0 = pitch black shadows, 0.0 = no shadow attenuation)"));
		} else {
			if (ImGui::DragFloat(obj.lightType == GameObject::LightType::Point ? "Radius (Max Dist)" : "Distance", &obj.lightRange, 0.2f, 0.1f, 200.0f)) isSceneDirty_ = true;
			if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
			ImGui::SetItemTooltip(Tr("Maximum illumination radius / distance"));

			if (ImGui::DragFloat(TrId("Decay"), &obj.lightDecay, 0.1f, 0.0f, 10.0f)) isSceneDirty_ = true;
			if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
			ImGui::SetItemTooltip(Tr("Attenuation decay exponent"));

			if (obj.lightType == GameObject::LightType::Spot) {
				if (DrawVec3Control(TrId("Direction"), obj.lightDirection, 0.0f)) {
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
				ImGui::SetItemTooltip(Tr("Direction vector of spot cone (linked with Transform Rotation)"));
				if (ImGui::SliderFloat(TrId("Spot Angle"), &obj.lightSpotAngle, 1.0f, 90.0f)) isSceneDirty_ = true;
				if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
				ImGui::SetItemTooltip(Tr("Cone half-angle of spot light in degrees"));

				if (ImGui::SliderFloat(TrId("Falloff Start Angle"), &obj.lightSpotFalloffStart, 1.0f, obj.lightSpotAngle)) isSceneDirty_ = true;
				if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
			}
		}
	}
#else
	(void)obj;
#endif
}

void EditorSuite::DrawAudioSourceComponentInspector(GameObject& obj)
{
#ifdef USE_IMGUI
	obj.hasAudioSource = true;
	if (DrawComponentHeader("Audio Source (XAudio2 + Media Foundation)", ComponentKind::AudioSource)) {
		// Discover available audio files in Resources/Sound and Assets/Audio
		std::vector<std::string> audioClips;
		audioClips.push_back("None");
		std::vector<std::string> soundDirs = { "Resources/Sound", "Resources", "Assets/Audio", "Assets" };
		for (const auto& dir : soundDirs) {
			std::error_code ec;
			if (std::filesystem::exists(dir, ec) && std::filesystem::is_directory(dir, ec)) {
				for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
					if (!entry.is_regular_file(ec)) continue;
					std::string ext = entry.path().extension().string();
					std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					if (ext == ".wav" || ext == ".mp3" || ext == ".m4a" || ext == ".wma") {
						std::string relPath = entry.path().generic_string();
						if (std::find(audioClips.begin(), audioClips.end(), relPath) == audioClips.end()) {
							audioClips.push_back(relPath);
						}
					}
				}
			}
		}

		int curClipIdx = 0;
		for (size_t cIdx = 0; cIdx < audioClips.size(); ++cIdx) {
			if (audioClips[cIdx] == obj.audioClipName) {
				curClipIdx = static_cast<int>(cIdx);
				break;
			}
		}

		std::vector<const char*> clipPtrs;
		for (const auto& s : audioClips) clipPtrs.push_back(s.c_str());

		if (ImGui::Combo(TrId("Audio ClipPreset"), &curClipIdx, clipPtrs.data(), static_cast<int>(clipPtrs.size()))) {
			obj.audioClipName = audioClips[curClipIdx];
			if (obj.audioClipName != "None" && !obj.audioClipName.empty()) {
				obj.runtimeSoundHandle = Audio::GetInstance()->LoadAudio(ResolveAssetPath(obj.audioClipName));
			}
			isSceneDirty_ = true;
			PushUndoSnapshot();
		}
		ImGui::SetItemTooltip(Tr("Select audio file (.wav, .mp3, .m4a, .wma) decoded via Media Foundation & cached in Audio::LoadAudio"));

		char clipBuf[256] = {};
		strncpy_s(clipBuf, obj.audioClipName.c_str(), sizeof(clipBuf) - 1);
		if (ImGui::InputText(TrId("Clip Path"), clipBuf, sizeof(clipBuf))) {
			obj.audioClipName = clipBuf;
			isSceneDirty_ = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			if (obj.audioClipName != "None" && !obj.audioClipName.empty()) {
				obj.runtimeSoundHandle = Audio::GetInstance()->LoadAudio(ResolveAssetPath(obj.audioClipName));
			}
			PushUndoSnapshot();
		}

		if (ImGui::SliderFloat(TrId("Volume"), &obj.audioVolume, 0.0f, 1.0f, "%.2f")) {
			if (obj.runtimeVoiceHandle != 0) {
				Audio::GetInstance()->SetVolume(obj.runtimeVoiceHandle, obj.audioVolume);
			}
			isSceneDirty_ = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();

		if (ImGui::Checkbox(TrId("Play On Awake"), &obj.audioPlayOnAwake)) {
			isSceneDirty_ = true;
			PushUndoSnapshot();
		}
		ImGui::SameLine();
		if (ImGui::Checkbox(TrId("Loop##Audio"), &obj.audioLoop)) {
			isSceneDirty_ = true;
			PushUndoSnapshot();
		}

		if (ImGui::SliderFloat(TrId("Pitch"), &obj.audioPitch, 0.25f, 4.0f, "%.2f")) {
			if (obj.runtimeVoiceHandle != 0) {
				Audio::GetInstance()->SetVoicePitch(obj.runtimeVoiceHandle, obj.audioPitch);
			}
			isSceneDirty_ = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();

		const std::string busNames = std::string(Tr("Effects")) + '\0' + Tr("Music") + '\0' + Tr("Voice") + '\0' + Tr("UI") + '\0';
		if (ImGui::Combo(TrId("Bus"), &obj.audioBus, busNames.c_str())) {
			isSceneDirty_ = true;
			PushUndoSnapshot();
		}

		if (ImGui::Checkbox(TrId("3D Sound"), &obj.audioSpatial)) {
			isSceneDirty_ = true;
			PushUndoSnapshot();
		}
		ImGui::SetItemTooltip("%s", Tr("Pans with the game camera and fades with distance during Play"));
		if (obj.audioSpatial) {
			if (ImGui::DragFloat(TrId("Min Distance"), &obj.audioMinDistance, 0.1f, 0.01f, 10000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
				obj.audioMaxDistance = (std::max)(obj.audioMaxDistance, obj.audioMinDistance + 0.01f);
				isSceneDirty_ = true;
			}
			if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
			if (ImGui::DragFloat(TrId("Max Distance"), &obj.audioMaxDistance, 0.1f, obj.audioMinDistance + 0.01f, 10000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
				isSceneDirty_ = true;
			}
			if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoSnapshot();
		}

		bool isPlayingNow = (obj.runtimeVoiceHandle != 0) && Audio::GetInstance()->IsPlaying(obj.runtimeVoiceHandle);
		if (isPlayingNow) {
			ImGui::TextColored(ImVec4(0.3f, 0.95f, 0.45f, 1.0f), Tr("Status: Playing (Voice #%u)"), obj.runtimeVoiceHandle);
		} else {
			ImGui::TextDisabled(Tr("Status: Stopped"));
		}

		if (ImGui::Button(TrId("[ > Play Preview ]"), ImVec2(135.0f, 24.0f))) {
			StartAudioSource(obj, true);
		}
		ImGui::SameLine();
		if (ImGui::Button(TrId("[ [] Stop ]##AudioStop"), ImVec2(100.0f, 24.0f))) {
			if (obj.runtimeVoiceHandle != 0) {
				Audio::GetInstance()->StopAudio(obj.runtimeVoiceHandle);
				obj.runtimeVoiceHandle = 0;
			}
		}
	}
#else
	(void)obj;
#endif
}
