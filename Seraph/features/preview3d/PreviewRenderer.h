#pragma once
#include "ModelLoader.h"
#include <d3d11.h>
#include <array>
#include <string>
#include <vector>
#include <utility>
#include <DirectXMath.h>

namespace Cheat::Core {

class PreviewRenderer {
public:
    PreviewRenderer() = default;
    ~PreviewRenderer() { Shutdown(); }

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* ctx,
                    unsigned int rt_width = 600, unsigned int rt_height = 900);

    bool LoadModel(const std::string& obj_path);
    bool LoadModelFromMemory(const char* obj_src, std::size_t obj_len);
    bool LoadTexture(const std::string& tex_path);
    bool LoadTextureFromMemory(const unsigned char* png, std::size_t png_len);

    void Update(float dt_seconds);
    void AddRotationDelta(float dyaw, float dpitch);
    void AddZoom(float delta);
    void SetAutoSpin(bool on) {
        m_AutoSpin = on;
        if (on) m_SpinPauseRemaining = 0.0f;
    }
    void SetModelYawOffset(float yaw) { m_ModelYawOffset = yaw; m_SceneDirty = true; }
    void SetModelPitchOffset(float pitch) { m_ModelPitchOffset = pitch; m_SceneDirty = true; }
    void SetSkeletonUpAxis(int axis) { m_SkelUpAxis = axis; }
    bool IsAutoSpinning() const { return m_AutoSpin; }
    void NotifyManualInput() { m_SpinPauseRemaining = 2.0f; }

    // Absolute view rotation, for callers that drive the model's facing from
    // outside (instead of the preview's drag/spin input).
    void SetViewYaw(float yaw) { m_ManualYaw = yaw; m_SceneDirty = true; }
    void SetViewPitch(float pitch) { m_ManualPitch = pitch; m_SceneDirty = true; }
    // Background colour of the render target. The Mode Changer sets alpha 0 so
    // only the model shows when the RT is blitted over the character.
    void SetClearColor(float r, float g, float b, float a) {
        m_ClearColor[0] = r; m_ClearColor[1] = g; m_ClearColor[2] = b; m_ClearColor[3] = a;
        m_SceneDirty = true;
    }
    void SetOrthographic(bool ortho) { m_UseOrthographic = ortho; m_SceneDirty = true; }
    void ResetView() {
        m_ManualYaw = 0.0f;
        m_ManualPitch = 0.0f;
        m_Angle = 0.0f;
        m_SpinAccum = 0.0f;
        m_Zoom = 0.936f;
        m_SceneDirty = true;
    }

    void* GetTextureID() const;
    bool SaveRT(const char* path);   // DEBUG ONLY
    bool GetProjectedUVBounds(float& u0, float& v0, float& u1, float& v1) const;

    bool GetProjectedPartBoxes(std::vector<std::array<std::pair<float, float>, 8>>& out) const;
    bool GetProjectedAimPartBoxes(
        std::vector<std::pair<int, std::array<std::pair<float, float>, 8>>>& out) const;
    bool GetProjectedAimPartCenters(
        std::vector<std::pair<int, std::pair<float, float>>>& out) const;
    bool GetProjectedR6Skeleton(std::vector<float>& out_uv_segs) const;
    bool GetProjectedModelBox(std::vector<float>& out_uv_corners) const;

    void SetSkeletonHead(float x, float y, float z);
    bool GetProjectedHead(float& u, float& v) const;

    // ── Skeletal animation (glTF skins) ──────────────────────────────────────
    // True when the loaded model has a skin + per-vertex bone data.
    bool HasSkin() const { return m_HasSkin; }
    // Number of joints in the loaded skeleton (0 when not skinned).
    int  JointCount() const { return (int)m_SkinJoints.size(); }
    // Names of the skeleton joints (parallel to the joint slots).
    const char* JointName(int slot) const;
    // Reset every joint to the bind pose.
    void ClearPose();
    // Set a bind-relative rotation for the joint whose glTF node name starts
    // with `jointName` (e.g. "Right Arm"). Rotation is applied in MODEL space
    // about the joint's pivot (axis is a model-space axis, angle in RADIANS).
    // Replaces any existing rotation for that joint.
    bool SetJointRotationAxis(const char* jointName, float ax, float ay, float az, float angleRad);
    // Composes an additional model-space rotation onto a joint (applied after
    // the existing one). Use this to combine e.g. a lift + a swing.
    bool AddJointRotationAxis(const char* jointName, float ax, float ay, float az, float angleRad);
    // Convenience: rotate about the model X / Y / Z axes.
    bool RotateJointX(const char* jointName, float angleRad);
    bool RotateJointY(const char* jointName, float angleRad);
    bool RotateJointZ(const char* jointName, float angleRad);

