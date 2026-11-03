#pragma once
#include "SceneJson.h"
#include <string>
#include <vector>

// Override keys of a prefab node against the node it comes from (EditorSuite_PrefabLinks.cpp):
// "key" (a whole value), "scale/0" (one axis), "components/Trail" (added or removed), "components/Trail/lifetime" (one field).
// Used for instance overrides (undo diff), prefab variants (against the base) and nested prefabs (against the inner prefab).

/// Adds to keys what differs between before and after, unless the whole value is listed already. The root's placement never counts.
void CollectPrefabOverrideKeys(const SceneJson::Json& before, const SceneJson::Json& after, bool isRoot, std::vector<std::string>& keys);
/// Copies one override from source into node. False = it no longer applies (node has no such part).
bool MergePrefabOverride(SceneJson::Json& node, const SceneJson::Json& source, const std::string& key);
