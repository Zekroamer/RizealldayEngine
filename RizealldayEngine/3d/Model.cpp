#include "3d/Model.h"
#include <cfloat>
#include "base/DirectXCommon.h"
#include "base/Profiler.h"
#include "base/TextureManager.h"
#include "base/Log.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <cassert>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

DirectXCommon* Model::sDxCommon_ = nullptr;
std::unordered_map<std::string, std::unique_ptr<Model>> Model::sCache_;

void Model::StaticInitialize(DirectXCommon* dxCommon) {
    sDxCommon_ = dxCommon;
}

Model::~Model() {
    for (auto& mesh : meshes_) {
        if (mesh.vertexBuffer) {
            if (sDxCommon_) {
                sDxCommon_->DeferRelease(mesh.vertexBuffer);
            }
            mesh.vertexBuffer.Reset();
        }
        if (mesh.indexBuffer) {
            if (sDxCommon_) {
                sDxCommon_->DeferRelease(mesh.indexBuffer);
            }
            mesh.indexBuffer.Reset();
        }
    }
}

Model* Model::CreateFromFile(const std::string& directoryPath, const std::string& filename) {
    std::string fullPath = directoryPath.empty() ? filename : (directoryPath + "/" + filename);
    
    auto it = sCache_.find(fullPath);
    if (it != sCache_.end()) {
        return it->second.get();
    }
    
    std::unique_ptr<Model> model = std::make_unique<Model>(PrivateTag{});
    
    size_t extPos = filename.find_last_of('.');
    std::string ext;
    if (extPos != std::string::npos) {
        ext = filename.substr(extPos + 1);
    }
    
    for (auto& c : ext) {
        c = static_cast<char>(std::tolower(c));
    }
    
    model->LoadAssimpModel(fullPath);
    if (model->meshes_.empty() && ext == "obj") {
        model->LoadOBJ(directoryPath, filename);
    }
    
    model->CreateGPUBuffers();
    
    Model* rawPtr = model.get();
    sCache_[fullPath] = std::move(model);
    return rawPtr;
}

std::unique_ptr<Model> Model::CreatePreviewFromFile(const std::string& filePath) {
    std::unique_ptr<Model> model = std::make_unique<Model>(PrivateTag{});
    std::filesystem::path p(filePath);
    std::string ext = p.extension().string();
    for (auto& c : ext) {
        c = static_cast<char>(std::tolower(c));
    }

    model->LoadAssimpModel(filePath);
    if (model->meshes_.empty() && ext == ".obj") {
        model->LoadOBJ(p.parent_path().string(), p.filename().string());
    }

    model->CreateGPUBuffers();
    return model;
}

uint32_t Model::GetTotalVertexCount() const {
    uint32_t total = 0;
    for (const auto& m : meshes_) {
        total += static_cast<uint32_t>(m.vertices.size());
    }
    return total;
}

uint32_t Model::GetTotalTriangleCount() const {
    uint32_t total = 0;
    for (const auto& m : meshes_) {
        total += static_cast<uint32_t>(m.indices.size() / 3);
    }
    return total;
}

void Model::ComputeCachedLocalAABB() {
    Vector3 minPos = { 1e9f, 1e9f, 1e9f };
    Vector3 maxPos = { -1e9f, -1e9f, -1e9f };
    bool hasVerts = false;

    std::vector<Matrix4x4> boneMatrices;
    const bool hasBones = HasBones();
    if (hasBones) {
        CalculateBoneTransforms(0.0f, 0, boneMatrices);
    }

    for (const auto& m : meshes_) {
        for (const auto& v : m.vertices) {
            hasVerts = true;
            Vector3 pos = { v.position.x, v.position.y, v.position.z };

            if (hasBones && !boneMatrices.empty()) {
                float totalWeight = v.boneWeights[0] + v.boneWeights[1] + v.boneWeights[2] + v.boneWeights[3];
                if (totalWeight > 0.0001f) {
                    Vector3 skinned = { 0.0f, 0.0f, 0.0f };
                    for (int k = 0; k < 4; ++k) {
                        float w = v.boneWeights[k];
                        uint32_t bIdx = v.boneIndices[k];
                        if (w > 0.0001f && bIdx < boneMatrices.size()) {
                            const Matrix4x4& bm = boneMatrices[bIdx];
                            float tx = pos.x * bm.m[0][0] + pos.y * bm.m[1][0] + pos.z * bm.m[2][0] + bm.m[3][0];
                            float ty = pos.x * bm.m[0][1] + pos.y * bm.m[1][1] + pos.z * bm.m[2][1] + bm.m[3][1];
                            float tz = pos.x * bm.m[0][2] + pos.y * bm.m[1][2] + pos.z * bm.m[2][2] + bm.m[3][2];
                            skinned.x += tx * w;
                            skinned.y += ty * w;
                            skinned.z += tz * w;
                        }
                    }
                    pos = skinned;
                }
            }

            minPos.x = (std::min)(minPos.x, pos.x);
            minPos.y = (std::min)(minPos.y, pos.y);
            minPos.z = (std::min)(minPos.z, pos.z);
            maxPos.x = (std::max)(maxPos.x, pos.x);
            maxPos.y = (std::max)(maxPos.y, pos.y);
            maxPos.z = (std::max)(maxPos.z, pos.z);
        }
    }

    if (!hasVerts) {
        localMin_ = { -0.5f, -0.5f, -0.5f };
        localMax_ = {  0.5f,  0.5f,  0.5f };
        hasValidBounds_ = false;
        return;
    }

    // Pad animated/skinned models slightly so animated limb motion stays inside the bounding box
    if (hasBones && !animations_.empty()) {
        float ex = (maxPos.x - minPos.x) * 0.25f + 0.25f;
        float ey = (maxPos.y - minPos.y) * 0.25f + 0.25f;
        float ez = (maxPos.z - minPos.z) * 0.25f + 0.25f;
        minPos.x -= ex; minPos.y -= ey; minPos.z -= ez;
        maxPos.x += ex; maxPos.y += ey; maxPos.z += ez;
    }

    // Ensure non-zero thickness along thin axes (e.g. Planes or Quads)
    if (maxPos.x - minPos.x < 0.02f) { minPos.x -= 0.01f; maxPos.x += 0.01f; }
    if (maxPos.y - minPos.y < 0.02f) { minPos.y -= 0.01f; maxPos.y += 0.01f; }
    if (maxPos.z - minPos.z < 0.02f) { minPos.z -= 0.01f; maxPos.z += 0.01f; }

    localMin_ = minPos;
    localMax_ = maxPos;
    hasValidBounds_ = true;
}

void Model::GetLocalAABB(Vector3& outMin, Vector3& outMax) const {
    outMin = localMin_;
    outMax = localMax_;
}

void Model::GetBounds(Vector3& outCenter, float& outMaxExtent) const {
    if (hasValidBounds_) {
        outCenter = {
            (localMin_.x + localMax_.x) * 0.5f,
            (localMin_.y + localMax_.y) * 0.5f,
            (localMin_.z + localMax_.z) * 0.5f
        };
        float dx = localMax_.x - localMin_.x;
        float dy = localMax_.y - localMin_.y;
        float dz = localMax_.z - localMin_.z;
        outMaxExtent = (std::max)({ dx, dy, dz, 0.001f });
        return;
    }

    outCenter = { 0.0f, 0.0f, 0.0f };
    outMaxExtent = 1.0f;
}

