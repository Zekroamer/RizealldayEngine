#pragma once
#include "EditorSuite.h"
#include <windows.h>
#include <shobjidl.h>
#include <RizeEngine.h>
#include "3d/Object3d.h"
#include "3d/Model.h"
#include "3d/LightGroup.h"
#include "3d/PrimitiveDrawer.h"
#include "2d/UIRenderer.h"
#include "base/Profiler.h"
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <unordered_set>
#include <set>
#include <cstring>
#include <string_view>
#include <ctime>
#include <iomanip>
#include <objbase.h>
#include <oleauto.h>
#include <shellapi.h>
#include "TransformUtil.h"
#include "Localization.h"

// Editor UI language (Options > Language): Tr = translated text, TrId = translated label with the English ImGui ID
using Localization::Tr;
using Localization::TrId;

#ifdef USE_IMGUI
#include <imgui.h>
#endif

namespace EditorSuiteDetail {
/// Files a model references next to it (.obj: .mtl + maps, .gltf: buffers + images), as relative paths
std::vector<std::string> ModelCompanionFiles(const std::filesystem::path& model);

/// "Assets/Scenes/Level1.scene.json" -> "Level1" (stem() alone would give "Level1.scene")
inline std::string SceneNameFromPath(const std::filesystem::path& path)
{
	std::string name = path.filename().string();
	for (const char* ext : { ".scene.json", ".json" }) {
		if (name.ends_with(ext)) {
			name.resize(name.size() - std::strlen(ext));
			break;
		}
	}
	return name;
}

/// True when the file is an editor scene: *.scene.json, or a *.json whose content has a "sceneName" key
inline bool IsSceneFile(const std::filesystem::path& path)
{
	const std::string fileName = path.filename().string();
	if (fileName.ends_with(".scene.json")) return true;
	if (!fileName.ends_with(".json")) return false;
	std::ifstream file(path);
	char head[512]{};
	file.read(head, sizeof(head) - 1);
	return std::string_view(head).find("\"sceneName\"") != std::string_view::npos;
}
}
