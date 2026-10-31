#include "EditorSuiteInternal.h"

void EditorSuite::DrawInspectorAndDetailsPanel(bool& showParticles)
{
#ifdef USE_IMGUI
	ImGuiIO& io = ImGui::GetIO();
	float screenW = io.DisplaySize.x;
	float screenH = io.DisplaySize.y;
	float topY = ImGui::GetFrameHeight() + kTopBarHeight;
	float rightW = screenW * 0.24f;
	if (rightW < 300.0f) rightW = 300.0f;
	float midH = (screenH - topY) * 0.65f;

	ImGui::SetNextWindowPos(ImVec2(screenW - rightW, topY));
	ImGui::SetNextWindowSize(ImVec2(rightW, midH));
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;

	if (ImGui::Begin(TrId("Inspector"), nullptr, flags)) {
		if (ImGui::BeginTabBar("InspectorTabs")) {
			// TAB 1: INSPECTOR
			if (ImGui::BeginTabItem(TrId("Inspector"))) {
				rightPanelTab_ = 0;
				if (selectedObjectIndex_ < 0 || selectedObjectIndex_ >= static_cast<int>(gameObjects_.size())) {
					ImGui::TextDisabled("No GameObject Selected");
					ImGui::Separator();
					DrawSkyboxSettingsUI();
					DrawPostProcessSettingsUI();
					ImGui::EndTabItem();
					ImGui::EndTabBar();
					ImGui::End();
					return;
				}

				auto& obj = gameObjects_[selectedObjectIndex_];

				// Widgets take ~60% of the width so right-hand labels (e.g. "Geometry Shader Effect") are not clipped
				const float inspectorLabelW = (std::max)(120.0f, ImGui::GetContentRegionAvail().x * 0.40f);
				ImGui::PushItemWidth(-inspectorLabelW);

				// Header: Active Checkbox + Object Name + Static
				if (ImGui::Checkbox("##Active", &obj.isActive)) {
					isSceneDirty_ = true;
					PushUndoSnapshot();
				}
				ImGui::SameLine();
				char nameBuf[128]{};
				strncpy_s(nameBuf, obj.name.c_str(), sizeof(nameBuf) - 1);
				const float staticCheckboxW = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize("Static").x;
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - staticCheckboxW - ImGui::GetStyle().ItemSpacing.x);
				if (ImGui::InputText("##ObjName", nameBuf, sizeof(nameBuf))) {
					obj.name = nameBuf;
					isSceneDirty_ = true;
				}
				if (ImGui::IsItemDeactivatedAfterEdit()) {
					PushUndoSnapshot();
				}
				ImGui::SameLine();
				if (ImGui::Checkbox("Static", &obj.isStatic)) {
					isSceneDirty_ = true;
					PushUndoSnapshot();
				}

				// Tag + collision layer / mask (layer names match ScriptLayer in ScriptAPI.h)
				{
					char tagBuf[64]{};
					strncpy_s(tagBuf, obj.tag.c_str(), sizeof(tagBuf) - 1);
					if (ImGui::InputText("Tag", tagBuf, sizeof(tagBuf))) {
						obj.tag = tagBuf;
						isSceneDirty_ = true;
					}
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						PushUndoSnapshot();
					}
					ImGui::SetItemTooltip("Free-form tag scripts can compare (e.g. \"Enemy\")");

					const uint32_t layer = obj.layer & 31u;
					if (ImGui::BeginCombo("Layer", GetCollisionLayerName(layer).c_str())) {
						for (uint32_t i = 0; i < 32; ++i) {
							if (ImGui::Selectable(GetCollisionLayerName(i).c_str(), i == layer)) {
								obj.layer = i;
								isSceneDirty_ = true;
								PushUndoSnapshot();
							}
						}
						ImGui::EndCombo();
					}
					ImGui::SetItemTooltip("%s", Tr("Collision layer of this object's colliders"));

					std::string maskPreview;
					if (obj.collisionMask == 0xFFFFFFFFu) {
						maskPreview = "Everything";
					} else if (obj.collisionMask == 0u) {
						maskPreview = "Nothing";
					} else {
						for (uint32_t i = 0; i < 32; ++i) {
							if (obj.collisionMask & (1u << i)) {
								if (!maskPreview.empty()) maskPreview += ", ";
								maskPreview += GetCollisionLayerName(i);
							}
						}
					}
					if (ImGui::BeginCombo("Collides With", maskPreview.c_str())) {
						if (ImGui::Selectable("Everything")) { obj.collisionMask = 0xFFFFFFFFu; isSceneDirty_ = true; PushUndoSnapshot(); }
						if (ImGui::Selectable("Nothing")) { obj.collisionMask = 0u; isSceneDirty_ = true; PushUndoSnapshot(); }
						ImGui::Separator();
						for (uint32_t i = 0; i < 32; ++i) {
							bool on = (obj.collisionMask & (1u << i)) != 0;
							if (ImGui::Checkbox(GetCollisionLayerName(i).c_str(), &on)) {
								obj.collisionMask = on ? (obj.collisionMask | (1u << i)) : (obj.collisionMask & ~(1u << i));
								isSceneDirty_ = true;
								PushUndoSnapshot();
							}
						}
						ImGui::EndCombo();
					}
					ImGui::SetItemTooltip("%s", Tr("Layers this object collides with. A pair collides only if both masks accept each other's layer."));
				}

				// 1. TRANSFORM COMPONENT (Always Present)
				if (ImGui::CollapsingHeader(TrId("Transform"), ImGuiTreeNodeFlags_DefaultOpen)) {
					DrawVec3Control("Position", obj.translation, 0.0f);
					ImGui::SetItemTooltip("%s", Tr("World position (X, Y, Z)"));

					if (DrawVec3Control("Rotation", obj.rotation, 0.0f)) {
						obj.rotationQuat = Quaternion::FromEuler(obj.rotation);
					}
					ImGui::SetItemTooltip("%s", Tr("Euler rotation in radians (X, Y, Z)"));

					DrawVec3Control("Scale", obj.scale, 1.0f);
					ImGui::SetItemTooltip("%s", Tr("Local scale vector (X, Y, Z)"));

					// Debug-only readouts stay out of the way unless expanded
					if (ImGui::TreeNodeEx("Debug##TransformDebug", ImGuiTreeNodeFlags_SpanAvailWidth)) {
						float qVal[4] = { obj.rotationQuat.x, obj.rotationQuat.y, obj.rotationQuat.z, obj.rotationQuat.w };
						ImGui::InputFloat4("Quaternion", qVal, "%.4f", ImGuiInputTextFlags_ReadOnly);
						ImGui::SetItemTooltip("%s", Tr("Live unit quaternion (X, Y, Z, W)"));

						if (ImGui::Button(TrId("Test 90-deg Roll Slerp"), ImVec2(-1.0f, 0.0f))) {
							Quaternion target = Quaternion::FromAxisAngle({ 0, 0, 1 }, 3.14159265f / 2.0f);
							obj.rotationQuat = Quaternion::Slerp(obj.rotationQuat, target, 0.5f);
							obj.rotation = obj.rotationQuat.ToEuler();
							isSceneDirty_ = true;
							PushUndoSnapshot();
						}
						ImGui::SetItemTooltip("%s", Tr("Interpolate orientation using spherical linear interpolation (Slerp)"));
						ImGui::TreePop();
					}
				}

				// 2..9. Built-in and data components, in registry order (EditorSuite_Components.cpp)
				DrawComponentInspectors(obj);

				// 10. SCRIPT COMPONENTS
				EnsureEditModeScriptInstances();
				for (size_t sIdx = 0; sIdx < obj.scripts.size(); ) {
					ImGui::PushID(static_cast<int>(sIdx));
					std::string headerLabel = std::format("{} (Script)##ScriptHeader_{}", obj.scripts[sIdx].className, sIdx);
					bool open = ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
					bool removeScript = false;
					if (ImGui::BeginPopupContextItem("ScriptCompCtx")) {
						if (ImGui::MenuItem(TrId("Open Script (.cpp)"))) {
							std::filesystem::path cppPath = ResolveGameScriptsDir() / "Scripts" / (obj.scripts[sIdx].className + ".cpp");
							OpenFileInVisualStudio(cppPath);
						}
						if (ImGui::MenuItem(TrId("Open Header (.h)"))) {
							std::filesystem::path hPath = ResolveGameScriptsDir() / "Scripts" / (obj.scripts[sIdx].className + ".h");
							OpenFileInVisualStudio(hPath);
						}
						ImGui::Separator();
						if (ImGui::MenuItem(TrId("Remove Component"))) {
							removeScript = true;
						}
						ImGui::EndPopup();
					}
					if (open) {
						ImGui::Indent(8.0f);
						auto& sc = obj.scripts[sIdx];
						ImGui::Text("Script Class: %s", sc.className.c_str());
						bool isReg = scriptModule_.IsRegistered(sc.className);
						if (sc.isCompiling || (isBuildingScripts_.load() && !isReg)) {
							ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "Status: Compiling...");
						} else if (!isReg || sc.isMissingScript) {
							ImGui::TextColored(ImVec4(1.0f, 0.40f, 0.25f, 1.0f), "Status: Missing Script (Click [ Build Scripts ] or fix class)");
						} else if (sceneState_ == EditorSceneState::Play || sceneState_ == EditorSceneState::Pause) {
							ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "Status: Active (Running in Play Mode)");
						} else {
							ImGui::TextDisabled("Status: Ready (GameScripts.dll)");
						}

						// [ Open Script ] and [ .h ] buttons on the same line, plus [ Remove ]
						float availW = ImGui::GetContentRegionAvail().x;
						float openW = (std::max)(90.0f, availW - 135.0f);
						if (ImGui::Button(TrId("[ Open Script ]"), ImVec2(openW, 24.0f))) {
							std::filesystem::path cppPath = ResolveGameScriptsDir() / "Scripts" / (sc.className + ".cpp");
							OpenFileInVisualStudio(cppPath);
						}
						ImGui::SetItemTooltip("Open %s.cpp in Visual Studio", sc.className.c_str());

						ImGui::SameLine();
						if (ImGui::Button(TrId("[ .h ]"), ImVec2(45.0f, 24.0f))) {
							std::filesystem::path hPath = ResolveGameScriptsDir() / "Scripts" / (sc.className + ".h");
							OpenFileInVisualStudio(hPath);
						}
						ImGui::SetItemTooltip("Open %s.h in Visual Studio", sc.className.c_str());

						ImGui::SameLine();
						if (ImGui::Button(TrId("Remove"), ImVec2(75.0f, 24.0f))) {
							removeScript = true;
						}
						ImGui::SetItemTooltip("%s", Tr("Remove this Script component from GameObject"));

						// Exposed Fields UI (EXPOSE_FIELD reflection)
						if (sc.instance && isReg) {
							int fieldCount = scriptModule_.GetFieldCount(sc.instance.get());
							if (fieldCount > 0) {
								ImGui::Spacing();
								ImGui::Separator();
								ImGui::TextDisabled("Exposed Fields:");
								for (int fIdx = 0; fIdx < fieldCount; ++fIdx) {
									ScriptFieldInfo info{};
									if (!scriptModule_.GetFieldInfo(sc.instance.get(), fIdx, info) || !info.dataPtr || !info.name || info.name[0] == '\0') continue;

									ImGui::PushID(fIdx);
									bool fieldEdited = false;
									bool fieldCommitted = false;

									switch (info.type) {
									case ScriptFieldType::Float: {
										float* ptr = static_cast<float*>(info.dataPtr);
										if (ImGui::DragFloat(info.name, ptr, 0.05f, 0.0f, 0.0f, "%.3f")) {
											fieldEdited = true;
										}
										if (ImGui::IsItemDeactivatedAfterEdit()) {
											fieldCommitted = true;
										}
										break;
									}
									case ScriptFieldType::Int: {
										int* ptr = static_cast<int*>(info.dataPtr);
										if (ImGui::DragInt(info.name, ptr, 1.0f)) {
											fieldEdited = true;
										}
										if (ImGui::IsItemDeactivatedAfterEdit()) {
											fieldCommitted = true;
										}
										break;
									}
									case ScriptFieldType::Bool: {
										bool* ptr = static_cast<bool*>(info.dataPtr);
										if (ImGui::Checkbox(info.name, ptr)) {
											fieldEdited = true;
											fieldCommitted = true;
										}
										break;
									}
									case ScriptFieldType::Vec3: {
										Vector3* ptr = static_cast<Vector3*>(info.dataPtr);
										if (DrawVec3Control(info.name, *ptr, 0.0f)) {
											fieldEdited = true;
											fieldCommitted = true;
										}
										break;
									}
									case ScriptFieldType::CharBuffer: {
										char* ptr = static_cast<char*>(info.dataPtr);
										if (ImGui::InputText(info.name, ptr, info.sizeInBytes)) {
											fieldEdited = true;
										}
										if (ImGui::IsItemDeactivatedAfterEdit()) {
											fieldCommitted = true;
										}
										break;
									}
									}

									if (fieldEdited || fieldCommitted) {
										CaptureScriptFieldsToSaved(sc);
										isSceneDirty_ = true;
									}
									if (fieldCommitted) {
										PushUndoSnapshot();
									}
									ImGui::PopID();
								}
							}
						}

						ImGui::Unindent(8.0f);
					}
					ImGui::PopID();

					if (removeScript) {
						if (obj.scripts[sIdx].instance) {
							if (sceneState_ == EditorSceneState::Play || sceneState_ == EditorSceneState::Pause || IsStandaloneMode()) {
								obj.scripts[sIdx].instance->SetOwner(GetOrUpdateScriptViewById(obj.id), obj.id);
								obj.scripts[sIdx].instance->SetEngineAPI(&engineApi_);
								obj.scripts[sIdx].instance->SetHost(this, this);
								ScriptBase* instance = obj.scripts[sIdx].instance.get();
								CallScript(instance, obj.id, "OnDestroy", [&] { instance->OnDestroy(); });
							}
							obj.scripts[sIdx].instance.reset();
						}
						obj.scripts.erase(obj.scripts.begin() + static_cast<std::ptrdiff_t>(sIdx));
						obj.hasScript = !obj.scripts.empty();
						obj.scriptClassName = obj.scripts.empty() ? "" : obj.scripts.front().className;
						isSceneDirty_ = true;
						PushUndoSnapshot();
					} else {
						++sIdx;
					}
				}

				// Centered [ Add Component ] Button
				ImGui::Spacing();
				ImGui::Separator();
				float btnWidth = 180.0f;
				float availWidth = ImGui::GetContentRegionAvail().x;
				ImGui::SetCursorPosX((availWidth - btnWidth) * 0.5f);
				if (ImGui::Button(TrId("Add Component"), ImVec2(btnWidth, 28.0f))) {
					ImGui::OpenPopup("AddComponentPopup");
				}

				if (ImGui::BeginPopup("AddComponentPopup")) {
					DrawAddComponentMenuItems(obj);
					ImGui::Separator();
					if (ImGui::BeginMenu(TrId("Script"))) {
						const auto& registeredScripts = scriptModule_.GetRegisteredNames();
						if (registeredScripts.empty()) {
							ImGui::TextDisabled("No registered scripts in GameScripts.dll");
						} else {
							for (const auto& scriptName : registeredScripts) {
								if (ImGui::MenuItem(scriptName.c_str())) {
									GameObject::ScriptComponent sc(scriptName);
									sc.instance = ScriptInstancePtr(scriptModule_.CreateScript(scriptName), ScriptInstanceDeleter{ &scriptModule_ });
									if (sc.instance) {
										RefreshAllScriptObjectViews();
										ScriptGameObjectView* view = GetOrUpdateScriptViewById(obj.id);
										sc.instance->SetOwner(view, obj.id);
										sc.instance->SetEngineAPI(&engineApi_);
										sc.instance->SetHost(this, this);
										CaptureScriptFieldsToSaved(sc);
										if (sceneState_ == EditorSceneState::Play || sceneState_ == EditorSceneState::Pause || IsStandaloneMode()) {
											sc.instance->Initialize();
											if (view && view->dirtyFromScript) {
												SyncViewToGameObject(*view, obj);
											}
										}
									}
									obj.scripts.push_back(std::move(sc));
									obj.hasScript = true;
									obj.scriptClassName = obj.scripts.front().className;
									isSceneDirty_ = true;
									PushUndoSnapshot();
								}
							}
						}
						ImGui::Separator();
						if (ImGui::MenuItem(TrId("New Script..."))) {
							scriptGenTargetObjectIndex_ = selectedObjectIndex_;
							scriptGenErrorMsg_.clear();
							showScriptGenModal_ = true;
						}
						ImGui::EndMenu();
					}
					ImGui::EndPopup();
				}

				// Apply a "Remove Component" request now that nothing else reads this object's component data
				if (pendingComponentRemoval_ >= 0) {
					if (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < static_cast<int>(gameObjects_.size())) {
						RemoveComponent(gameObjects_[selectedObjectIndex_], static_cast<ComponentKind>(pendingComponentRemoval_));
					}
					pendingComponentRemoval_ = -1;
				}
				if (!pendingDataComponentRemoval_.empty()) {
					if (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < static_cast<int>(gameObjects_.size())) {
						RemoveComponentByName(gameObjects_[selectedObjectIndex_], pendingDataComponentRemoval_);
					}
					pendingDataComponentRemoval_.clear();
				}

				ImGui::PopItemWidth();
				ImGui::EndTabItem();
			}

			// DETAILS TAB (Live Asset Previews & Engine Statistics)
			ImGuiTabItemFlags detailsTabFlags = focusDetailsTab_ ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
			if (ImGui::BeginTabItem(TrId("Details"), nullptr, detailsTabFlags)) {
				focusDetailsTab_ = false;
				rightPanelTab_ = 1;
				ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "Asset & Scene Details");
				ImGui::Separator();

				if (!selectedAssetPath_.empty() && std::filesystem::exists(selectedAssetPath_) && !std::filesystem::is_directory(selectedAssetPath_)) {
					std::string ext = selectedAssetPath_.extension().string();
					std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

					ImGui::Text("Selected Asset: %s", selectedAssetPath_.filename().string().c_str());
					ImGui::TextDisabled("Path: %s", selectedAssetPath_.string().c_str());

					try {
						auto sizeBytes = std::filesystem::file_size(selectedAssetPath_);
						if (sizeBytes < 1024) {
							ImGui::Text("File Size: %llu Bytes (%.2f KB)", static_cast<unsigned long long>(sizeBytes), sizeBytes / 1024.0f);
						} else {
							ImGui::Text("File Size: %.2f KB (%llu Bytes)", sizeBytes / 1024.0f, static_cast<unsigned long long>(sizeBytes));
						}
					} catch (...) {}

					if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".dds") {
						ImGui::Separator();
						ImGui::Text("2D Texture Preview:");
						if (currentPreviewTextureHandle_ == 0 && currentPreviewAssetPath_ != selectedAssetPath_.string()) {
							SelectAssetForPreview(selectedAssetPath_);
						}
						if (currentPreviewTextureHandle_ != 0) {
							ImGui::Image(
								ImTextureRef((ImTextureID)TextureManager::GetInstance()->GetGPUHandle(currentPreviewTextureHandle_).ptr),
								ImVec2(140.0f, 140.0f));
						}

						if (ImGui::Button(TrId("[ Create Textured Rectangle in Scene ]"), ImVec2(-1.0f, 26.0f))) {
							uint32_t permHandle = TextureManager::GetInstance()->LoadTexture(selectedAssetPath_.string());
							auto& q = CreateQuad(selectedAssetPath_.stem().string(), { 0.0f, 1.0f, 0.0f });
							q.texturePath = ToProjectRelativePath(selectedAssetPath_);
							q.textureHandle = permHandle;
							isSceneDirty_ = true;
							PushUndoSnapshot();
						}
						if (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < static_cast<int>(gameObjects_.size())) {
							if (ImGui::Button(TrId("[ Assign to Selected Object Material ]"), ImVec2(-1.0f, 26.0f))) {
								uint32_t permHandle = TextureManager::GetInstance()->LoadTexture(selectedAssetPath_.string());
								gameObjects_[selectedObjectIndex_].texturePath = ToProjectRelativePath(selectedAssetPath_);
								gameObjects_[selectedObjectIndex_].textureHandle = permHandle;
								isSceneDirty_ = true;
								PushUndoSnapshot();
							}
						}
					} else if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb") {
						ImGui::Separator();
						ImGui::Text("3D Model Preview (Assimp):");
						if (!previewModel_ && currentPreviewAssetPath_ != selectedAssetPath_.string()) {
							SelectAssetForPreview(selectedAssetPath_);
						}
						ImGui::Text("Format: %s", ext.c_str());
						ImGui::Text("Vertices: %u  |  Triangles: %u", previewVertexCount_, previewTriangleCount_);
						if (previewModel_) {
							ImGui::Image(
								ImTextureRef((ImTextureID)modelPreviewRT_.GetSrvGPUHandle().ptr),
								ImVec2(180.0f, 180.0f));
						} else {
							ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "Unable to parse 3D geometry.");
						}
						if (ImGui::Button(TrId("[ Spawn 3D Model in Scene ]"), ImVec2(-1.0f, 26.0f))) {
							auto& obj = CreateGameObject(selectedAssetPath_.stem().string(), GameObjectType::Cube);
							obj.hasMesh = true;
							obj.meshEnabled = true;
							obj.meshType = GameObject::MeshType::CustomModel;
							obj.modelPath = ToProjectRelativePath(selectedAssetPath_);
							obj.translation = { 0.0f, 1.0f, 0.0f };
							if (obj.hasBoxCollider) FitBoxColliderToMesh(obj); // the cube's 1x1x1 box would not fit the model
							isSceneDirty_ = true;
							PushUndoSnapshot();
							LogMessage(std::format("[Scene] Spawned 3D Model '{}' from '{}'", obj.name, obj.modelPath), 0);
						}
					} else if (ext == ".wav" || ext == ".mp3" || ext == ".m4a" || ext == ".wma") {
						ImGui::Separator();
						ImGui::Text("Audio Clip Preview (XAudio2 + Media Foundation):");
						if (ImGui::Button(TrId("[ > Play Audio Clip ]"), ImVec2(-1.0f, 26.0f))) {
							uint32_t sh = Audio::GetInstance()->LoadAudio(selectedAssetPath_.string());
							Audio::GetInstance()->PlayAudio(sh, false, 1.0f);
						}
						if (ImGui::Button(TrId("[ [] Stop All Audio ]"), ImVec2(-1.0f, 26.0f))) {
							Audio::GetInstance()->StopAllAudio();
						}
						if (ImGui::Button(TrId("[ Create Audio Source in Scene ]"), ImVec2(-1.0f, 26.0f))) {
							auto& aud = CreateGameObject(selectedAssetPath_.stem().string(), GameObjectType::AudioSource);
							aud.hasAudioSource = true;
							aud.audioClipName = ToProjectRelativePath(selectedAssetPath_);
							aud.audioPlayOnAwake = true;
							aud.runtimeSoundHandle = Audio::GetInstance()->LoadAudio(ResolveAssetPath(aud.audioClipName));
							isSceneDirty_ = true;
							PushUndoSnapshot();
						}
					} else if (ext == ".cpp" || ext == ".h" || ext == ".hpp") {
						ImGui::Separator();
						ImGui::Text("C++ Script Source File:");
						if (ImGui::Button(TrId("[ Open in Visual Studio ]"), ImVec2(-1.0f, 26.0f))) {
							OpenFileInVisualStudio(selectedAssetPath_);
						}
					}
					ImGui::Separator();
				} else {
					ImGui::TextDisabled("Select a texture (.png, .jpg, .dds) or 3D model (.obj, .fbx, .gltf) in the Project browser to inspect.");
					ImGui::Separator();
				}

				ImGui::Text("Total GameObjects: %zu", gameObjects_.size());
				ImGui::Text("Active Cameras: %s", gameCamera_ ? "1" : "0");
				ImGui::Text("Lighting: %s", enableLighting_ ? "Enabled" : "Disabled");
				ImGui::Checkbox("Frustum Culling", &enableFrustumCulling_);
				ImGui::Text("Frustum Culling Stats: %d Visible / %d Culled", lastVisibleObjects_, lastCulledObjects_);

				ImGui::EndTabItem();
			} else {
				focusDetailsTab_ = false;
			}

			ImGui::EndTabBar();
		}
	}
	ImGui::End();