Model* Model::CreateCube() {
    const std::string cacheKey = "__primitive_cube";
    auto it = sCache_.find(cacheKey);
    if (it != sCache_.end()) {
        return it->second.get();
    }

    auto model = std::make_unique<Model>(PrivateTag{});
    MeshData mesh;
    mesh.textureHandle = TextureManager::GetInstance()->GetDefaultWhiteTexture();

    struct FaceDef {
        Vector3 normal;
        Vector3 corners[4];
    };
    FaceDef faces[6] = {
        // Front (+Z)
        { {0.0f, 0.0f, 1.0f}, { {-0.5f, -0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f}, { 0.5f,  0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f} } },
        // Back (-Z)
        { {0.0f, 0.0f, -1.0f}, { { 0.5f, -0.5f, -0.5f}, { 0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f} } },
        // Top (+Y)
        { {0.0f, 1.0f, 0.0f}, { {-0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f, -0.5f}, { 0.5f,  0.5f, -0.5f}, { 0.5f,  0.5f,  0.5f} } },
        // Bottom (-Y)
        { {0.0f, -1.0f, 0.0f}, { {-0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f, -0.5f} } },
        // Right (+X)
        { {1.0f, 0.0f, 0.0f}, { { 0.5f, -0.5f,  0.5f}, { 0.5f,  0.5f,  0.5f}, { 0.5f,  0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f} } },
        // Left (-X)
        { {-1.0f, 0.0f, 0.0f}, { {-0.5f, -0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f,  0.5f}, {-0.5f, -0.5f,  0.5f} } }
    };

    Vector2 uvs[4] = { {0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f} };

    for (int f = 0; f < 6; f++) {
        uint32_t baseIdx = static_cast<uint32_t>(mesh.vertices.size());
        for (int v = 0; v < 4; v++) {
            VertexData vd{};
            vd.position = { faces[f].corners[v].x, faces[f].corners[v].y, faces[f].corners[v].z, 1.0f };
            vd.normal = faces[f].normal;
            vd.texcoord = uvs[v];
            mesh.vertices.push_back(vd);
        }
        // Corners are listed counter-clockwise seen from outside; D3D treats clockwise as front-facing
        // (FrontCounterClockwise = FALSE), so emit them reversed or the outer faces get back-face culled.
        mesh.indices.push_back(baseIdx + 0);
        mesh.indices.push_back(baseIdx + 2);
        mesh.indices.push_back(baseIdx + 1);
        mesh.indices.push_back(baseIdx + 0);
        mesh.indices.push_back(baseIdx + 3);
        mesh.indices.push_back(baseIdx + 2);
    }

    model->meshes_.push_back(std::move(mesh));
    model->CreateGPUBuffers();

    Model* rawPtr = model.get();
    sCache_[cacheKey] = std::move(model);
    return rawPtr;
}

Model* Model::CreatePlane() {
    const std::string cacheKey = "__primitive_plane";
    auto it = sCache_.find(cacheKey);
    if (it != sCache_.end()) {
        return it->second.get();
    }

    auto model = std::make_unique<Model>(PrivateTag{});
    MeshData mesh;
    mesh.textureHandle = TextureManager::GetInstance()->GetDefaultWhiteTexture();

    VertexData v0{ {-5.0f, 0.0f, -5.0f, 1.0f}, {0.0f, 10.0f}, {0.0f, 1.0f, 0.0f} };
    VertexData v1{ {-5.0f, 0.0f,  5.0f, 1.0f}, {0.0f,  0.0f}, {0.0f, 1.0f, 0.0f} };
    VertexData v2{ { 5.0f, 0.0f,  5.0f, 1.0f}, {10.0f, 0.0f}, {0.0f, 1.0f, 0.0f} };
    VertexData v3{ { 5.0f, 0.0f, -5.0f, 1.0f}, {10.0f,10.0f}, {0.0f, 1.0f, 0.0f} };

    mesh.vertices = { v0, v1, v2, v3 };
    // Double-sided indices so the plane is visible from top and bottom regardless of culling
    mesh.indices = { 0, 2, 1, 0, 3, 2, 0, 1, 2, 0, 2, 3 };

    model->meshes_.push_back(std::move(mesh));
    model->CreateGPUBuffers();

    Model* rawPtr = model.get();
    sCache_[cacheKey] = std::move(model);
    return rawPtr;
}

Model* Model::CreateQuad() {
    const std::string cacheKey = "__primitive_quad";
    auto it = sCache_.find(cacheKey);
    if (it != sCache_.end()) {
        return it->second.get();
    }

    auto model = std::make_unique<Model>(PrivateTag{});
    MeshData mesh;
    mesh.textureHandle = TextureManager::GetInstance()->GetDefaultWhiteTexture();

    VertexData v0{ {-0.5f, -0.5f, 0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f} };
    VertexData v1{ {-0.5f,  0.5f, 0.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f} };
    VertexData v2{ { 0.5f,  0.5f, 0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f} };
    VertexData v3{ { 0.5f, -0.5f, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f} };

    mesh.vertices = { v0, v1, v2, v3 };
    mesh.indices = { 0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2 };

    model->meshes_.push_back(std::move(mesh));
    model->CreateGPUBuffers();

    Model* rawPtr = model.get();
    sCache_[cacheKey] = std::move(model);
    return rawPtr;
}

Model* Model::CreateSphere() {
    const std::string cacheKey = "__primitive_sphere";
    auto it = sCache_.find(cacheKey);
    if (it != sCache_.end()) {
        return it->second.get();
    }

    auto model = std::make_unique<Model>(PrivateTag{});
    MeshData mesh;
    mesh.textureHandle = TextureManager::GetInstance()->GetDefaultWhiteTexture();

    const int latitudeBands = 16;
    const int longitudeBands = 24;
    const float radius = 0.5f;

    for (int latNumber = 0; latNumber <= latitudeBands; latNumber++) {
        float theta = static_cast<float>(latNumber) * 3.14159265f / static_cast<float>(latitudeBands);
        float sinTheta = std::sin(theta);
        float cosTheta = std::cos(theta);

        for (int longNumber = 0; longNumber <= longitudeBands; longNumber++) {
            float phi = static_cast<float>(longNumber) * 2.0f * 3.14159265f / static_cast<float>(longitudeBands);
            float sinPhi = std::sin(phi);
            float cosPhi = std::cos(phi);

            Vector3 normal = { cosPhi * sinTheta, cosTheta, sinPhi * sinTheta };
            Vector2 uv = { 1.0f - (static_cast<float>(longNumber) / static_cast<float>(longitudeBands)), 1.0f - (static_cast<float>(latNumber) / static_cast<float>(latitudeBands)) };

            VertexData vd{};
            vd.position = { normal.x * radius, normal.y * radius, normal.z * radius, 1.0f };
            vd.normal = normal;
            vd.texcoord = uv;
            mesh.vertices.push_back(vd);
        }
    }

    for (int latNumber = 0; latNumber < latitudeBands; latNumber++) {
        for (int longNumber = 0; longNumber < longitudeBands; longNumber++) {
            uint32_t first = (latNumber * (longitudeBands + 1)) + longNumber;
            uint32_t second = first + longitudeBands + 1;

            mesh.indices.push_back(first);
            mesh.indices.push_back(first + 1);
            mesh.indices.push_back(second);

            mesh.indices.push_back(second);
            mesh.indices.push_back(first + 1);
            mesh.indices.push_back(second + 1);
        }
    }

    model->meshes_.push_back(std::move(mesh));
    model->CreateGPUBuffers();

    Model* rawPtr = model.get();
    sCache_[cacheKey] = std::move(model);
    return rawPtr;
}

static inline Matrix4x4 AiToRizeMatrix(const aiMatrix4x4& ai) {
    Matrix4x4 m;
    m.m[0][0] = ai.a1; m.m[0][1] = ai.b1; m.m[0][2] = ai.c1; m.m[0][3] = ai.d1;
    m.m[1][0] = ai.a2; m.m[1][1] = ai.b2; m.m[1][2] = ai.c2; m.m[1][3] = ai.d2;
    m.m[2][0] = ai.a3; m.m[2][1] = ai.b3; m.m[2][2] = ai.c3; m.m[2][3] = ai.d3;
    m.m[3][0] = ai.a4; m.m[3][1] = ai.b4; m.m[3][2] = ai.c4; m.m[3][3] = ai.d4;
    return m;
}

