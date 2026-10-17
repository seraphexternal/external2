#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace Cheat::Core {

struct ModelVertex {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
    // Skinning (optional, filled by PreviewRenderer when loading):
    uint8_t boneIndices[4] = {0, 0, 0, 0};
    float   boneWeights[4] = {0.f, 0.f, 0.f, 0.f};
};

struct ModelTri {
    int i0, i1, i2;
};

struct ModelPartAABB {
    float min[3];
    float max[3];
    bool  valid = false;
};

// A drawable slice of the loaded model with its own texture (GLB submesh).
// base_vertex/count index into LoadedModel::vertices (the body triangle-list).
struct LoadedSubMesh {
    unsigned int base_vertex = 0;
    unsigned int vertex_count = 0;
    std::vector<unsigned char> texture; // raw PNG/JPEG bytes for this slice
};

// One glTF node (skeleton bone or mesh holder). Matrix is row-major (D3D
// convention, row-vector math: p' = p * M) matching the renderer's XMMATRIX.
struct ModelNode {
    std::string name;
    int parent = -1;
    float local[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    int jointSlot = -1;   // index into LoadedModel::skinJoints, or -1
};

struct LoadedModel {
    std::vector<ModelVertex>   vertices;
    std::vector<ModelVertex>   wing_vertices;
    std::vector<float>         positions;
    std::vector<ModelTri>      tris;
    std::vector<ModelTri>      body_tris;
    std::vector<float>         body_positions;
    std::vector<float>         box_positions;
    std::vector<ModelPartAABB> body_parts;
    std::vector<ModelPartAABB> box_parts;
    std::vector<LoadedSubMesh> submeshes;
    // Skeletal animation (glTF skins). Filled only for rigged models.
    std::vector<ModelNode> nodes;        // full node hierarchy (bind pose)
    std::vector<int>       skinJoints;   // node index per joint slot
    std::vector<float>     invBind;      // 16 floats per slot, row-major
    int meshNode = -1;       // index of the node holding the (skinned) mesh
    bool hasSkin = false;
    float scale = 1.0f;
    float center[3] = {};
    float raw_min[3] = {};
    float raw_max[3] = {};
    float body_min[3] = {};
    float body_max[3] = {};
    float box_min[3] = {};
    float box_max[3] = {};
};

struct GLBTexture {
    std::vector<unsigned char> data;
    int width = 0;
    int height = 0;
};

bool LoadOBJ(const std::string& path, LoadedModel& out);
bool LoadOBJFromMemory(const char* src, std::size_t len, LoadedModel& out);
bool LoadGLB(const std::string& path, LoadedModel& out, GLBTexture* embedded_tex = nullptr);

}
