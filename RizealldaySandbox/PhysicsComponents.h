#pragma once
#include "math/Vector3.h"

// Data components of the physics system (see EditorSuite_Physics.cpp)

/// Upright capsule. `height` is the total height including both caps.
struct CapsuleColliderComponent {
	bool enabled = true;
	Vector3 center = { 0.0f, 0.0f, 0.0f };
	float radius = 0.5f;
	float height = 2.0f;
};

/// Collides with the triangles of the object's own model. Never simulated: a Rigidbody on it acts as kinematic.
struct MeshColliderComponent {
	bool enabled = true;
};

/// Connects this object's body to another object (connectedObjectId = 0: to the world).
struct JointComponent {
	bool enabled = true;
	int jointType = 0; // 0 Fixed, 1 Point, 2 Hinge, 3 Distance
	int connectedObjectId = 0;
	Vector3 anchor = { 0.0f, 0.0f, 0.0f }; // in this object's local space
	Vector3 axis = { 0.0f, 0.0f, 1.0f };   // Hinge: world axis at creation
};