    // ── Absolute pose (player mirror) ────────────────────────────────────────
    // Set an ABSOLUTE joint rotation, expressed in the semantic frame the emotes
    // use (X = side, Y = up, Z = depth) and mapped onto the loaded model's axes
    // exactly like the emote helpers. Unlike SetJointRotationAxis (a delta about
    // one axis), this replaces the joint's whole orientation and is composed
    // down the hierarchy, so a child attaches to its animated parent. Identity
    // means "bind pose". Each call overwrites the joint; call ClearWorldDeltas()
    // first to reset the ones you no longer drive. Cleared by ClearPose() too.
    bool SetJointDeltaSemantic(const char* jointName,
                               float qx, float qy, float qz, float qw);
    void ClearWorldDeltas();

    // Local (parent-relative) joint rotation for keyframe animation: `q` is
    // expressed in the semantic frame (X = side, Y = up, Z = depth) and mapped
    // onto the loaded model's axes like the emote helpers. Applied down the
    // hierarchy: the child rotates about its own pivot in the already-rotated
    // parent, so limbs stay attached. Replaces any existing rotation; reset with
    // ClearPose().
    bool SetJointLocalQuat(const char* jointName,
                           float qx, float qy, float qz, float qw);

    bool LoadModel(Cheat::Core::LoadedModel& model) { return ApplyLoadedModel(model); }

    bool IsReady() const { return m_Ready && m_VB != nullptr && m_BodyVertCount > 0; }
    void Shutdown();

private:
    bool CreateRenderTarget(unsigned int w, unsigned int h);
    bool CreateShaders();
    bool ApplyLoadedModel(LoadedModel& model);
    void BuildR6SkeletonFromParts();
    void BuildGenericSkeletonFromBounds();
    void BuildAimPartBoxes();
    // Skeleton helpers.
    void ComputeBindWorlds();
    void FinalizeSkin();
    void SynthesizeSkin(std::vector<ModelVertex>& verts);
    // Derives the model's semantic axes (up / side / depth) from the bind joint
    // pivots so the emote system's X/Y/Z rotations map onto them regardless of
    // the model's authored orientation (Y-up humanoid, Z-up plush, etc.).
    void UpdateEmoteAxes();
    void RecomputeBoneMatrices();
    int  FindJointSlot(const char* jointName) const;
    float WingAlpha() const;

    struct AimPartAABB {
        float min[3]{};
        float max[3]{};
        bool valid = false;
    };
    static constexpr int AimPartCount = 8;
    AimPartAABB m_AimParts[AimPartCount]{};

    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Ctx = nullptr;

    ID3D11Texture2D* m_RTTex = nullptr;
    ID3D11RenderTargetView* m_RTV = nullptr;
    ID3D11Texture2D* m_ResolveTex = nullptr;
    ID3D11ShaderResourceView* m_SRV = nullptr;
    ID3D11Texture2D* m_DepthTex = nullptr;
    ID3D11DepthStencilView* m_DSV = nullptr;
    unsigned int m_Width = 0;
    unsigned int m_Height = 0;
    unsigned int m_SampleCount = 1;

    ID3D11VertexShader* m_VS = nullptr;
    ID3D11PixelShader* m_PS = nullptr;
    ID3D11InputLayout* m_IL = nullptr;
    ID3D11Buffer* m_CB = nullptr;
    ID3D11Buffer* m_VB = nullptr;
    unsigned int m_BodyVertCount = 0;
    unsigned int m_WingVertCount = 0;

    // ── Skeleton skinning state ──────────────────────────────────────────────
    static constexpr int MaxBones = 96;
    ID3D11Buffer* m_BoneCB = nullptr;
    bool m_HasSkin = false;
    bool m_PoseDirty = true;
    std::vector<ModelNode> m_Nodes;     // bind node hierarchy
    std::vector<int>       m_SkinJoints;// node index per joint slot
    std::vector<float>     m_InvBind;   // 16 per slot, row-major
    std::vector<float>     m_NodeWorld; // 16 per node, bind world, row-major
    std::vector<int>       m_NodeOrder; // parents-before-children traversal
    // Per-node override rotation as a quaternion (x,y,z,w); identity default.
    std::vector<float>     m_JointQuat; // 4 per node
    // Per-node ABSOLUTE model-space rotation delta (player mirror), applied
    // down the hierarchy. m_AbsSet marks which nodes are driven this way.
    std::vector<float>         m_AbsQuat; // 4 per node
    std::vector<unsigned char> m_AbsSet;  // 1 per node
    std::vector<float>     m_BoneMatrices; // 16 per slot, row-major

