#include "EditorSuiteInternal.h"

using EditorSuiteDetail::SceneNameFromPath;
using EditorSuiteDetail::IsSceneFile;

void EditorSuite::SelectAssetForPreview(const std::filesystem::path& assetPath)
{
	if (!dxCommon_ || dxCommon_->IsDeviceLost()) {
		return;
	}

	if (currentPreviewAssetPath_ == assetPath.string() && (currentPreviewTextureHandle_ != 0 || previewModel_ != nullptr)) {
		selectedAssetPath_ = assetPath;
		focusDetailsTab_ = true;
		return;
	}

	ReleasePreviewResources();
	selectedAssetPath_ = assetPath;
	currentPreviewAssetPath_ = assetPath.string();

	if (assetPath.empty() || !std::filesystem::exists(assetPath) || std::filesystem::is_directory(assetPath)) {
		return;
	}

	std::string ext = assetPath.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

	if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".dds") {
		currentPreviewTextureHandle_ = TextureManager::GetInstance()->LoadPreviewTexture(assetPath.string());
		focusDetailsTab_ = true;
	} else if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb") {
		previewModel_ = Model::CreatePreviewFromFile(assetPath.string());
		if (previewModel_) {
			previewVertexCount_ = previewModel_->GetTotalVertexCount();
			previewTriangleCount_ = previewModel_->GetTotalTriangleCount();
			Vector3 center = { 0.0f, 0.0f, 0.0f };
			float maxExtent = 2.0f;
			previewModel_->GetBounds(center, maxExtent);
			previewModelScale_ = 1.65f / (std::max)(maxExtent, 0.001f);
			previewModelOffset_ = center;
			previewRotationY_ = 0.0f;

			if (!previewObject3d_) {
				previewObject3d_ = std::make_unique<Object3d>();
				previewObject3d_->Initialize();
			}
			previewObject3d_->SetCamera(&previewCamera_);
			previewObject3d_->SetModel(previewModel_.get(), true);
			if (Material* mat = previewObject3d_->GetMaterial()) {
				mat->color = { 1.0f, 1.0f, 1.0f, 1.0f };
				mat->enableLighting = 1;
				mat->lightingType = 4;
				mat->shininess = 32.0f;
				mat->specularColor = { 0.6f, 0.6f, 0.6f };
				mat->receiveShadows = 0;
			}
		}
		focusDetailsTab_ = true;
	}
}

void EditorSuite::ReleasePreviewResources()
{
	if (currentPreviewTextureHandle_ != 0) {
		TextureManager::GetInstance()->ReleasePreviewTexture();
		currentPreviewTextureHandle_ = 0;
	}
	if (previewObject3d_) {
		previewObject3d_->SetModel(nullptr, true);
	}
	if (previewModel_) {
		// Model::~Model() queues its vertex and index buffers into dxCommon_->DeferRelease()
		previewModel_.reset();
	}
	previewVertexCount_ = 0;
	previewTriangleCount_ = 0;
	currentPreviewAssetPath_.clear();
}


AssetDatabase& EditorSuite::AssetDb() const
{
#ifndef USE_IMGUI
	assetDatabase_.SetReadOnly(true); // exported games never write .meta files
#endif
	assetDatabase_.SetRoot(currentProjectRoot_);
	return assetDatabase_;
}

