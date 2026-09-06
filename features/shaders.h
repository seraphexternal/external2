#pragma once

#define NOMINMAX
#include <d3d11.h>
#include <d3dcompiler.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <algorithm>
#include <cstring>
#include <cmath>
#include "../rbx/math/math.h"

#pragma comment(lib, "d3dcompiler.lib")

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace sdk
{
    struct vec3_t { float x, y, z; };
    struct mat3_t { float m[3][3]; };
    struct mat4_t { float m[4][4]; };

    struct mesh_vertex_t { float pos[3]; float normal[3]; };
    struct mesh_face_t { uint32_t indices[3]; };
    struct mesh_data_t
    {
        std::vector<mesh_vertex_t> vertices;
        std::vector<mesh_face_t> faces;
        vec3_t bounds_min{};
        vec3_t bounds_max{};
    };

    inline std::string extract_asset_id(const std::string& id) { return id; }

    inline vec3_t from_vec3(const Vectors::Vector3& v) { return { v.x, v.y, v.z }; }

    inline mat3_t from_cframe(const sCFrame& cf)
    {
        mat3_t r;
        r.m[0][0] = cf.r00; r.m[0][1] = cf.r01; r.m[0][2] = cf.r02;
        r.m[1][0] = cf.r10; r.m[1][1] = cf.r11; r.m[1][2] = cf.r12;
        r.m[2][0] = cf.r20; r.m[2][1] = cf.r21; r.m[2][2] = cf.r22;
        return r;
    }

    inline mat4_t from_viewmatrix(const Matrixes::Matrix4& m)
    {
        mat4_t r;
        std::memcpy(r.m, m.data, sizeof(float) * 16);
        return r;
    }

    inline mesh_data_t make_unit_box()
    {
        mesh_data_t mesh;
        mesh.bounds_min = { -0.5f, -0.5f, -0.5f };
        mesh.bounds_max = { 0.5f, 0.5f, 0.5f };

        struct FaceDef { vec3_t normal; vec3_t verts[4]; };
        FaceDef faces[6] = {
            {{ 0, 0, 1}, {{-0.5f,-0.5f, 0.5f},{ 0.5f,-0.5f, 0.5f},{ 0.5f, 0.5f, 0.5f},{-0.5f, 0.5f, 0.5f}}},
            {{ 0, 0,-1}, {{ 0.5f,-0.5f,-0.5f},{-0.5f,-0.5f,-0.5f},{-0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f}}},
            {{ 0, 1, 0}, {{-0.5f, 0.5f, 0.5f},{ 0.5f, 0.5f, 0.5f},{ 0.5f, 0.5f,-0.5f},{-0.5f, 0.5f,-0.5f}}},
            {{ 0,-1, 0}, {{-0.5f,-0.5f,-0.5f},{ 0.5f,-0.5f,-0.5f},{ 0.5f,-0.5f, 0.5f},{-0.5f,-0.5f, 0.5f}}},
            {{ 1, 0, 0}, {{ 0.5f,-0.5f, 0.5f},{ 0.5f,-0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f, 0.5f}}},
            {{-1, 0, 0}, {{-0.5f,-0.5f,-0.5f},{-0.5f,-0.5f, 0.5f},{-0.5f, 0.5f, 0.5f},{-0.5f, 0.5f,-0.5f}}},
        };

        for (auto& f : faces)
        {
            uint32_t base = (uint32_t)mesh.vertices.size();
            for (int i = 0; i < 4; i++)
            {
                mesh_vertex_t v;
                v.pos[0] = f.verts[i].x; v.pos[1] = f.verts[i].y; v.pos[2] = f.verts[i].z;
                v.normal[0] = f.normal.x; v.normal[1] = f.normal.y; v.normal[2] = f.normal.z;
                mesh.vertices.push_back(v);
            }
            mesh.faces.push_back({ base, base + 1, base + 2 });
            mesh.faces.push_back({ base, base + 2, base + 3 });
        }

        return mesh;
    }
}

namespace shaders
{
    struct gpu_mesh_t
    {
        ID3D11Buffer* vb = nullptr;
        uint32_t vertex_count = 0;
    };