void Model::LoadAssimpModel(const std::string& filePath) {
    // Resolve path if not found directly
    std::string actualPath = filePath;
    if (!std::filesystem::exists(actualPath)) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::filesystem::path pExe(exePath);
        std::filesystem::path candidate = pExe.parent_path() / filePath;
        if (std::filesystem::exists(candidate)) {
            actualPath = candidate.string();
        } else {
            candidate = pExe.parent_path().parent_path() / "RizealldaySandbox" / filePath;
            if (std::filesystem::exists(candidate)) {
                actualPath = candidate.string();
            }
        }
    }

    Log::GetInstance()->logOutput("Loading model: " + actualPath + "\n");

    Assimp::Importer importer;
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
    const aiScene* scene = importer.ReadFile(actualPath,
        aiProcess_Triangulate |
        aiProcess_FlipUVs |
        aiProcess_GenSmoothNormals |
        aiProcess_JoinIdenticalVertices |
        aiProcess_LimitBoneWeights);

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        std::string err = importer.GetErrorString();
        Log::GetInstance()->logOutput("Assimp Load Error: " + err + "\n");
        return;
    }

    // 1. Build Node Hierarchy
    rootNode_ = BuildNodeHierarchy(scene->mRootNode);
    Matrix4x4 rootMat = AiToRizeMatrix(scene->mRootNode->mTransformation);
    globalInverseTransform_ = Mathematics::Inverse(rootMat);

    // Collect accumulated node transform and owning node name for each mesh index in the Assimp hierarchy
    std::vector<Matrix4x4> meshNodeTransforms(scene->mNumMeshes, Mathematics::MakeIdentity4x4());
    std::vector<std::string> meshNodeNames(scene->mNumMeshes);
    auto collectMeshNodeTransforms = [&](auto& self, const aiNode* node, const Matrix4x4& parentMat) -> void {
        if (!node) return;
        Matrix4x4 globalMat = Mathematics::Multiply(AiToRizeMatrix(node->mTransformation), parentMat);
        for (unsigned int m = 0; m < node->mNumMeshes; ++m) {
            unsigned int meshIdx = node->mMeshes[m];
            if (meshIdx < meshNodeTransforms.size()) {
                meshNodeTransforms[meshIdx] = globalMat;
                meshNodeNames[meshIdx] = node->mName.C_Str();
            }
        }
        for (unsigned int c = 0; c < node->mNumChildren; ++c) {
            self(self, node->mChildren[c], globalMat);
        }
    };
    collectMeshNodeTransforms(collectMeshNodeTransforms, scene->mRootNode, Mathematics::MakeIdentity4x4());

    // 2. Load Meshes & Bones
    std::string directory = actualPath.substr(0, actualPath.find_last_of('/'));
    if (directory == actualPath) directory = actualPath.substr(0, actualPath.find_last_of('\\'));
    
    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[i];
        if (!mesh || mesh->mNumVertices == 0 || mesh->mNumFaces == 0) {
            continue;
        }

        MeshData meshData;
        meshData.vertices.resize(mesh->mNumVertices);
        const bool hasMeshBones = (mesh->mNumBones > 0);
        const Matrix4x4& nodeMat = meshNodeTransforms[i];
        
        for (unsigned int v = 0; v < mesh->mNumVertices; v++) {
            VertexData& vertex = meshData.vertices[v];
            Vector3 rawPos = { mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z };
            Vector3 rawNorm = mesh->mNormals ? Vector3{ mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z } : Vector3{ 0.0f, 1.0f, 0.0f };

            if (!hasMeshBones) {
                float tx = rawPos.x * nodeMat.m[0][0] + rawPos.y * nodeMat.m[1][0] + rawPos.z * nodeMat.m[2][0] + nodeMat.m[3][0];
                float ty = rawPos.x * nodeMat.m[0][1] + rawPos.y * nodeMat.m[1][1] + rawPos.z * nodeMat.m[2][1] + nodeMat.m[3][1];
                float tz = rawPos.x * nodeMat.m[0][2] + rawPos.y * nodeMat.m[1][2] + rawPos.z * nodeMat.m[2][2] + nodeMat.m[3][2];
                rawPos = { tx, ty, tz };

                float nx = rawNorm.x * nodeMat.m[0][0] + rawNorm.y * nodeMat.m[1][0] + rawNorm.z * nodeMat.m[2][0];
                float ny = rawNorm.x * nodeMat.m[0][1] + rawNorm.y * nodeMat.m[1][1] + rawNorm.z * nodeMat.m[2][1];
                float nz = rawNorm.x * nodeMat.m[0][2] + rawNorm.y * nodeMat.m[1][2] + rawNorm.z * nodeMat.m[2][2];
                float nLen = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (nLen > 0.0001f) {
                    rawNorm = { nx / nLen, ny / nLen, nz / nLen };
                }
            }

            vertex.position = { rawPos.x, rawPos.y, rawPos.z, 1.0f };
            vertex.normal = rawNorm;
            if (mesh->mTextureCoords[0]) {
                vertex.texcoord = { mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y };
            } else {
                vertex.texcoord = { 0.0f, 0.0f };
            }
            vertex.boneIndices[0] = vertex.boneIndices[1] = vertex.boneIndices[2] = vertex.boneIndices[3] = 0;
            vertex.boneWeights[0] = vertex.boneWeights[1] = vertex.boneWeights[2] = vertex.boneWeights[3] = 0.0f;
        }

        // Read bones (or create virtual node bone when the FBX has node animations without skin weights)
        if (mesh->mNumBones > 0) {
            std::vector<std::vector<std::pair<uint32_t, float>>> vertexBones(mesh->mNumVertices);
            for (unsigned int b = 0; b < mesh->mNumBones; b++) {
                aiBone* bone = mesh->mBones[b];
                std::string boneName = bone->mName.C_Str();

                uint32_t boneId = 0;
                auto it = boneInfoMap_.find(boneName);
                if (it == boneInfoMap_.end()) {
                    boneId = static_cast<uint32_t>(boneList_.size());
                    BoneInfo info;
                    info.name = boneName;
                    info.id = boneId;
                    info.offsetMatrix = AiToRizeMatrix(bone->mOffsetMatrix);
                    boneInfoMap_[boneName] = info;
                    boneList_.push_back(info);
                } else {
                    boneId = it->second.id;
                }

                for (unsigned int w = 0; w < bone->mNumWeights; w++) {
                    uint32_t vId = bone->mWeights[w].mVertexId;
                    float weight = bone->mWeights[w].mWeight;
                    if (vId < mesh->mNumVertices && weight > 0.0001f) {
                        vertexBones[vId].push_back({ boneId, weight });
                    }
                }
            }

            for (unsigned int v = 0; v < mesh->mNumVertices; v++) {
                auto& bList = vertexBones[v];
                std::sort(bList.begin(), bList.end(), [](const auto& a, const auto& b) {
                    return a.second > b.second;
                });
                float totalWeight = 0.0f;
                for (size_t k = 0; k < 4 && k < bList.size(); k++) {
                    meshData.vertices[v].boneIndices[k] = bList[k].first;
                    meshData.vertices[v].boneWeights[k] = bList[k].second;
                    totalWeight += bList[k].second;
                }
                if (totalWeight > 0.0001f) {
                    for (size_t k = 0; k < 4; k++) {
                        meshData.vertices[v].boneWeights[k] /= totalWeight;
                    }
                }
            }
        } else if (scene->mNumAnimations > 0 && !meshNodeNames[i].empty()) {
            const std::string& nodeName = meshNodeNames[i];
            uint32_t boneId = 0;
            auto it = boneInfoMap_.find(nodeName);
            if (it == boneInfoMap_.end()) {
                boneId = static_cast<uint32_t>(boneList_.size());
                BoneInfo info;
                info.name = nodeName;
                info.id = boneId;
                info.offsetMatrix = Mathematics::Multiply(Mathematics::Inverse(nodeMat), rootMat);
                boneInfoMap_[nodeName] = info;
                boneList_.push_back(info);
            } else {
                boneId = it->second.id;
            }
            for (unsigned int v = 0; v < mesh->mNumVertices; v++) {
                meshData.vertices[v].boneIndices[0] = boneId;
                meshData.vertices[v].boneWeights[0] = 1.0f;
            }
        }
        
        // Indices
        for (unsigned int f = 0; f < mesh->mNumFaces; f++) {
            aiFace& face = mesh->mFaces[f];
            for (unsigned int idx = 0; idx < face.mNumIndices; idx++) {
                meshData.indices.push_back(face.mIndices[idx]);
            }
        }
        
        // Texture
        if (mesh->mMaterialIndex >= 0 && scene->mMaterials) {
            aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
            aiString texPath;
            if (material->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
                std::string rawTex = texPath.C_Str();
                size_t slash = rawTex.find_last_of("\\/");
                std::string filenameOnly = (slash != std::string::npos) ? rawTex.substr(slash + 1) : rawTex;
                std::string texFilePath = directory + "/" + filenameOnly;
                meshData.textureHandle = TextureManager::GetInstance()->LoadTexture(texFilePath);
            }
            // PBR maps next to the model (embedded textures are not supported, like the diffuse above)
            const auto loadMap = [&](std::initializer_list<aiTextureType> types, bool linear) -> uint32_t {
                for (aiTextureType type : types) {
                    aiString path;
                    if (material->GetTexture(type, 0, &path) != AI_SUCCESS || path.C_Str()[0] == '*') continue;
                    const std::string raw = path.C_Str();
                    const size_t cut = raw.find_last_of("\\/");
                    return TextureManager::GetInstance()->LoadTexture(directory + "/" + (cut != std::string::npos ? raw.substr(cut + 1) : raw), linear);
                }
                return 0;
            };
            meshData.maps.normal = loadMap({ aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA }, true);
            meshData.maps.orm = loadMap({ aiTextureType_UNKNOWN, aiTextureType_METALNESS }, true);
            meshData.maps.emissive = loadMap({ aiTextureType_EMISSIVE, aiTextureType_EMISSION_COLOR }, false);
        }
        if (meshData.textureHandle == 0) {
            meshData.textureHandle = TextureManager::GetInstance()->GetDefaultWhiteTexture();
        }
        
        meshes_.push_back(std::move(meshData));
    }

    // 3. Load Animations
    for (unsigned int a = 0; a < scene->mNumAnimations; a++) {
        aiAnimation* anim = scene->mAnimations[a];
        AnimationClip clip;
        clip.name = anim->mName.C_Str();
        if (clip.name.empty()) {
            clip.name = std::format("Animation_{}", a);
        }
        clip.duration = static_cast<float>(anim->mDuration);
        clip.ticksPerSecond = static_cast<float>(anim->mTicksPerSecond != 0.0 ? anim->mTicksPerSecond : 25.0);
        
        for (unsigned int c = 0; c < anim->mNumChannels; c++) {
            aiNodeAnim* channel = anim->mChannels[c];
            NodeAnimTrack track;
            
            track.positions.reserve(channel->mNumPositionKeys);
            for (unsigned int p = 0; p < channel->mNumPositionKeys; p++) {
                KeyVector3 k;
                k.time = static_cast<float>(channel->mPositionKeys[p].mTime);
                k.value = { channel->mPositionKeys[p].mValue.x, channel->mPositionKeys[p].mValue.y, channel->mPositionKeys[p].mValue.z };
                track.positions.push_back(k);
            }
            
            track.rotations.reserve(channel->mNumRotationKeys);
            for (unsigned int r = 0; r < channel->mNumRotationKeys; r++) {
                KeyQuat k;
                k.time = static_cast<float>(channel->mRotationKeys[r].mTime);
                k.x = channel->mRotationKeys[r].mValue.x;
                k.y = channel->mRotationKeys[r].mValue.y;
                k.z = channel->mRotationKeys[r].mValue.z;
                k.w = channel->mRotationKeys[r].mValue.w;
                track.rotations.push_back(k);
            }
            
            track.scalings.reserve(channel->mNumScalingKeys);
            for (unsigned int s = 0; s < channel->mNumScalingKeys; s++) {
                KeyVector3 k;
                k.time = static_cast<float>(channel->mScalingKeys[s].mTime);
                k.value = { channel->mScalingKeys[s].mValue.x, channel->mScalingKeys[s].mValue.y, channel->mScalingKeys[s].mValue.z };
                track.scalings.push_back(k);
            }
            
            clip.channels[channel->mNodeName.C_Str()] = std::move(track);
        }
        animations_.push_back(std::move(clip));
    }

    Log::GetInstance()->logOutput(std::format("Model loaded: {} meshes, {} bones, {} animations.\n",
        meshes_.size(), boneList_.size(), animations_.size()));
}