#endif
}

bool EditorSuite::DrawVec3Control(const std::string& label, Vector3& values, float resetValue, float columnWidth)
{
	bool modified = false;
#ifdef USE_IMGUI
	ImGui::PushID(label.c_str());

	if (ImGui::BeginTable("Vec3ControlTable", 2, ImGuiTableFlags_SizingStretchProp)) {
		ImGui::TableSetupColumn("LabelCol", ImGuiTableColumnFlags_WidthFixed, columnWidth);
		ImGui::TableSetupColumn("ControlCol", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableNextRow();

		ImGui::TableNextColumn();
		ImGui::TextUnformatted(label.c_str());

		ImGui::TableNextColumn();
		float availWidth = ImGui::GetContentRegionAvail().x;
		float itemWidth = (availWidth - 54.0f) / 3.0f;
		ImVec2 buttonSize = ImVec2(18.0f, 0.0f);

		// X Axis (Red)
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.15f, 0.15f, 1.0f));
		if (ImGui::Button(TrId("X"), buttonSize)) {
			values.x = resetValue;
			modified = true;
			isSceneDirty_ = true;
			PushUndoSnapshot();
		}
		ImGui::PopStyleColor();
		ImGui::SameLine(0.0f, 2.0f);
		ImGui::SetNextItemWidth(itemWidth);
		if (ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f")) {
			modified = true;
			isSceneDirty_ = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			PushUndoSnapshot();
		}
		ImGui::SameLine(0.0f, 4.0f);

		// Y Axis (Green)
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.65f, 0.20f, 1.0f));
		if (ImGui::Button(TrId("Y"), buttonSize)) {
			values.y = resetValue;
			modified = true;
			isSceneDirty_ = true;
			PushUndoSnapshot();
		}
		ImGui::PopStyleColor();
		ImGui::SameLine(0.0f, 2.0f);
		ImGui::SetNextItemWidth(itemWidth);
		if (ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f")) {
			modified = true;
			isSceneDirty_ = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			PushUndoSnapshot();
		}
		ImGui::SameLine(0.0f, 4.0f);

		// Z Axis (Blue)
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.35f, 0.78f, 1.0f));
		if (ImGui::Button(TrId("Z"), buttonSize)) {
			values.z = resetValue;
			modified = true;
			isSceneDirty_ = true;
			PushUndoSnapshot();
		}
		ImGui::PopStyleColor();
		ImGui::SameLine(0.0f, 2.0f);
		ImGui::SetNextItemWidth(itemWidth);
		if (ImGui::DragFloat("##Z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f")) {
			modified = true;
			isSceneDirty_ = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			PushUndoSnapshot();
		}

		ImGui::EndTable();
	}

	ImGui::PopID();