int EditorSuite::RepointAssetReferences(const std::string& from, const std::string& to)
{
	AssetDatabase& db = AssetDb();
	auto lower = [](std::string s) {
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return s;
	};
	const std::string fromKey = lower(from);
	// Returns the new path when `path` is `from` itself or lies inside the moved folder
	auto repoint = [&](const std::string& path, std::string& outNew) {
		const std::string assetPath = db.ToAssetPath(path);
		if (assetPath.empty()) return false;
		const std::string key = lower(assetPath);
		if (key != fromKey && !(key.starts_with(fromKey) && key.size() > fromKey.size() && key[fromKey.size()] == '/')) return false;
		outNew = to + assetPath.substr(from.size());
		return true;
	};

	int changed = 0;
	// A renamed prefab file: its instances follow (they link by name)
	const std::string prefabSuffix = ".prefab.json";
	const auto prefabName = [&](const std::string& path) {
		const std::string file = std::filesystem::path(path).filename().string();
		return lower(file).ends_with(prefabSuffix) ? file.substr(0, file.size() - prefabSuffix.size()) : std::string();
	};
	if (const std::string oldName = prefabName(from), newName = prefabName(to); !oldName.empty() && !newName.empty() && oldName != newName) {
		for (GameObject& obj : gameObjects_) {
			if (obj.prefabSource != oldName) continue;
			obj.prefabSource = newName;
			++changed;
		}
		bool tileSetChanged = false; // Mapchip tiles link their prefab by name too
		for (MapchipTileKind& kind : mapTileSet_.kinds) {
			if (kind.prefab != oldName) continue;
			kind.prefab = newName;
			tileSetChanged = true;
		}
		if (tileSetChanged) SaveMapchipTileSet();
		ClearPrefabCache();
	}
	if (std::string newSet; repoint(mapTileSetPath_, newSet)) { // the open scene's tile set file
		mapTileSetPath_ = newSet;
		++changed;
	}
	std::string moved;
	for (GameObject& obj : gameObjects_) {
		for (std::string* path : { &obj.texturePath, &obj.modelPath, &obj.audioClipName }) {
			if (repoint(*path, moved)) {
				*path = moved;
				++changed;
			}
		}
		// Asset fields of data components (e.g. the Animator's controller)
		for (const auto& entry : obj.components.Entries()) {
			const ComponentInfo* info = ComponentRegistry::Get().Find(entry.name);
			if (!info || !entry.data) continue;
			for (const ComponentField& field : info->fields) {
				if (field.type != ComponentFieldType::Asset) continue;
				char* text = static_cast<char*>(entry.data->Data()) + field.offset;
				if (repoint(std::string(text, strnlen(text, field.size)), moved)) {
					strncpy_s(text, field.size, moved.c_str(), _TRUNCATE);
					++changed;
				}
			}
		}
	}
	if (repoint(skybox_.GetCubemapPath(), moved)) {
		skybox_.SetCubemapPath(moved);
		++changed;
	}
	// File-path state of the editor itself (not part of the scene content)
	if (repoint(currentScenePath_, moved)) {
		currentScenePath_ = db.ToAbsolute(moved).string();
		strncpy_s(openScenePathBuf_, currentScenePath_.c_str(), sizeof(openScenePathBuf_) - 1);
		if (!prefabMode_.active) currentSceneName_ = SceneNameFromPath(currentScenePath_); // renamed in the Project panel: the Hierarchy header follows
	}
	if (repoint(buildStartupSceneBuf_, moved)) {
		strncpy_s(buildStartupSceneBuf_, db.ToAbsolute(moved).string().c_str(), sizeof(buildStartupSceneBuf_) - 1);
	}
	if (repoint(materialEditorPath_, moved)) materialEditorPath_ = moved; // the graph window saves to the new name
	resolvedAssetCache_.clear();
	ClearPrefabCache();
	return changed;
}

bool EditorSuite::MoveAssetAndUpdateReferences(const std::string& from, const std::string& to)
{
	if (sceneState_ != EditorSceneState::Edit) {
		LogMessage("[Assets] Stop Play mode before moving or renaming assets.", 1);
		return false;
	}
	AssetDatabase& db = AssetDb();
	const std::string src = db.ToAssetPath(from);
	const std::string dst = db.ToAssetPath(to);
	const bool wasSelected = !selectedAssetPath_.empty() && db.ToAssetPath(std::filesystem::absolute(selectedAssetPath_).string()) == src;
	if (wasSelected) ReleasePreviewResources();

	std::string error;
	if (!db.MoveAsset(src.empty() ? from : src, dst.empty() ? to : dst, &error)) {
		LogMessage(std::format("[Assets] Cannot move '{}': {}", from, error), 2);
		return false;
	}
	const int changed = RepointAssetReferences(src, dst);
	if (changed > 0) isSceneDirty_ = true;
	if (wasSelected) selectedAssetPath_ = db.ToAbsolute(dst);
	// Keep browsing the same folder when it (or one of its parents) was the one moved
	const std::string browsing = db.ToAssetPath(std::filesystem::absolute(currentDirectory_).string());
	if (!browsing.empty() && (browsing == src || browsing.starts_with(src + "/"))) {
		currentDirectory_ = db.ToAbsolute(dst + browsing.substr(src.size()));
	}
	LogMessage(std::format("[Assets] Moved '{}' -> '{}' ({} reference(s) in this scene updated).", src, dst, changed), 0);
	return true;
}