void Model::LoadOBJ(const std::string& directoryPath, const std::string& filename) {
    MeshData meshData;
    std::vector<Vector4> positions;
    std::vector<Vector3> normals;
    std::vector<Vector2> texcoords;
    std::string line;

    std::ifstream file(directoryPath + "/" + filename);
    if (!file.is_open()) {
        Log::GetInstance()->logOutput("Failed to open OBJ file: " + directoryPath + "/" + filename + "\n");
        return;
    }

    while (std::getline(file, line)) {
        std::string identifier;
        std::istringstream s(line);
        s >> identifier;

        if (identifier == "v") {
            Vector4 position;
            s >> position.x >> position.y >> position.z;
            position.w = 1.0f;
            positions.push_back(position);
        }
        else if (identifier == "vt") {
            Vector2 texcoord;
            s >> texcoord.x >> texcoord.y;
            texcoords.push_back({ texcoord.x, 1.0f - texcoord.y });
        }
        else if (identifier == "vn") {
            Vector3 normal;
            s >> normal.x >> normal.y >> normal.z;
            normals.push_back(normal);
        }
        else if (identifier == "f") {
            VertexData triangle[3];
            for (int32_t faceVertex = 0; faceVertex < 3; ++faceVertex) {
                std::string vertexDefinition;
                s >> vertexDefinition;

                std::istringstream v(vertexDefinition);
                uint32_t elementIndices[3] = {0, 0, 0};
                for (int32_t element = 0; element < 3; ++element) {
                    std::string index;
                    std::getline(v, index, '/');
                    if (!index.empty()) {
                        elementIndices[element] = std::stoi(index);
                    }
                }
                
                Vector4 position = {0,0,0,1};
                Vector2 texcoord = {0,0};
                Vector3 normal = {0,0,0};
                
                if (elementIndices[0] > 0 && elementIndices[0] <= positions.size()) position = positions[elementIndices[0] - 1];
                if (elementIndices[1] > 0 && elementIndices[1] <= texcoords.size()) texcoord = texcoords[elementIndices[1] - 1];
                if (elementIndices[2] > 0 && elementIndices[2] <= normals.size()) normal = normals[elementIndices[2] - 1];
                
                position.x *= -1.0f;
                normal.x *= -1.0f;
                
                VertexData vData{};
                vData.position = position;
                vData.texcoord = texcoord;
                vData.normal = normal;
                triangle[faceVertex] = vData;
            }
            meshData.vertices.push_back(triangle[2]);
            meshData.vertices.push_back(triangle[1]);
            meshData.vertices.push_back(triangle[0]);
        }
    }
    
    for (uint32_t i = 0; i < meshData.vertices.size(); i++) {
        meshData.indices.push_back(i);
    }
    
    meshData.textureHandle = TextureManager::GetInstance()->GetDefaultWhiteTexture();
    meshes_.push_back(std::move(meshData));
}

void Model::CreateGPUBuffers() {
    ComputeCachedLocalAABB();

    if (!sDxCommon_ || sDxCommon_->IsDeviceLost()) {
        return;
    }
    for (auto& mesh : meshes_) {
        if (mesh.vertices.empty() || mesh.indices.empty()) {
            continue;
        }

        // Vertex Buffer
        size_t vSizeInBytes = sizeof(VertexData) * mesh.vertices.size();
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = vSizeInBytes;
        desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
        desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        HRESULT hr = sDxCommon_->GetDevice()->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mesh.vertexBuffer));
        if (FAILED(hr) || !mesh.vertexBuffer) {
            if (!sDxCommon_->CheckDeviceLost(hr, "Model::CreateGPUBuffers(VB)")) {
                Log::GetInstance()->logOutput("Failed to create vertex buffer\n");
            }
            continue;
        }
        
        void* mappedVData = nullptr;
        mesh.vertexBuffer->Map(0, nullptr, &mappedVData);
        memcpy(mappedVData, mesh.vertices.data(), vSizeInBytes);
        mesh.vertexBuffer->Unmap(0, nullptr);
        
        mesh.vbView.BufferLocation = mesh.vertexBuffer->GetGPUVirtualAddress();
        mesh.vbView.SizeInBytes = static_cast<UINT>(vSizeInBytes);
        mesh.vbView.StrideInBytes = sizeof(VertexData);
        
        // Index Buffer
        size_t iSizeInBytes = sizeof(uint32_t) * mesh.indices.size();
        desc.Width = iSizeInBytes;
        hr = sDxCommon_->GetDevice()->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mesh.indexBuffer));
        if (FAILED(hr) || !mesh.indexBuffer) {
            if (!sDxCommon_->CheckDeviceLost(hr, "Model::CreateGPUBuffers(IB)")) {
                Log::GetInstance()->logOutput("Failed to create index buffer\n");
            }
            continue;
        }
        
        void* mappedIData = nullptr;
        mesh.indexBuffer->Map(0, nullptr, &mappedIData);
        memcpy(mappedIData, mesh.indices.data(), iSizeInBytes);
        mesh.indexBuffer->Unmap(0, nullptr);
        
        mesh.ibView.BufferLocation = mesh.indexBuffer->GetGPUVirtualAddress();
        mesh.ibView.SizeInBytes = static_cast<UINT>(iSizeInBytes);
        mesh.ibView.Format = DXGI_FORMAT_R32_UINT;
    }
}