    struct chams_settings_t
    {
        float color[3] = { 1, 1, 1 };
        float opacity = 1.0f;
        int shader_type = 2;
        bool filled = true;
        float shininess = 0.5f;
        float glow_intensity = 0.8f;
        float outline_width = 1.0f;
        float time_sec = 0.0f;
    };

    struct shader_vertex_t
    {
        float pos[3];
        float normal[3];
    };

    struct alignas(16) constants_t
    {
        float wvp[16];
        float world[16];
        float cam_alpha[4];
        float tint[4];
        float mode_f[4];
        float params[4];
        float extrusion[4];
    };

    inline ID3D11Device* g_device = nullptr;
    inline ID3D11DeviceContext* g_context = nullptr;
    inline ID3D11VertexShader* g_vs = nullptr;
    inline ID3D11PixelShader* g_ps = nullptr;
    inline ID3D11InputLayout* g_layout = nullptr;
    inline ID3D11Buffer* g_cbuffer = nullptr;
    inline ID3D11BlendState* g_blend = nullptr;
    inline ID3D11RasterizerState* g_raster = nullptr;
    inline ID3D11DepthStencilState* g_depth = nullptr;
    inline bool g_initialized = false;
    inline std::unordered_map<std::string, gpu_mesh_t> g_gpu_cache;

    static const char* vs_hlsl = R"(
cbuffer cb : register(b0)
{
    row_major float4x4 wvp;
    row_major float4x4 world;
    float4 cam_alpha;
    float4 tint;
    float4 mode_f;
    float4 params;
    float4 extrusion;
};

struct vs_in  { float3 pos : POSITION; float3 nrm : NORMAL; };
struct vs_out { float4 pos : SV_POSITION; float3 wpos : TEXCOORD0; float3 wnrm : TEXCOORD1; };

vs_out main(vs_in i)
{
    vs_out o;
    float3 wpos = mul(world, float4(i.pos, 1.0)).xyz;
    o.wnrm = normalize(mul((float3x3)world, i.nrm));
    o.pos = mul(wvp, float4(wpos, 1.0));
    o.wpos = wpos;
    return o;
}
)";

    static const char* ps_hlsl = R"(
cbuffer cb : register(b0)
{
    row_major float4x4 wvp;
    row_major float4x4 world;
    float4 cam_alpha;
    float4 tint;
    float4 mode_f;
    float4 params;
    float4 extrusion;
};

struct ps_in { float4 pos : SV_POSITION; float3 wpos : TEXCOORD0; float3 wnrm : TEXCOORD1; };

float ggx(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (3.14159 * d * d + 0.0001);
}