#endif
	return modified;
}


bool EditorSuite::DrawComponentHeader(const char* label, ComponentKind kind)
{
#ifdef USE_IMGUI
	bool keep = true;
	// Passing &keep adds the close (x) button on the right side of the header
	const bool open = ImGui::CollapsingHeader(TrId(label), &keep, ImGuiTreeNodeFlags_DefaultOpen);
	ImGui::SetItemTooltip("%s", Tr("Right-click or press x to remove this component"));
	if (ImGui::BeginPopupContextItem()) {
		if (ImGui::MenuItem(TrId("Remove Component"))) {
			keep = false;
		}
		ImGui::EndPopup();
	}
	if (!keep) {
		pendingComponentRemoval_ = static_cast<int>(kind);
	}
	return open && keep;
#else
	(void)label;
	(void)kind;
	return false;
#endif
}

void EditorSuite::RemoveComponent(GameObject& obj, ComponentKind kind)
{
	// Objects created from a typed menu entry (Camera, Light, ...) are recognised by their type in several
	// places (gizmos, camera sync, audio on Play), so removing that component also makes the object Empty.
	auto clearTypeIf = [&](std::initializer_list<GameObjectType> types) {
		for (GameObjectType t : types) {
			if (obj.type == t) {
				obj.type = GameObjectType::Empty;
				return;
			}
		}
	};

	const char* name = "Component";
	switch (kind) {
	case ComponentKind::Mesh:
		name = "Mesh Filter & Renderer";
		obj.hasMesh = false;
		obj.editObject3d.reset();
		obj.gameObject3d.reset();
		obj.shadowTransform.reset();
		clearTypeIf({ GameObjectType::Cube, GameObjectType::Sphere, GameObjectType::Plane, GameObjectType::Quad });
		break;
	case ComponentKind::BoxCollider:
		name = "Box Collider";
		obj.hasBoxCollider = false;
		break;
	case ComponentKind::SphereCollider:
		name = "Sphere Collider";
		obj.hasSphereCollider = false;
		break;
	case ComponentKind::Rigidbody:
		name = "Rigidbody";
		obj.hasRigidbody = false;
		obj.velocity = { 0.0f, 0.0f, 0.0f };
		obj.isGrounded = false;
		break;
	case ComponentKind::Camera:
		name = "Camera";
		obj.hasCamera = false;
		clearTypeIf({ GameObjectType::Camera });
		break;
	case ComponentKind::Light:
		name = "Light";
		obj.hasLight = false;
		clearTypeIf({ GameObjectType::DirectionalLight, GameObjectType::PointLight, GameObjectType::SpotLight });
		break;
	case ComponentKind::ParticleSystem:
		name = "Particle System";
		obj.hasParticleSystem = false;
		obj.particleQuad.reset(); // GPU resources go through DeferRelease in ~ParticleQuad
		obj.attachedParentId = -1;
		obj.attachToIndex = -1;
		clearTypeIf({ GameObjectType::ParticleSystem });
		break;
	case ComponentKind::AudioSource:
		name = "Audio Source";
		if (obj.runtimeVoiceHandle != 0) {
			Audio::GetInstance()->StopAudio(obj.runtimeVoiceHandle);
			obj.runtimeVoiceHandle = 0;
		}
		obj.hasAudioSource = false;
		clearTypeIf({ GameObjectType::AudioSource });
		break;
	case ComponentKind::Mapchip:
		name = "Mapchip Level Editor";
		obj.hasMapchip = false;
		clearTypeIf({ GameObjectType::MapchipGrid });
		break;
	}

	isSceneDirty_ = true;
	PushUndoSnapshot();
	LogMessage(std::format("[Inspector] Removed {} from '{}'.", name, obj.name), 0);
}