void Model::DrawGeometry(ID3D12GraphicsCommandList* cmdList, UINT instanceCount) {
    if (!cmdList || !sDxCommon_ || sDxCommon_->IsDeviceLost()) {
        return;
    }
    for (const auto& mesh : meshes_) {
        if (!mesh.vertexBuffer || !mesh.indexBuffer || mesh.indices.empty()) {
            continue;
        }
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmdList->IASetVertexBuffers(0, 1, &mesh.vbView);
        cmdList->IASetIndexBuffer(&mesh.ibView);
        cmdList->DrawIndexedInstanced(static_cast<UINT>(mesh.indices.size()), instanceCount, 0, 0, 0);
        Profiler::GetInstance()->CountDrawCall();
    }
}

void Model::Draw(ID3D12GraphicsCommandList* cmdList, uint32_t overrideTextureHandle, const MaterialMaps* maps, UINT instanceCount) {
    if (!cmdList || !sDxCommon_ || sDxCommon_->IsDeviceLost()) {
        return;
    }
    TextureManager* textures = TextureManager::GetInstance();
    const uint32_t white = textures->GetDefaultWhiteTexture();
    const uint32_t flat = textures->GetDefaultNormalTexture();
    for (const auto& mesh : meshes_) {
        if (!mesh.vertexBuffer || !mesh.indexBuffer || mesh.indices.empty()) {
            continue;
        }

        uint32_t texHandle = (overrideTextureHandle != 0) ? overrideTextureHandle : mesh.textureHandle;
        if (texHandle == 0) {
            texHandle = TextureManager::GetInstance()->GetDefaultWhiteTexture();
        }
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = TextureManager::GetInstance()->GetGPUHandle(texHandle);
        if (gpuHandle.ptr == 0) {
            continue;
        }
        cmdList->SetGraphicsRootDescriptorTable(2, gpuHandle);
        const uint32_t normal = (maps && maps->normal) ? maps->normal : (mesh.maps.normal ? mesh.maps.normal : flat);
        const uint32_t orm = (maps && maps->orm) ? maps->orm : (mesh.maps.orm ? mesh.maps.orm : white);
        const uint32_t emissive = (maps && maps->emissive) ? maps->emissive : (mesh.maps.emissive ? mesh.maps.emissive : white);
        cmdList->SetGraphicsRootDescriptorTable(7, textures->GetGPUHandle(normal));
        cmdList->SetGraphicsRootDescriptorTable(8, textures->GetGPUHandle(orm));
        cmdList->SetGraphicsRootDescriptorTable(9, textures->GetGPUHandle(emissive));
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmdList->IASetVertexBuffers(0, 1, &mesh.vbView);
        cmdList->IASetIndexBuffer(&mesh.ibView);
        cmdList->DrawIndexedInstanced(static_cast<UINT>(mesh.indices.size()), instanceCount, 0, 0, 0);
        Profiler::GetInstance()->CountDrawCall();
    }
}

NodeData Model::BuildNodeHierarchy(void* pAiNode) {
    aiNode* node = static_cast<aiNode*>(pAiNode);
    NodeData data;
    data.name = node->mName.C_Str();
    data.transform = AiToRizeMatrix(node->mTransformation);

    aiVector3D s, p;
    aiQuaternion r;
    node->mTransformation.Decompose(s, r, p);
    data.defaultScaling = { s.x, s.y, s.z };
    data.defaultRotation = { 0.0f, r.x, r.y, r.z, r.w };
    data.defaultPosition = { p.x, p.y, p.z };

    data.children.reserve(node->mNumChildren);
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        data.children.push_back(BuildNodeHierarchy(node->mChildren[i]));
    }
    return data;
}

void Model::ReadNodeHierarchy(float animTimeTicks, const AnimationClip* pAnim, const NodeData& node, const Matrix4x4& parentTransform, std::vector<Matrix4x4>& outMatrices) const {
    Matrix4x4 nodeTransform = node.transform;

    if (pAnim) {
        auto it = pAnim->channels.find(node.name);
        if (it != pAnim->channels.end()) {
            const NodeAnimTrack& track = it->second;
            Vector3 scaling = InterpolateScaling(animTimeTicks, track, node.defaultScaling);
            KeyQuat rot = InterpolateRotation(animTimeTicks, track, node.defaultRotation);
            Vector3 translation = InterpolatePosition(animTimeTicks, track, node.defaultPosition);

            if (lockRootMotion_ && (node.name.find("Hips") != std::string::npos || node.name.find("Root") != std::string::npos)) {
                if (!track.positions.empty()) {
                    translation.x = track.positions[0].value.x;
                    translation.z = track.positions[0].value.z;
                } else {
                    translation.x = node.defaultPosition.x;
                    translation.z = node.defaultPosition.z;
                }
            }

            aiQuaternion aiRot(rot.w, rot.x, rot.y, rot.z);
            aiMatrix4x4 aiMat(aiVector3D(scaling.x, scaling.y, scaling.z), aiRot, aiVector3D(translation.x, translation.y, translation.z));
            nodeTransform = AiToRizeMatrix(aiMat);

        }
    }

    Matrix4x4 globalTransform = Mathematics::Multiply(nodeTransform, parentTransform);

    auto boneIt = boneInfoMap_.find(node.name);
    if (boneIt != boneInfoMap_.end()) {
        uint32_t boneId = boneIt->second.id;
        if (boneId < outMatrices.size()) {
            Matrix4x4 toRoot = Mathematics::Multiply(globalTransform, globalInverseTransform_);
            outMatrices[boneId] = Mathematics::Multiply(boneIt->second.offsetMatrix, toRoot);
        }
    }

    for (const auto& child : node.children) {
        ReadNodeHierarchy(animTimeTicks, pAnim, child, globalTransform, outMatrices);
    }
}

Vector3 Model::InterpolatePosition(float animTimeTicks, const NodeAnimTrack& track, const Vector3& defaultPos) {
    if (track.positions.empty()) return defaultPos;
    if (track.positions.size() == 1) return track.positions[0].value;

    size_t index = 0;
    for (size_t i = 0; i < track.positions.size() - 1; i++) {
        if (animTimeTicks < track.positions[i + 1].time) {
            index = i;
            break;
        }
    }
    size_t nextIndex = index + 1;
    float t1 = track.positions[index].time;
    float t2 = track.positions[nextIndex].time;
    float factor = (t2 > t1) ? (animTimeTicks - t1) / (t2 - t1) : 0.0f;
    factor = std::clamp(factor, 0.0f, 1.0f);
    const Vector3& p1 = track.positions[index].value;
    const Vector3& p2 = track.positions[nextIndex].value;
    return {
        p1.x + factor * (p2.x - p1.x),
        p1.y + factor * (p2.y - p1.y),
        p1.z + factor * (p2.z - p1.z)
    };
}