    float m_ModelScale = 1.0f;
    float m_ModelCenter[3] = {};
    float m_FrameScale = 1.0f;
    float m_FrameCenter[3] = {};
    float m_RawMin[3] = {};
    float m_RawMax[3] = {};
    float m_BodyMin[3] = {};
    float m_BodyMax[3] = {};
    float m_BoxMin[3] = {};
    float m_BoxMax[3] = {};
    std::vector<float> m_UniquePos;
    std::vector<float> m_BodyPos;
    std::vector<float> m_BoxPos;
    std::vector<ModelPartAABB> m_BodyParts;
    std::vector<ModelPartAABB> m_BoxParts;

    static constexpr int MaxSkelSegs = 12;
    float m_SkelSeg[MaxSkelSegs][2][3] = {};
    int m_SkelSegCount = 0;
    // Preferred skeleton up axis (0=X,1=Y,2=Z), or -1 for auto = largest extent.
    // Set explicitly because a wide arm-span pose (e.g. Mario) can make the largest
    // extent be a LATERAL axis, which would lay the skeleton out sideways.
    int m_SkelUpAxis = -1;

    // Semantic model-space axes for the emote system. The emote poses are
    // authored in a frame where X = side, Y = up and Z = depth; these unit
    // vectors map that frame onto the loaded model's actual orientation.
    float m_EmoteAxisUp[3]    = { 0.0f, 1.0f, 0.0f };
    float m_EmoteAxisSide[3]  = { 1.0f, 0.0f, 0.0f };
    float m_EmoteAxisDepth[3] = { 0.0f, 0.0f, 1.0f };
    void MapEmoteAxis(float ax, float ay, float az, float out[3]) const;
    // Re-express a semantic-frame quaternion (X = side, Y = up, Z = depth) as a
    // model-space quaternion using the derived emote axes.
    void SemanticToModelQuat(const float qsem[4], float out[4]);

    ID3D11Texture2D* m_TexRes = nullptr;
    ID3D11ShaderResourceView* m_TexSRV = nullptr;
    ID3D11SamplerState* m_Sampler = nullptr;

    // Per-submesh draw slices + their own texture SRVs (GLB multi-material).
    static constexpr int MaxSubMeshes = 64;
    struct SubMeshDraw {
        ID3D11ShaderResourceView* srv = nullptr;
        unsigned int base = 0;
        unsigned int count = 0;
    };
    SubMeshDraw m_SubMeshes[MaxSubMeshes]{};
    int m_SubMeshCount = 0;
    ID3D11ShaderResourceView* RecreateSubTexSRV(ID3D11ShaderResourceView*& srv,
                                                const std::vector<unsigned char>& bytes);

    ID3D11RasterizerState* m_RS = nullptr;
    ID3D11DepthStencilState* m_DSState = nullptr;
    ID3D11DepthStencilState* m_DSNoWrite = nullptr;
    ID3D11BlendState* m_BlendOpaque = nullptr;
    ID3D11BlendState* m_BlendAlpha = nullptr;

    float m_ClearColor[4] = { 0.05f, 0.05f, 0.08f, 1.0f };
    float m_Angle = 0.0f;
    float m_ModelYawOffset = 0.0f;
    float m_ModelPitchOffset = 0.0f;
    float m_ManualYaw = 0.0f;
    float m_ManualPitch = 0.0f;
    float m_Zoom = 0.936f; // чуть ближе чем 0.72
    bool m_AutoSpin = false;
    float m_SpinPauseRemaining = 0.0f;
    float m_SpinAccum = 0.0f;
    bool m_SceneDirty = true;
    float m_WingAnimTime = 0.0f;
    bool m_UseOrthographic = true; // Use orthographic projection for consistent model size

    float m_LastMVP[16] = {};
    bool m_LastMVPValid = false;

    mutable float m_CachedU0 = 0, m_CachedV0 = 0, m_CachedU1 = 1, m_CachedV1 = 1;
    mutable bool m_CachedBoundsValid = false;

    // Model-space point at the character's head (top end of the up axis),
    // used to anchor external markers (e.g. the ESP Head Circle) onto the model.
    float m_SkelHead[3] = { 0.0f, 0.0f, 0.0f };
    bool m_SkelHeadValid = false;

    bool m_Ready = false;
};

}