float3 aces_tonemap(float3 x)
{
    float a = 2.51; float b = 0.03; float c = 2.43; float d = 0.59; float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 main(ps_in i) : SV_TARGET
{
    float3 n = normalize(i.wnrm);
    float3 v = normalize(cam_alpha.xyz - i.wpos);
    float NdotV = max(dot(n, v), 0.001);
    float alpha = cam_alpha.w;
    float3 base = tint.rgb;
    int st = (int)floor(mode_f.x + 0.01);

    float3 keyDir = normalize(float3(0.6, 1.0, 0.8));
    float3 fillDir = normalize(float3(-0.8, 0.3, -0.4));
    float3 H = normalize(keyDir + v);
    float NdotH = max(dot(n, H), 0.0);
    float fresnel = pow(1.0 - NdotV, 4.0);
    float rim = pow(1.0 - NdotV, 2.0);

    float3 fc;
    float a;

    if (st == 0)
    {
        float NdotL1 = max(dot(n, keyDir), 0.0);
        float NdotL2 = max(dot(n, fillDir), 0.0);
        float3 diff = base * (0.22 + NdotL1 * 0.55 + NdotL2 * 0.18);
        float spec = ggx(NdotH, 0.35) * 0.5;
        fc = diff + spec;
        fc *= 1.0 + rim * 0.15;
        a = alpha;
    }
    else if (st == 1)
    {
        float2 uv = i.pos.xy;
        float dx = ddx(uv); float dy = ddy(uv);
        float2 dxy = float2(length(dx), length(dy));
        float pxWidth = max(dxy.x, dxy.y);

        float3 e0 = normalize(ddx(i.wpos));
        float3 e1 = normalize(ddy(i.wpos));
        float3 faceNormal = cross(e0, e1);
        float edgeFactor = 1.0 - saturate(abs(dot(n, faceNormal)) * 4.0);

        float gridScale = 2.0;
        float2 wc = frac(i.wpos.xz * gridScale) - 0.5;
        float2 wb = abs(wc);
        float lineX = smoothstep(0.48, 0.5, wb.x);
        float lineY = smoothstep(0.48, 0.5, wb.y);
        float gridLines = max(lineX, lineY);
        float grid3d = gridLines * 0.4;

        float wire = saturate(edgeFactor * 3.0 + grid3d);
        fc = base * (0.15 + wire * 1.8);
        fc += base * NdotH * ggx(NdotH, 0.1) * wire * 0.6;
        a = saturate(alpha * (0.3 + wire * 0.7));
    }
    else if (st == 2)
    {
        float power = 2.5;
        float intensity = 3.0;
        float rimFresnel = pow(1.0 - NdotV, power);
        float glowFresnel = pow(1.0 - NdotV, 1.5);
        float highlight = pow(NdotH, 64.0) * 2.0;
        float3 glowColor = base * 2.5;
        fc = base * 0.05;
        fc += glowColor * glowFresnel * intensity * 0.5;
        fc += glowColor * rimFresnel * intensity;
        fc += float3(1, 1, 1) * highlight;
        fc += base * fresnel * 0.3;
        a = saturate(alpha * saturate(0.15 + rimFresnel * 0.85 + highlight * 0.3));
    }
    else
    {
        float edge = pow(1.0 - NdotV, 1.8);
        float center = pow(NdotV, 0.6);
        float3 lit = base * (0.4 + max(dot(n, keyDir), 0.0) * 0.6);
        float3 edgeColor = base * 1.8 + float3(0.3, 0.3, 0.3);
        fc = lerp(lit, edgeColor, edge * 0.7);
        fc *= 0.7 + center * 0.3;
        float rimSpec = pow(NdotH, 32.0) * 0.3;
        fc += rimSpec;
        a = saturate(alpha * (0.1 + center * 0.9));
    }

    fc = aces_tonemap(fc);
    return float4(fc, a);
}
)";

    inline sdk::mat4_t mat4_identity()
    {
        sdk::mat4_t r{};
        r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
        return r;
    }

    inline ID3D11Buffer* create_buffer(const void* data, UINT size, UINT bind)
    {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = size;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = bind;

        D3D11_SUBRESOURCE_DATA sd{};
        sd.pSysMem = data;

        ID3D11Buffer* buf = nullptr;
        g_device->CreateBuffer(&bd, &sd, &buf);
        return buf;
    }

    inline gpu_mesh_t& get_or_create(const std::string& id, const shader_vertex_t* verts, uint32_t v_count)
    {
        auto it = g_gpu_cache.find(id);
        if (it != g_gpu_cache.end())
            return it->second;

        gpu_mesh_t gm{};
        gm.vb = create_buffer(verts, v_count * sizeof(shader_vertex_t), D3D11_BIND_VERTEX_BUFFER);
        gm.vertex_count = v_count;

        g_gpu_cache[id] = gm;
        return g_gpu_cache[id];
    }

    inline void fill_constants(
        constants_t& cb,
        const sdk::mat4_t& wvp,
        const sdk::mat4_t& world,
        const sdk::vec3_t& cam,
        const chams_settings_t& settings,
        float vs_pass,
        float ps_pass)
    {
        std::memcpy(cb.wvp, &wvp, 64);
        std::memcpy(cb.world, &world, 64);
        cb.cam_alpha[0] = cam.x;
        cb.cam_alpha[1] = cam.y;
        cb.cam_alpha[2] = cam.z;
        cb.cam_alpha[3] = settings.opacity;
        cb.tint[0] = settings.color[0];
        cb.tint[1] = settings.color[1];
        cb.tint[2] = settings.color[2];
        cb.tint[3] = 1.0f;
        cb.mode_f[0] = static_cast<float>(settings.shader_type);
        cb.mode_f[1] = vs_pass;
        cb.mode_f[2] = ps_pass;
        cb.mode_f[3] = settings.filled ? 1.0f : 0.0f;
        cb.params[0] = settings.shininess;
        cb.params[1] = settings.glow_intensity;
        cb.params[2] = settings.outline_width;
        cb.params[3] = settings.time_sec;
        cb.extrusion[0] = std::max(0.002f, settings.outline_width * 0.035f);
        cb.extrusion[1] = cb.extrusion[2] = cb.extrusion[3] = 0.0f;
    }

    inline void draw_mesh_internal(const gpu_mesh_t& gm, const constants_t& cb)
    {
        g_context->UpdateSubresource(g_cbuffer, 0, nullptr, &cb, 0, 0);

        UINT stride = sizeof(shader_vertex_t);
        UINT offset = 0;
        g_context->IASetVertexBuffers(0, 1, &gm.vb, &stride, &offset);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_context->IASetInputLayout(g_layout);
        g_context->VSSetShader(g_vs, nullptr, 0);
        g_context->VSSetConstantBuffers(0, 1, &g_cbuffer);
        g_context->PSSetShader(g_ps, nullptr, 0);
        g_context->PSSetConstantBuffers(0, 1, &g_cbuffer);
        g_context->OMSetBlendState(g_blend, nullptr, 0xFFFFFFFF);
        g_context->OMSetDepthStencilState(g_depth, 0);
        g_context->RSSetState(g_raster);
        g_context->Draw(gm.vertex_count, 0);
    }

    inline void render_mesh(
        const std::string& mesh_id,
        const sdk::mesh_data_t& mesh,
        const sdk::mat3_t& rotation,
        const sdk::vec3_t& position,
        const sdk::vec3_t& part_size,
        const sdk::mat4_t& view_proj,
        const sdk::vec3_t& cam_pos,
        const chams_settings_t& settings)
    {
        if (!g_initialized)
            return;

        std::vector<shader_vertex_t> verts;
        verts.reserve(mesh.faces.size() * 3);

        for (size_t i = 0; i < mesh.faces.size(); i++)
        {
            uint32_t i0 = mesh.faces[i].indices[0];
            uint32_t i1 = mesh.faces[i].indices[1];
            uint32_t i2 = mesh.faces[i].indices[2];
            if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size())
                continue;

            const uint32_t ix[3] = { i0, i1, i2 };
            for (int j = 0; j < 3; j++)
            {
                shader_vertex_t sv{};
                std::memcpy(sv.pos, mesh.vertices[ix[j]].pos, 12);
                std::memcpy(sv.normal, mesh.vertices[ix[j]].normal, 12);
                verts.push_back(sv);
            }
        }

        if (verts.empty())
            return;

        uint32_t v_count = static_cast<uint32_t>(verts.size());

        float mesh_sx = mesh.bounds_max.x - mesh.bounds_min.x;
        float mesh_sy = mesh.bounds_max.y - mesh.bounds_min.y;
        float mesh_sz = mesh.bounds_max.z - mesh.bounds_min.z;

        sdk::vec3_t scale = { 1.0f, 1.0f, 1.0f };
        if (mesh_sx > 0.001f && mesh_sy > 0.001f && mesh_sz > 0.001f)
        {
            scale.x = part_size.x / mesh_sx;
            scale.y = part_size.y / mesh_sy;
            scale.z = part_size.z / mesh_sz;
            scale.x = std::clamp(scale.x, 0.001f, 100.0f);
            scale.y = std::clamp(scale.y, 0.001f, 100.0f);
            scale.z = std::clamp(scale.z, 0.001f, 100.0f);
        }

        sdk::vec3_t adj_pos = position;
        if (mesh_sx > 0.001f && mesh_sy > 0.001f && mesh_sz > 0.001f)
        {
            sdk::vec3_t c{
                (mesh.bounds_min.x + mesh.bounds_max.x) * 0.5f,
                (mesh.bounds_min.y + mesh.bounds_max.y) * 0.5f,
                (mesh.bounds_min.z + mesh.bounds_max.z) * 0.5f
            };
            sdk::vec3_t scaled_c{ c.x * scale.x, c.y * scale.y, c.z * scale.z };
            sdk::vec3_t Mc{
                rotation.m[0][0] * scaled_c.x + rotation.m[0][1] * scaled_c.y + rotation.m[0][2] * scaled_c.z,
                rotation.m[1][0] * scaled_c.x + rotation.m[1][1] * scaled_c.y + rotation.m[1][2] * scaled_c.z,
                rotation.m[2][0] * scaled_c.x + rotation.m[2][1] * scaled_c.y + rotation.m[2][2] * scaled_c.z
            };
            adj_pos.x = position.x - Mc.x;
            adj_pos.y = position.y - Mc.y;
            adj_pos.z = position.z - Mc.z;
        }

        std::string canon = sdk::extract_asset_id(mesh_id);
        if (canon.empty())
            canon = mesh_id;
        std::string gpu_key = "v2_" + canon + "_" + std::to_string(mesh.vertices.size()) + "_" + std::to_string(mesh.faces.size());

        auto& gm = get_or_create(gpu_key, verts.data(), v_count);

        sdk::mat4_t world{};
        world.m[0][0] = rotation.m[0][0] * scale.x; world.m[0][1] = rotation.m[0][1] * scale.y; world.m[0][2] = rotation.m[0][2] * scale.z; world.m[0][3] = adj_pos.x;
        world.m[1][0] = rotation.m[1][0] * scale.x; world.m[1][1] = rotation.m[1][1] * scale.y; world.m[1][2] = rotation.m[1][2] * scale.z; world.m[1][3] = adj_pos.y;
        world.m[2][0] = rotation.m[2][0] * scale.x; world.m[2][1] = rotation.m[2][1] * scale.y; world.m[2][2] = rotation.m[2][2] * scale.z; world.m[2][3] = adj_pos.z;
        world.m[3][0] = 0; world.m[3][1] = 0; world.m[3][2] = 0; world.m[3][3] = 1;

        constants_t cb{};
        fill_constants(cb, view_proj, world, cam_pos, settings, 0.0f, 0.0f);
        draw_mesh_internal(gm, cb);
    }

    inline bool init(ID3D11Device* device, ID3D11DeviceContext* context)
    {
        g_device = device;
        g_context = context;
        g_gpu_cache.clear();

        ID3DBlob* vs_blob = nullptr;
        ID3DBlob* ps_blob = nullptr;
        ID3DBlob* err = nullptr;

        if (FAILED(D3DCompile(vs_hlsl, strlen(vs_hlsl), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vs_blob, &err)))
        {
            if (err) err->Release();
            return false;
        }

        if (FAILED(D3DCompile(ps_hlsl, strlen(ps_hlsl), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &ps_blob, &err)))
        {
            if (err) err->Release();
            vs_blob->Release();
            return false;
        }

        device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &g_vs);
        device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &g_ps);

        D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        device->CreateInputLayout(layout, 2, vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), &g_layout);

        vs_blob->Release();
        ps_blob->Release();

        D3D11_BUFFER_DESC cbd{};
        cbd.ByteWidth = sizeof(constants_t);
        cbd.Usage = D3D11_USAGE_DEFAULT;
        cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        device->CreateBuffer(&cbd, nullptr, &g_cbuffer);

        D3D11_BLEND_DESC bd{};
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        device->CreateBlendState(&bd, &g_blend);

        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.AntialiasedLineEnable = TRUE;
        device->CreateRasterizerState(&rd, &g_raster);

        D3D11_DEPTH_STENCIL_DESC dd{};
        dd.DepthEnable = FALSE;
        device->CreateDepthStencilState(&dd, &g_depth);

        g_initialized = true;
        return true;
    }

    inline void shutdown()
    {
        for (auto& [id, gm] : g_gpu_cache)
        {
            if (gm.vb) gm.vb->Release();
        }
        g_gpu_cache.clear();

        if (g_vs) { g_vs->Release(); g_vs = nullptr; }
        if (g_ps) { g_ps->Release(); g_ps = nullptr; }
        if (g_layout) { g_layout->Release(); g_layout = nullptr; }
        if (g_cbuffer) { g_cbuffer->Release(); g_cbuffer = nullptr; }
        if (g_blend) { g_blend->Release(); g_blend = nullptr; }
        if (g_raster) { g_raster->Release(); g_raster = nullptr; }
        if (g_depth) { g_depth->Release(); g_depth = nullptr; }
    }
}