KeyQuat Model::InterpolateRotation(float animTimeTicks, const NodeAnimTrack& track, const KeyQuat& defaultRot) {
    if (track.rotations.empty()) return defaultRot;
    if (track.rotations.size() == 1) return track.rotations[0];

    size_t index = 0;
    for (size_t i = 0; i < track.rotations.size() - 1; i++) {
        if (animTimeTicks < track.rotations[i + 1].time) {
            index = i;
            break;
        }
    }
    size_t nextIndex = index + 1;
    float t1 = track.rotations[index].time;
    float t2 = track.rotations[nextIndex].time;
    float factor = (t2 > t1) ? (animTimeTicks - t1) / (t2 - t1) : 0.0f;
    factor = std::clamp(factor, 0.0f, 1.0f);

    const KeyQuat& r1 = track.rotations[index];
    const KeyQuat& r2 = track.rotations[nextIndex];

    aiQuaternion q1(r1.w, r1.x, r1.y, r1.z);
    aiQuaternion q2(r2.w, r2.x, r2.y, r2.z);
    aiQuaternion qOut;
    aiQuaternion::Interpolate(qOut, q1, q2, factor);
    qOut.Normalize();

    return { 0.0f, qOut.x, qOut.y, qOut.z, qOut.w };
}

Vector3 Model::InterpolateScaling(float animTimeTicks, const NodeAnimTrack& track, const Vector3& defaultScale) {
    if (track.scalings.empty()) return defaultScale;
    if (track.scalings.size() == 1) return track.scalings[0].value;

    size_t index = 0;
    for (size_t i = 0; i < track.scalings.size() - 1; i++) {
        if (animTimeTicks < track.scalings[i + 1].time) {
            index = i;
            break;
        }
    }
    size_t nextIndex = index + 1;
    float t1 = track.scalings[index].time;
    float t2 = track.scalings[nextIndex].time;
    float factor = (t2 > t1) ? (animTimeTicks - t1) / (t2 - t1) : 0.0f;
    factor = std::clamp(factor, 0.0f, 1.0f);
    const Vector3& s1 = track.scalings[index].value;
    const Vector3& s2 = track.scalings[nextIndex].value;
    return {
        s1.x + factor * (s2.x - s1.x),
        s1.y + factor * (s2.y - s1.y),
        s1.z + factor * (s2.z - s1.z)
    };
}

void Model::CalculateBoneTransformsReference(float timeInSeconds, int animIndex, std::vector<Matrix4x4>& outMatrices) const {
    outMatrices.assign(128, Mathematics::MakeIdentity4x4());

    if (boneInfoMap_.empty()) {
        return;
    }

    const AnimationClip* pAnim = nullptr;
    float timeInTicks = 0.0f;
    if (animIndex >= 0 && animIndex < static_cast<int>(animations_.size())) {
        pAnim = &animations_[animIndex];
        float tps = pAnim->ticksPerSecond > 0.0f ? pAnim->ticksPerSecond : 25.0f;
        timeInTicks = timeInSeconds * tps;
        if (pAnim->duration > 0.0f) {
            timeInTicks = std::fmod(timeInTicks, pAnim->duration);
        }
    }

    ReadNodeHierarchy(timeInTicks, pAnim, rootNode_, Mathematics::MakeIdentity4x4(), outMatrices);
}

// ---- Pose pipeline (roadmap 5.1) ----

const std::vector<Model::FlatNode>& Model::FlatNodes() const {
    if (!flatNodes_.empty() || rootNode_.name.empty()) return flatNodes_;
    // Pre-order walk: a parent always precedes its children, so globals can be built in one forward pass
    struct Item { const NodeData* node; int parent; };
    std::vector<Item> stack{ { &rootNode_, -1 } };
    while (!stack.empty()) {
        const Item item = stack.back();
        stack.pop_back();
        FlatNode flat;
        flat.node = item.node;
        flat.parent = item.parent;
        if (auto it = boneInfoMap_.find(item.node->name); it != boneInfoMap_.end()) flat.boneId = static_cast<int>(it->second.id);
        const int index = static_cast<int>(flatNodes_.size());
        flatNodes_.push_back(flat);
        for (auto child = item.node->children.rbegin(); child != item.node->children.rend(); ++child) {
            stack.push_back({ &*child, index });
        }
    }
    return flatNodes_;
}

size_t Model::GetNodeCount() const {
    return FlatNodes().size();
}

const AnimationClip* Model::GetAnimationClip(int animIndex) const {
    return (animIndex >= 0 && animIndex < static_cast<int>(animations_.size())) ? &animations_[animIndex] : nullptr;
}

int Model::FindAnimationIndex(const std::string& name) const {
    for (size_t i = 0; i < animations_.size(); ++i) {
        if (animations_[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

void Model::SampleLocalPose(const AnimationClip* clip, float timeInSeconds, bool loop, LocalPose& outPose) const {
    const std::vector<FlatNode>& nodes = FlatNodes();
    outPose.assign(nodes.size(), NodePose{});
    if (!clip) return;

    const float tps = clip->ticksPerSecond > 0.0f ? clip->ticksPerSecond : 25.0f;
    float ticks = timeInSeconds * tps;
    if (clip->duration > 0.0f) {
        // Looping wraps like the reference; a one-shot clip holds its last frame instead of jumping back to 0
        ticks = loop ? std::fmod(ticks, clip->duration) : std::clamp(ticks, 0.0f, clip->duration);
        if (ticks < 0.0f) ticks += clip->duration;
    }

    for (size_t i = 0; i < nodes.size(); ++i) {
        const NodeData& node = *nodes[i].node;
        auto it = clip->channels.find(node.name);
        if (it == clip->channels.end()) continue; // stays at the bind transform
        const NodeAnimTrack& track = it->second;
        NodePose& p = outPose[i];
        p.bind = false;
        p.scaling = InterpolateScaling(ticks, track, node.defaultScaling);
        p.rotation = InterpolateRotation(ticks, track, node.defaultRotation);
        p.translation = InterpolatePosition(ticks, track, node.defaultPosition);
        if (lockRootMotion_ && (node.name.find("Hips") != std::string::npos || node.name.find("Root") != std::string::npos)) {
            if (!track.positions.empty()) {
                p.translation.x = track.positions[0].value.x;
                p.translation.z = track.positions[0].value.z;
            } else {
                p.translation.x = node.defaultPosition.x;
                p.translation.z = node.defaultPosition.z;
            }
        }
    }
}

void Model::BlendPoses(const LocalPose& a, const LocalPose& b, float weight, LocalPose& outPose, const std::vector<uint8_t>* mask) const {
    const std::vector<FlatNode>& nodes = FlatNodes();
    const size_t count = (std::min)({ a.size(), b.size(), nodes.size() });
    const float w = std::clamp(weight, 0.0f, 1.0f);
    outPose.assign(nodes.size(), NodePose{});
    for (size_t i = 0; i < count; ++i) {
        if (a[i].bind && b[i].bind) continue;
        if (mask && i < mask->size() && (*mask)[i] == 0) {
            outPose[i] = a[i];
            continue;
        }
        const NodeData& node = *nodes[i].node;
        // A node animated on one side only blends against its bind (decomposed default) transform
        const NodePose bindPose{ node.defaultPosition, node.defaultRotation, node.defaultScaling, false };
        const NodePose& pa = a[i].bind ? bindPose : a[i];
        const NodePose& pb = b[i].bind ? bindPose : b[i];
        NodePose& o = outPose[i];
        o.bind = false;
        o.translation = pa.translation + (pb.translation - pa.translation) * w;
        o.scaling = pa.scaling + (pb.scaling - pa.scaling) * w;
        aiQuaternion qa(pa.rotation.w, pa.rotation.x, pa.rotation.y, pa.rotation.z);
        aiQuaternion qb(pb.rotation.w, pb.rotation.x, pb.rotation.y, pb.rotation.z);
        aiQuaternion q;
        aiQuaternion::Interpolate(q, qa, qb, w); // shortest path
        q.Normalize();
        o.rotation = { 0.0f, q.x, q.y, q.z, q.w };
    }
}

void Model::ComputeGlobalTransforms(const LocalPose& pose, std::vector<Matrix4x4>& outGlobals, const std::vector<std::pair<int, Matrix4x4>>* overrides) const {
    const std::vector<FlatNode>& nodes = FlatNodes();
    outGlobals.resize(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        Matrix4x4 local = nodes[i].node->transform;
        if (i < pose.size() && !pose[i].bind) {
            const NodePose& p = pose[i];
            aiQuaternion rot(p.rotation.w, p.rotation.x, p.rotation.y, p.rotation.z);
            aiMatrix4x4 m(aiVector3D(p.scaling.x, p.scaling.y, p.scaling.z), rot, aiVector3D(p.translation.x, p.translation.y, p.translation.z));
            local = AiToRizeMatrix(m);
        }
        const int parent = nodes[i].parent;
        outGlobals[i] = (parent >= 0) ? Mathematics::Multiply(local, outGlobals[parent]) : local;
        if (overrides) {
            for (const auto& [node, modelMatrix] : *overrides) {
                if (node == static_cast<int>(i)) {
                    outGlobals[i] = Mathematics::Multiply(modelMatrix, Mathematics::Inverse(globalInverseTransform_));
                    break;
                }
            }
        }
    }
}

void Model::BuildSkinMatrices(const LocalPose& pose, std::vector<Matrix4x4>& outMatrices, const std::vector<std::pair<int, Matrix4x4>>* overrides) const {
    outMatrices.assign(128, Mathematics::MakeIdentity4x4());
    if (boneInfoMap_.empty()) return;
    std::vector<Matrix4x4> globals;
    ComputeGlobalTransforms(pose, globals, overrides);
    const std::vector<FlatNode>& nodes = FlatNodes();
    for (size_t i = 0; i < nodes.size(); ++i) {
        const int boneId = nodes[i].boneId;
        if (boneId < 0 || boneId >= static_cast<int>(outMatrices.size())) continue;
        const Matrix4x4 toRoot = Mathematics::Multiply(globals[i], globalInverseTransform_);
        outMatrices[boneId] = Mathematics::Multiply(boneInfoMap_.at(nodes[i].node->name).offsetMatrix, toRoot);
    }
}

int Model::FindNode(const std::string& name) const {
    const std::vector<FlatNode>& nodes = FlatNodes();
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].node->name == name) return static_cast<int>(i);
    }
    return -1;
}

std::vector<uint8_t> Model::MakeNodeMask(const std::string& rootName) const {
    const std::vector<FlatNode>& nodes = FlatNodes();
    const int root = rootName.empty() ? -1 : FindNode(rootName);
    std::vector<uint8_t> mask(nodes.size(), root < 0 ? 1 : 0);
    if (root >= 0) {
        mask[root] = 1;
        for (size_t i = root + 1; i < nodes.size(); ++i) { // pre-order: parents come first
            if (nodes[i].parent >= 0 && mask[nodes[i].parent]) mask[i] = 1;
        }
    }
    return mask;
}

int Model::RootBoneNode() const {
    const std::vector<FlatNode>& nodes = FlatNodes();
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].boneId >= 0) return static_cast<int>(i);
    }
    return -1;
}