bool EditorSuite::DeleteAssetToRecycleBin(const std::string& path)
{
	if (sceneState_ != EditorSceneState::Edit) {
		LogMessage("[Assets] Stop Play mode before deleting assets.", 1);
		return false;
	}
	AssetDatabase& db = AssetDb();
	const std::string assetPath = db.ToAssetPath(path);
	if (!selectedAssetPath_.empty() && db.ToAssetPath(std::filesystem::absolute(selectedAssetPath_).string()) == assetPath) {
		ReleasePreviewResources();
		selectedAssetPath_.clear();
	}
	std::string error;
	if (!db.DeleteAsset(path, &error)) {
		LogMessage(std::format("[Assets] Cannot delete '{}': {}", path, error), 2);
		return false;
	}
	resolvedAssetCache_.clear();
	ClearPrefabCache();
	// The graph window must not write the deleted file (or a file of a deleted folder) back on its next edit
	const std::string& open = materialEditorPath_;
	if (!assetPath.empty() && open.size() >= assetPath.size() && _strnicmp(open.c_str(), assetPath.c_str(), assetPath.size()) == 0 &&
		(open.size() == assetPath.size() || open[assetPath.size()] == '/')) {
		materialEditorPath_.clear();
		materialEditor_.MarkSaved(); // nothing left to save
	}
	LogMessage(std::format("[Assets] Moved '{}' to the recycle bin.", assetPath), 0);
	return true;
}

