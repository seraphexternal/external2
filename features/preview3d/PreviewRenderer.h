#pragma once
#include "ModelLoader.h"
#include <d3d11.h>
#include <array>
#include <string>
#include <vector>
#include <utility>

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