bool Model::GetNodeModelMatrix(const LocalPose& pose, int node, Matrix4x4& out, const std::vector<std::pair<int, Matrix4x4>>* overrides) const {
    if (node < 0 || node >= static_cast<int>(FlatNodes().size())) return false;
    std::vector<Matrix4x4> globals;
    ComputeGlobalTransforms(pose, globals, overrides);
    out = Mathematics::Multiply(globals[node], globalInverseTransform_);
    return true;
}

namespace {
// Row-vector rotation helpers for IK; everything else in this file goes through assimp's column-vector types
Vector3 RowPosition(const Matrix4x4& m) { return { m.m[3][0], m.m[3][1], m.m[3][2] }; }
float RowDot(const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vector3 RowCross(const Vector3& a, const Vector3& b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
Vector3 Normalized(const Vector3& v, const Vector3& fallback) {
    const float len = std::sqrt(RowDot(v, v));
    return len > 1.0e-6f ? Vector3{ v.x / len, v.y / len, v.z / len } : fallback;
}

// The rotation part of a matrix that may also scale
Matrix4x4 RotationOnly(const Matrix4x4& m) {
    Matrix4x4 r = Mathematics::MakeIdentity4x4();
    for (int row = 0; row < 3; ++row) {
        const Vector3 axis = Normalized({ m.m[row][0], m.m[row][1], m.m[row][2] }, { 0.0f, 0.0f, 0.0f });
        r.m[row][0] = axis.x; r.m[row][1] = axis.y; r.m[row][2] = axis.z;
    }
    return r;
}

// Row-vector matrix taking direction `from` to direction `to` (Rodrigues)
Matrix4x4 RotationBetween(const Vector3& from, const Vector3& to) {
    const Vector3 a = Normalized(from, { 0.0f, 1.0f, 0.0f });
    const Vector3 b = Normalized(to, { 0.0f, 1.0f, 0.0f });
    const float c = RowDot(a, b);
    Vector3 v = RowCross(a, b);
    if (c < -0.9999f) { // opposite: any perpendicular axis
        v = Normalized(RowCross(a, std::abs(a.y) < 0.9f ? Vector3{ 0.0f, 1.0f, 0.0f } : Vector3{ 1.0f, 0.0f, 0.0f }), { 1.0f, 0.0f, 0.0f });
        Matrix4x4 m = Mathematics::MakeIdentity4x4();
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                const float vi = i == 0 ? v.x : i == 1 ? v.y : v.z;
                const float vj = j == 0 ? v.x : j == 1 ? v.y : v.z;
                m.m[i][j] = 2.0f * vi * vj - (i == j ? 1.0f : 0.0f);
            }
        }
        return m;
    }
    const float k = 1.0f / (1.0f + c);
    // Column form R = I + [v]x + k [v]x^2; row-vector form is its transpose
    Matrix4x4 m = Mathematics::MakeIdentity4x4();
    m.m[0][0] = v.x * v.x * k + c;       m.m[1][0] = v.x * v.y * k - v.z;     m.m[2][0] = v.x * v.z * k + v.y;
    m.m[0][1] = v.y * v.x * k + v.z;     m.m[1][1] = v.y * v.y * k + c;       m.m[2][1] = v.y * v.z * k - v.x;
    m.m[0][2] = v.z * v.x * k - v.y;     m.m[1][2] = v.z * v.y * k + v.x;     m.m[2][2] = v.z * v.z * k + c;
    return m;
}

Matrix4x4 QuatToRowMatrix(const KeyQuat& q) {
    const aiMatrix3x3 c = aiQuaternion(q.w, q.x, q.y, q.z).GetMatrix();
    Matrix4x4 m = Mathematics::MakeIdentity4x4();
    m.m[0][0] = c.a1; m.m[1][0] = c.a2; m.m[2][0] = c.a3;
    m.m[0][1] = c.b1; m.m[1][1] = c.b2; m.m[2][1] = c.b3;
    m.m[0][2] = c.c1; m.m[1][2] = c.c2; m.m[2][2] = c.c3;
    return m;
}

KeyQuat RowMatrixToQuat(const Matrix4x4& m) {
    aiMatrix3x3 c;
    c.a1 = m.m[0][0]; c.a2 = m.m[1][0]; c.a3 = m.m[2][0];
    c.b1 = m.m[0][1]; c.b2 = m.m[1][1]; c.b3 = m.m[2][1];
    c.c1 = m.m[0][2]; c.c2 = m.m[1][2]; c.c3 = m.m[2][2];
    aiQuaternion q(c);
    q.Normalize();
    return { 0.0f, q.x, q.y, q.z, q.w };
}
}

