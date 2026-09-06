static const char s_VS[] = R"(
cbuffer CB : register(b0) {
    float4x4 g_MVP;
    float4x4 g_World;
    float4   g_Opacity;
};
struct VS_In {
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    float2 uv  : TEXCOORD;
};
struct VS_Out {
    float4 pos : SV_POSITION;
    float3 nrm : NORMAL;
    float2 uv  : TEXCOORD;
};
VS_Out main(VS_In v) {
    VS_Out o;
    o.pos = mul(float4(v.pos, 1.0), g_MVP);
    o.nrm = mul(v.nrm, (float3x3)g_World);
    o.uv  = v.uv;
    return o;
}
)";
static const char s_PS[] = R"(
cbuffer CB : register(b0) {
    float4x4 g_MVP;
    float4x4 g_World;
    float4   g_Opacity;
};
Texture2D    g_Tex  : register(t0);
SamplerState g_Sam  : register(s0);
struct VS_Out {
    float4 pos : SV_POSITION;
    float3 nrm : NORMAL;
    float2 uv  : TEXCOORD;
};
float4 main(VS_Out v) : SV_Target {
    float4 tex = g_Tex.Sample(g_Sam, v.uv);
    float3 n = normalize(v.nrm);
    float3 L = normalize(float3(0.45, 0.85, -0.35));
    float3 H = normalize(L + float3(0.0, 0.15, -1.0));
    float ndotl = saturate(dot(n, L));
    float spec = pow(saturate(dot(n, H)), 32.0) * 0.18;
    float wrap = saturate(ndotl * 0.65 + 0.35);
    float3 lit = tex.rgb * (0.28 + wrap * 0.82) + spec;
    lit = saturate((lit - 0.5) * 1.08 + 0.5);
    return float4(lit, saturate(g_Opacity.x));
}
)";

static bool CompileShader(const char* src, const char* entry, const char* profile, ID3DBlob** out)
{
    ID3DBlob* err = nullptr;
    HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr,
                            entry, profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &err);
    if (err) err->Release(); // ошибки нам пофиг, SUCCEEDED хватит
    return SUCCEEDED(hr);
}

