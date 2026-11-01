#pragma once
#include "math/Vector3.h"

/// Data component "Bone Attachment": the object follows a bone of its parent instead of the parent itself
struct BoneAttachmentComponent {
	char bone[64] = "";
};

/// Data component "PBR Material": the object is lit with PBR (lightingType 6). `material` optionally points at a
/// .material.json holding the same values, so several objects can share them.
struct PbrMaterialComponent {
	char material[260] = "";
	float metallic = 0.0f;
	float roughness = 0.5f;
	float normalStrength = 1.0f;
	Vector3 emissive = { 1.0f, 1.0f, 1.0f };
	float emissiveStrength = 0.0f;
	char albedoMap[260] = "";
	char normalMap[260] = "";
	char ormMap[260] = "";
	char emissiveMap[260] = "";
};