bool Model::SolveTwoBoneIK(LocalPose& pose, int endNode, const Vector3& target, const Vector3& pole, float weight) const {
    const std::vector<FlatNode>& nodes = FlatNodes();
    if (endNode < 0 || endNode >= static_cast<int>(nodes.size()) || pose.size() != nodes.size()) return false;
    const int mid = nodes[endNode].parent;
    const int upper = mid >= 0 ? nodes[mid].parent : -1;
    if (upper < 0) return false;

    std::vector<Matrix4x4> globals;
    ComputeGlobalTransforms(pose, globals);
    const auto modelOf = [&](int i) { return Mathematics::Multiply(globals[i], globalInverseTransform_); };
    const Vector3 a = RowPosition(modelOf(upper));
    const Vector3 b = RowPosition(modelOf(mid));
    const Vector3 c = RowPosition(modelOf(endNode));
    const Vector3 ab = b - a;
    const Vector3 bc = c - b;
    const float upperLength = std::sqrt(RowDot(ab, ab));
    const float lowerLength = std::sqrt(RowDot(bc, bc));
    if (upperLength < 1.0e-5f || lowerLength < 1.0e-5f) return false;

    const Vector3 toTarget = target - a;
    const float reachMin = std::abs(upperLength - lowerLength) + 1.0e-3f;
    const float reachMax = upperLength + lowerLength - 1.0e-3f;
    const float distance = std::clamp(std::sqrt(RowDot(toTarget, toTarget)), reachMin, reachMax);
    const Vector3 direction = Normalized(toTarget, Normalized(c - a, { 0.0f, -1.0f, 0.0f }));
    // Law of cosines: how far along the line the knee lies, and how far off it
    const float along = (upperLength * upperLength - lowerLength * lowerLength + distance * distance) / (2.0f * distance);
    const float off = std::sqrt((std::max)(upperLength * upperLength - along * along, 0.0f));
    const Vector3 toPole = pole - a;
    const float poleAlong = RowDot(toPole, direction);
    Vector3 bend = Normalized(toPole - direction * poleAlong, { 0.0f, 0.0f, 0.0f });
    if (RowDot(bend, bend) < 0.5f) { // pole on the line: keep the current bend
        const float abAlong = RowDot(ab, direction);
        bend = Normalized(ab - direction * abAlong, std::abs(direction.y) < 0.9f ? Vector3{ 0.0f, 1.0f, 0.0f } : Vector3{ 1.0f, 0.0f, 0.0f });
    }
    const Vector3 newB = a + direction * along + bend * off;
    const Vector3 newC = a + direction * distance;

    // Local rotation so the bone's world rotation gains `worldDelta`: L' = L * P * R * P^-1 (row vectors)
    const auto applyDelta = [&](int node, const Matrix4x4& parentRotation, const Matrix4x4& worldDelta) {
        NodePose& p = pose[node];
        if (p.bind) {
            const NodeData& data = *nodes[node].node;
            p = { data.defaultPosition, data.defaultRotation, data.defaultScaling, false };
        }
        const Matrix4x4 inverseParent = Mathematics::Inverse(parentRotation);
        const Matrix4x4 changed = Mathematics::Multiply(Mathematics::Multiply(Mathematics::Multiply(QuatToRowMatrix(p.rotation), parentRotation), worldDelta), inverseParent);
        const KeyQuat solved = RowMatrixToQuat(changed);
        aiQuaternion blended;
        aiQuaternion::Interpolate(blended, aiQuaternion(p.rotation.w, p.rotation.x, p.rotation.y, p.rotation.z), aiQuaternion(solved.w, solved.x, solved.y, solved.z), std::clamp(weight, 0.0f, 1.0f));
        blended.Normalize();
        p.rotation = { 0.0f, blended.x, blended.y, blended.z, blended.w };
    };

    const int upperParent = nodes[upper].parent;
    const Matrix4x4 upperParentRotation = upperParent >= 0 ? RotationOnly(modelOf(upperParent)) : Mathematics::MakeIdentity4x4();
    const Matrix4x4 upperDelta = RotationBetween(ab, newB - a);
    applyDelta(upper, upperParentRotation, upperDelta);

    // Where the lower bone points once the upper one has turned
    const Matrix4x4& d = upperDelta;
    const Vector3 turnedLower = {
        bc.x * d.m[0][0] + bc.y * d.m[1][0] + bc.z * d.m[2][0],
        bc.x * d.m[0][1] + bc.y * d.m[1][1] + bc.z * d.m[2][1],
        bc.x * d.m[0][2] + bc.y * d.m[1][2] + bc.z * d.m[2][2] };
    const Matrix4x4 midParentRotation = Mathematics::Multiply(RotationOnly(modelOf(upper)), upperDelta);
    applyDelta(mid, midParentRotation, RotationBetween(turnedLower, newC - newB));
    return true;
}

std::vector<Model::RagdollBone> Model::BuildRagdollBones(const LocalPose& pose) const {
    std::vector<RagdollBone> result;
    const std::vector<FlatNode>& nodes = FlatNodes();
    std::vector<Matrix4x4> globals;
    ComputeGlobalTransforms(pose, globals);
    std::vector<Vector3> positions(nodes.size());
    Vector3 mn{ FLT_MAX, FLT_MAX, FLT_MAX }, mx{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (size_t i = 0; i < nodes.size(); ++i) {
        positions[i] = RowPosition(Mathematics::Multiply(globals[i], globalInverseTransform_));
        if (nodes[i].boneId < 0) continue;
        mn = { (std::min)(mn.x, positions[i].x), (std::min)(mn.y, positions[i].y), (std::min)(mn.z, positions[i].z) };
        mx = { (std::max)(mx.x, positions[i].x), (std::max)(mx.y, positions[i].y), (std::max)(mx.z, positions[i].z) };
    }
    const Vector3 size = mx - mn;
    float threshold = std::sqrt(RowDot(size, size)) * 0.06f;

    // Longest child bone of every bone
    std::vector<int> child(nodes.size(), -1);
    std::vector<float> length(nodes.size(), 0.0f);
    for (size_t i = 0; i < nodes.size(); ++i) {
        const int parent = nodes[i].parent;
        if (parent < 0 || nodes[i].boneId < 0 || nodes[parent].boneId < 0) continue;
        const Vector3 d = positions[i] - positions[parent];
        const float len = std::sqrt(RowDot(d, d));
        if (len > length[parent]) {
            length[parent] = len;
            child[parent] = static_cast<int>(i);
        }
    }

    for (int attempt = 0; attempt < 8; ++attempt, threshold *= 1.25f) {
        result.clear();
        std::vector<int> partOfNode(nodes.size(), -1);
        for (size_t i = 0; i < nodes.size(); ++i) {
            const int parent = nodes[i].parent;
            const int inherited = parent >= 0 ? partOfNode[parent] : -1;
            partOfNode[i] = inherited;
            // The root is always a part, however short: everything else hangs from it
            const bool isRoot = result.empty() && nodes[i].boneId >= 0;
            if (child[i] < 0 || (length[i] < threshold && !isRoot)) continue;
            RagdollBone bone;
            bone.node = static_cast<int>(i);
            bone.start = positions[i];
            bone.end = positions[child[i]];
            if (length[i] < threshold) {
                const Vector3 along = Normalized(bone.end - bone.start, { 0.0f, 1.0f, 0.0f });
                bone.end = bone.start + along * threshold;
            }
            bone.parentPart = inherited;
            partOfNode[i] = static_cast<int>(result.size());
            result.push_back(bone);
        }
        if (result.size() <= 16) break;
    }
    return result;
}
void Model::GetJointPositions(const LocalPose& pose, std::vector<Vector3>& outPositions, std::vector<int>& outParents, std::vector<bool>& outIsBone) const {
    std::vector<Matrix4x4> globals;
    ComputeGlobalTransforms(pose, globals);
    const std::vector<FlatNode>& nodes = FlatNodes();
    outPositions.resize(nodes.size());
    outParents.resize(nodes.size());
    outIsBone.resize(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        const Matrix4x4 m = Mathematics::Multiply(globals[i], globalInverseTransform_);
        outPositions[i] = { m.m[3][0], m.m[3][1], m.m[3][2] }; // row-vector convention: translation in row 3
        outParents[i] = nodes[i].parent;
        outIsBone[i] = nodes[i].boneId >= 0;
    }
}

void Model::CalculateBoneTransforms(float timeInSeconds, int animIndex, std::vector<Matrix4x4>& outMatrices) const {
    LocalPose pose;
    SampleLocalPose(GetAnimationClip(animIndex), timeInSeconds, true, pose);
    BuildSkinMatrices(pose, outMatrices);
}

float Model::GetAnimationDuration(int animIndex) const {
    if (animIndex >= 0 && animIndex < static_cast<int>(animations_.size())) {
        float tps = animations_[animIndex].ticksPerSecond > 0.0f ? animations_[animIndex].ticksPerSecond : 25.0f;
        return animations_[animIndex].duration / tps;
    }
    return 0.0f;
}

const std::string& Model::GetAnimationName(int animIndex) const {
    static const std::string kEmpty = "";
    if (animIndex >= 0 && animIndex < static_cast<int>(animations_.size())) {
        return animations_[animIndex].name;
    }
    return kEmpty;
}