void EditorSuite::DrawProjectAndConsolePanel()
{
#ifdef USE_IMGUI
	ImGuiIO& io = ImGui::GetIO();
	float screenW = io.DisplaySize.x;
	float screenH = io.DisplaySize.y;
	float topY = ImGui::GetFrameHeight() + kTopBarHeight;
	float midH = (screenH - topY) * 0.65f;
	float bottomH = screenH - topY - midH;

	ImGui::SetNextWindowPos(ImVec2(0.0f, topY + midH));
	ImGui::SetNextWindowSize(ImVec2(screenW, bottomH));
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;

	if (ImGui::Begin(TrId("Project / Console"), nullptr, flags)) {
		if (ImGui::BeginTabBar("BottomPanelTabs")) {
			// TAB 1: PROJECT (Asset Browser with Subfolder Navigation)
			if (ImGui::BeginTabItem(TrId("Project"))) {
				bottomPanelTab_ = 0;
				// Moves requested by drag & drop run after the listing (the entries are being iterated)
				std::string pendingMoveFrom;
				std::string pendingMoveTo;
				AssetDatabase& assetDb = AssetDb();
				// "Assets/..." path of a Project panel entry, "" when it is not a managed asset (engine folders, scripts)
				auto managedAssetPath = [&](const std::filesystem::path& p) {
					std::error_code aec;
					return assetDb.ToAssetPath(std::filesystem::absolute(p, aec).string());
				};
				// Right-click menu, drag source and folder drop target of one entry
				auto assetItemInteractions = [&](const std::filesystem::path& p, const std::string& assetPath, bool isDirectory) {
					if (assetPath.empty() || assetPath == "Assets") return;
					if (ImGui::BeginPopupContextItem()) {
						if (ImGui::MenuItem(TrId("Rename..."))) {
							assetRenameTarget_ = p;
							strncpy_s(assetRenameBuf_, p.filename().string().c_str(), sizeof(assetRenameBuf_) - 1);
							openAssetRenamePopup_ = true;
						}
						if (ImGui::MenuItem(TrId("Delete"))) {
							assetDeleteTarget_ = p;
							openAssetDeletePopup_ = true;
						}
						if (const std::string file = p.filename().string(); !isDirectory && file.ends_with(".prefab.json") && ImGui::MenuItem(TrId("Open in Prefab Mode"))) {
							OpenPrefabMode(file.substr(0, file.size() - std::string_view(".prefab.json").size()));
						}
						ImGui::Separator();
						ImGui::TextDisabled(Tr("GUID: %s"), isDirectory ? "(folder)" : assetDb.FindGuid(assetPath).c_str());
						ImGui::EndPopup();
					}
					if (ImGui::BeginDragDropSource()) {
						ImGui::SetDragDropPayload("RIZE_ASSET_PATH", assetPath.c_str(), assetPath.size() + 1);
						ImGui::Text(Tr("Move %s"), p.filename().string().c_str());
						ImGui::EndDragDropSource();
					}
					if (isDirectory && ImGui::BeginDragDropTarget()) {
						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("RIZE_ASSET_PATH")) {
							const std::string from = static_cast<const char*>(payload->Data);
							pendingMoveFrom = from;
							pendingMoveTo = assetPath + "/" + std::filesystem::path(from).filename().string();
						}
						ImGui::EndDragDropTarget();
					}
				};

				if (ImGui::Button(TrId("< Back"))) {
					std::filesystem::path parent = currentDirectory_.parent_path();
					if (!parent.empty() && parent != currentDirectory_) {
						currentDirectory_ = parent;
					} else if (currentDirectory_ != ".") {
						currentDirectory_ = ".";
					}
				}
				ImGui::SetItemTooltip("%s", Tr("Navigate to parent directory (drop an asset here to move it up one folder)"));
				if (ImGui::BeginDragDropTarget()) {
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("RIZE_ASSET_PATH")) {
						const std::filesystem::path from = static_cast<const char*>(payload->Data);
						const std::filesystem::path parentFolder = from.parent_path().parent_path();
						if (!managedAssetPath(assetDb.ToAbsolute(parentFolder.generic_string())).empty()) {
							pendingMoveFrom = from.generic_string();
							pendingMoveTo = (parentFolder / from.filename()).generic_string();
						}
					}
					ImGui::EndDragDropTarget();
				}

				ImGui::SameLine();
				if (ImGui::Button(TrId("Refresh"))) {
					assetDb.Refresh();
					LogMessage(std::format("[Assets] Rescanned Assets/: {} asset(s) with a GUID.", assetDb.Count()), 0);
				}
				ImGui::SetItemTooltip("%s", Tr("Rescan the project's Assets folder (.meta GUID files)"));

				ImGui::SameLine();
				ImGui::BeginDisabled(currentProjectRoot_.empty());
				if (ImGui::Button(TrId("Import..."))) {
					const std::vector<std::string> files = OpenNativeFilesDialog();
					ImportAssets(std::vector<std::filesystem::path>(files.begin(), files.end()));
				}
				ImGui::EndDisabled();
				ImGui::SetItemTooltip("%s", Tr("Copy files into this folder (or drag files from Explorer onto the editor window)"));

				ImGui::SameLine();
				if (ImGui::Button(TrId("Root"))) {
					currentDirectory_ = ".";
				}
				ImGui::SetItemTooltip("%s", Tr("Browse workspace root (Resources, shaders, Assets)"));

				ImGui::SameLine();
				if (ImGui::Button(TrId("Resources"))) {
					if (std::filesystem::exists("Resources")) currentDirectory_ = "Resources";
				}
				ImGui::SameLine();
				if (ImGui::Button(TrId("Scripts"))) {
					std::filesystem::path scriptsDir = ResolveGameScriptsDir() / "Scripts";
					if (std::filesystem::exists(scriptsDir)) {
						currentDirectory_ = scriptsDir;
					}
				}
				ImGui::SetItemTooltip("%s", Tr("Browse GameScripts/Scripts (.cpp / .h)"));

				ImGui::SameLine();
				if (ImGui::Button(TrId("shaders"))) {
					if (std::filesystem::exists("shaders")) currentDirectory_ = "shaders";
					else if (std::filesystem::exists("../RizealldayEngine/shaders")) currentDirectory_ = "../RizealldayEngine/shaders";
				}
				if (!currentProjectRoot_.empty() && std::filesystem::exists(currentProjectRoot_)) {
					ImGui::SameLine();
					if (ImGui::Button(TrId("Project Root"))) {
						currentDirectory_ = currentProjectRoot_;
					}
				}

				ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), Tr("Directory: %s"), currentDirectory_.string().c_str());
				ImGui::SameLine((std::max)(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - 200.0f));
				ImGui::SetNextItemWidth(190.0f);
				ImGui::InputTextWithHint("##AssetSearch", Tr("Search Assets..."), assetSearchBuf_, sizeof(assetSearchBuf_));

				ImGui::Separator();

				ImGui::BeginChild("ProjectAssetsScrollArea", ImVec2(0, 0), false);
				try {
					if (std::filesystem::exists(currentDirectory_) && std::filesystem::is_directory(currentDirectory_)) {
						std::vector<std::filesystem::directory_entry> dirs;
						std::vector<std::filesystem::directory_entry> files;
						for (const auto& entry : std::filesystem::directory_iterator(currentDirectory_)) {
							std::string filename = entry.path().filename().string();
							if (AssetDatabase::IsMetaFile(entry.path()) || entry.path().filename().native().starts_with(L'.')) continue; // .meta, .gitkeep
							if (assetSearchBuf_[0] != '\0' && filename.find(assetSearchBuf_) == std::string::npos) continue;
							if (entry.is_directory()) dirs.push_back(entry);
							else files.push_back(entry);
						}

						// Render directories first (double-click to enter subfolder)
						for (const auto& entry : dirs) {
							std::string filename = entry.path().filename().string();
							ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), Tr("[DIR]"));
							ImGui::SameLine();
							bool isSel = (selectedAssetPath_ == entry.path());
							if (ImGui::Selectable(filename.c_str(), isSel, ImGuiSelectableFlags_AllowDoubleClick)) {
								if (selectedAssetPath_ != entry.path()) {
									ReleasePreviewResources();
									selectedAssetPath_ = entry.path();
								}
								if (ImGui::IsMouseDoubleClicked(0)) {
									currentDirectory_ = entry.path();
								}
							}
							ImGui::SetItemTooltip(Tr("Double-click to open folder '%s'. Drop assets here to move them."), filename.c_str());
							assetItemInteractions(entry.path(), managedAssetPath(entry.path()), true);
						}

						// Render files
						for (const auto& entry : files) {
							std::string filename = entry.path().filename().string();
							std::string ext = entry.path().extension().string();
							std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

							std::string badge = "[FILE]";
							ImVec4 badgeCol = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);

							if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".dds") {
								badge = "[TEX]";
								badgeCol = ImVec4(0.9f, 0.4f, 0.6f, 1.0f);
							} else if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb") {
								badge = "[3D]";
								badgeCol = ImVec4(0.3f, 0.8f, 0.9f, 1.0f);
							} else if (filename.ends_with(".vfxmat.json")) {
								badge = "[MAT]";
								badgeCol = ImVec4(1.0f, 0.6f, 0.3f, 1.0f);
							} else if (filename.ends_with(".vfxparticle.json")) {
								badge = "[PFX]";
								badgeCol = ImVec4(1.0f, 0.85f, 0.3f, 1.0f);
							} else if (filename.ends_with(".prefab.json")) {
								badge = "[PREFAB]";
								badgeCol = ImVec4(0.4f, 0.65f, 1.0f, 1.0f);
							} else if (filename.find(".scene.json") != std::string::npos || ext == ".json") {
								badge = "[SCENE]";
								badgeCol = ImVec4(0.9f, 0.5f, 0.2f, 1.0f);
							} else if (ext == ".csv") {
								badge = "[MAP]";
								badgeCol = ImVec4(0.2f, 0.8f, 0.3f, 1.0f);
							} else if (ext == ".hlsl" || ext == ".hlsli") {
								badge = "[HLSL]";
								badgeCol = ImVec4(0.7f, 0.5f, 0.95f, 1.0f);
							} else if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".m4a" || ext == ".wma") {
								badge = "[AUDIO]";
								badgeCol = ImVec4(0.4f, 0.9f, 0.7f, 1.0f);
							} else if (ext == ".cpp") {
								badge = "[CPP]";
								badgeCol = ImVec4(0.35f, 0.75f, 1.0f, 1.0f);
							} else if (ext == ".h" || ext == ".hpp") {
								badge = "[H]";
								badgeCol = ImVec4(0.65f, 0.55f, 1.0f, 1.0f);
							}

							ImGui::TextColored(badgeCol, "%s", badge.c_str());
							ImGui::SameLine();
							bool isSel = (selectedAssetPath_ == entry.path());
							if (ImGui::Selectable(filename.c_str(), isSel, ImGuiSelectableFlags_AllowDoubleClick)) {
								SelectAssetForPreview(entry.path());
								if (ImGui::IsMouseDoubleClicked(0)) {
									if (IsSceneFile(entry.path()) && !IsPathInsideProject(entry.path())) {
										LogMessage(std::format("[Scene] '{}' is outside the current project and cannot be opened.", filename), 1);
									} else if (IsSceneFile(entry.path())) {
										LoadScene(entry.path().string());
									} else if (ext == ".cpp" || ext == ".h" || ext == ".hpp") {
										OpenFileInVisualStudio(entry.path());
									} else if (filename.ends_with(".vfxmat.json") || filename.ends_with(".vfxparticle.json")) {
										OpenMaterialGraph(managedAssetPath(entry.path()));
									} else if (filename.ends_with(".prefab.json")) {
										// Double-click: place one instance at the origin
										const std::string prefabName = filename.substr(0, filename.size() - std::string_view(".prefab.json").size());
										if (const uint32_t newId = InstantiatePrefabNow(prefabName, { 0.0f, 0.0f, 0.0f }); newId != 0) {
											selectedObjectIndex_ = FindGameObjectIndexById(newId);
											isSceneDirty_ = true;
											if (sceneState_ == EditorSceneState::Edit) PushUndoSnapshot();
										}
									}
								}
								if (filename.ends_with(".prefab.json")) {
									ImGui::SetItemTooltip(Tr("Double-click to place in the scene. Scripts: InstantiatePrefab(\"%s\")"),
										filename.substr(0, filename.size() - 12).c_str());
								}
							}
							const std::string assetPath = managedAssetPath(entry.path());
							if (!assetPath.empty()) assetDb.GuidForPath(assetPath); // files dropped in from Explorer get their .meta here
							assetItemInteractions(entry.path(), assetPath, false);
						}
					}
				} catch (...) {}
				ImGui::EndChild();

				if (!pendingMoveFrom.empty()) {
					MoveAssetAndUpdateReferences(pendingMoveFrom, pendingMoveTo);
				}

				if (openAssetRenamePopup_) {
					ImGui::OpenPopup(TrId("Rename Asset"));
					openAssetRenamePopup_ = false;
				}
				if (ImGui::BeginPopupModal(TrId("Rename Asset"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
					ImGui::Text(Tr("Rename '%s'"), assetRenameTarget_.filename().string().c_str());
					if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
					const bool submitted = ImGui::InputText("##AssetRename", assetRenameBuf_, sizeof(assetRenameBuf_), ImGuiInputTextFlags_EnterReturnsTrue);
					ImGui::TextDisabled(Tr("References in scenes and prefabs follow the asset (GUID)."));
					if (submitted || ImGui::Button(TrId("Rename"), ImVec2(120.0f, 0.0f))) {
						const std::string newName = assetRenameBuf_;
						if (!newName.empty() && newName.find_first_of("/\\:*?\"<>|") == std::string::npos) {
							MoveAssetAndUpdateReferences(managedAssetPath(assetRenameTarget_),
								managedAssetPath(assetRenameTarget_.parent_path() / newName));
							ImGui::CloseCurrentPopup();
						} else {
							LogMessage("[Assets] Invalid file name.", 1);
						}
					}
					ImGui::SameLine();
					if (ImGui::Button(TrId("Cancel"), ImVec2(120.0f, 0.0f))) ImGui::CloseCurrentPopup();
					ImGui::EndPopup();
				}

				if (openAssetDeletePopup_) {
					ImGui::OpenPopup(TrId("Delete Asset"));
					openAssetDeletePopup_ = false;
				}
				if (ImGui::BeginPopupModal(TrId("Delete Asset"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
					ImGui::Text(Tr("Move '%s' to the recycle bin?"), assetDeleteTarget_.filename().string().c_str());
					ImGui::TextDisabled(Tr("Objects that use it keep the path but show nothing until it is restored."));
					if (ImGui::Button(TrId("Delete"), ImVec2(120.0f, 0.0f))) {
						DeleteAssetToRecycleBin(managedAssetPath(assetDeleteTarget_));
						ImGui::CloseCurrentPopup();
					}
					ImGui::SameLine();
					if (ImGui::Button(TrId("Cancel"), ImVec2(120.0f, 0.0f))) ImGui::CloseCurrentPopup();
					ImGui::EndPopup();
				}

				ImGui::EndTabItem();
			}

			// TAB 2: CONSOLE
			if (ImGui::BeginTabItem(TrId("Console"))) {
				bottomPanelTab_ = 1;
				if (ImGui::Button(TrId("Clear"))) {
					consoleLogs_.clear();
					infoCount_ = 0; warningCount_ = 0; errorCount_ = 0;
				}
				ImGui::SameLine();
				ImGui::Checkbox(TrId("Collapse"), &collapseLogs_);
				ImGui::SameLine(ImGui::GetContentRegionAvail().x - 180.0f);
				ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), Tr("Info: %d"), infoCount_);
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), Tr("Warn: %d"), warningCount_);
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), Tr("Error: %d"), errorCount_);

				ImGui::Separator();
				ImGui::BeginChild("ConsoleScrollArea", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
				for (const auto& log : consoleLogs_) {
					ImVec4 col = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
					if (log.type == 1) col = ImVec4(0.95f, 0.85f, 0.3f, 1.0f);
					else if (log.type == 2) col = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
					ImGui::TextColored(col, "%s", log.message.c_str());
				}
				if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
				ImGui::EndChild();

				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}
	}
	ImGui::End();
#endif
}

