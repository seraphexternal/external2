static bool ProjectMVP(const float* M, float x, float y, float z, float& u, float& v)
{
    const float clip_x = x * M[0] + y * M[4] + z * M[8]  + M[12];
    const float clip_y = x * M[1] + y * M[5] + z * M[9]  + M[13];
    const float clip_w = x * M[3] + y * M[7] + z * M[11] + M[15];
    if (clip_w <= 1e-5f) return false;
    const float inv = 1.0f / clip_w;
    u = clip_x * inv * 0.5f + 0.5f;
    v = -clip_y * inv * 0.5f + 0.5f;
    return true;
}

bool PreviewRenderer::GetProjectedUVBounds(float& u0, float& v0, float& u1, float& v1) const
{
    if (!m_LastMVPValid) return false;
    if (m_CachedBoundsValid) {
        u0 = m_CachedU0; v0 = m_CachedV0; u1 = m_CachedU1; v1 = m_CachedV1;
        return true;
    }

    float minU = 1e9f, minV = 1e9f, maxU = -1e9f, maxV = -1e9f;
    bool any = false;
    const float* M = m_LastMVP;

    auto expand = [&](float x, float y, float z) {
        float u, v;
        if (!ProjectMVP(M, x, y, z, u, v)) return;
        minU = (std::min)(minU, u); maxU = (std::max)(maxU, u);
        minV = (std::min)(minV, v); maxV = (std::max)(maxV, v);
        any = true;
    };

    for (const auto& p : m_BodyParts) {
        if (!p.valid) continue;
        for (int i = 0; i < 8; ++i) {
            expand((i & 1) ? p.max[0] : p.min[0],
                   (i & 2) ? p.max[1] : p.min[1],
                   (i & 4) ? p.max[2] : p.min[2]);
        }
    }

    const std::vector<float>& pos =
        !m_BoxPos.empty() ? m_BoxPos :
        (!m_BodyPos.empty() ? m_BodyPos : m_UniquePos);
    for (size_t i = 0; i + 2 < pos.size(); i += 3)
        expand(pos[i], pos[i + 1], pos[i + 2]);

    if (!any) return false;

    m_CachedU0 = u0 = minU; m_CachedV0 = v0 = minV;
    m_CachedU1 = u1 = maxU; m_CachedV1 = v1 = maxV;
    m_CachedBoundsValid = true;
    return true;
}

bool PreviewRenderer::GetProjectedPartBoxes(
    std::vector<std::array<std::pair<float, float>, 8>>& out) const
{
    out.clear();
    if (!m_LastMVPValid || m_BodyParts.empty()) return false;

    const float* M = m_LastMVP;
    out.reserve(m_BodyParts.size());
    for (const auto& p : m_BodyParts) {
        if (!p.valid) continue;
        std::array<std::pair<float, float>, 8> box{};
        bool ok = true;
        for (int i = 0; i < 8; ++i) {

            const float x = (i & 4) ? p.max[0] : p.min[0];
            const float y = (i & 2) ? p.max[1] : p.min[1];
            const float z = (i & 1) ? p.max[2] : p.min[2];
            float u, v;
            if (!ProjectMVP(M, x, y, z, u, v)) { ok = false; break; }
            box[i] = { u, v };
        }
        if (ok) out.push_back(box);
    }
    return !out.empty();
}

bool PreviewRenderer::GetProjectedAimPartBoxes(
    std::vector<std::pair<int, std::array<std::pair<float, float>, 8>>>& out) const
{
    out.clear();
    if (!m_LastMVPValid) return false;

    const float* M = m_LastMVP;
    out.reserve(AimPartCount);
    for (int pi = 0; pi < AimPartCount; ++pi) {
        const AimPartAABB& p = m_AimParts[pi];
        if (!p.valid) continue;
        std::array<std::pair<float, float>, 8> box{};
        bool ok = true;
        for (int i = 0; i < 8; ++i) {
            const float x = (i & 4) ? p.max[0] : p.min[0];
            const float y = (i & 2) ? p.max[1] : p.min[1];
            const float z = (i & 1) ? p.max[2] : p.min[2];
            float u, v;
            if (!ProjectMVP(M, x, y, z, u, v)) { ok = false; break; }
            box[i] = { u, v };
        }
        if (ok) out.emplace_back(pi, box);
    }
    return !out.empty();
}

bool PreviewRenderer::GetProjectedAimPartCenters(
    std::vector<std::pair<int, std::pair<float, float>>>& out) const
{
    out.clear();
    if (!m_LastMVPValid) return false;

    const float* M = m_LastMVP;
    out.reserve(AimPartCount);
    for (int pi = 0; pi < AimPartCount; ++pi) {
        const AimPartAABB& p = m_AimParts[pi];
        if (!p.valid) continue;
        const float x = (p.min[0] + p.max[0]) * 0.5f;
        const float y = (p.min[1] + p.max[1]) * 0.5f;
        const float z = (p.min[2] + p.max[2]) * 0.5f;
        float u, v;
        if (!ProjectMVP(M, x, y, z, u, v)) continue;
        out.emplace_back(pi, std::make_pair(u, v));
    }
    return !out.empty();
}

void PreviewRenderer::SetSkeletonHead(float x, float y, float z)
{
    m_SkelHead[0] = x; m_SkelHead[1] = y; m_SkelHead[2] = z;
    m_SkelHeadValid = true;
}

bool PreviewRenderer::GetProjectedHead(float& u, float& v) const
{
    if (!m_LastMVPValid || !m_SkelHeadValid) return false;
    return ProjectMVP(m_LastMVP, m_SkelHead[0], m_SkelHead[1], m_SkelHead[2], u, v);
}

bool PreviewRenderer::GetProjectedR6Skeleton(std::vector<float>& out_uv_segs) const
{
    out_uv_segs.clear();
    if (!m_LastMVPValid || m_SkelSegCount <= 0) return false;
    out_uv_segs.reserve((size_t)m_SkelSegCount * 4);
    for (int i = 0; i < m_SkelSegCount; ++i) {
        float u0, v0, u1, v1;
        if (!ProjectMVP(m_LastMVP,
                m_SkelSeg[i][0][0], m_SkelSeg[i][0][1], m_SkelSeg[i][0][2], u0, v0))
            continue;
        if (!ProjectMVP(m_LastMVP,
                m_SkelSeg[i][1][0], m_SkelSeg[i][1][1], m_SkelSeg[i][1][2], u1, v1))
            continue;
        out_uv_segs.push_back(u0);
        out_uv_segs.push_back(v0);
        out_uv_segs.push_back(u1);
        out_uv_segs.push_back(v1);
    }
    return out_uv_segs.size() >= 4;
}

bool PreviewRenderer::GetProjectedModelBox(std::vector<float>& out_uv_corners) const
{
    out_uv_corners.clear();
    if (!m_LastMVPValid) return false;
    const float* M = m_LastMVP;
    // corner bit pattern: x=bit2, y=bit1, z=bit0
    static const int idx[8][3] = {
        {0,0,0},{1,0,0},{0,1,0},{1,1,0},{0,0,1},{1,0,1},{0,1,1},{1,1,1}
    };
    float corners[8][2];
    for (int i = 0; i < 8; ++i)
    {
        const float x = idx[i][0] ? m_BoxMax[0] : m_BoxMin[0];
        const float y = idx[i][1] ? m_BoxMax[1] : m_BoxMin[1];
        const float z = idx[i][2] ? m_BoxMax[2] : m_BoxMin[2];
        float u, v;
        if (!ProjectMVP(M, x, y, z, u, v)) return false;
        corners[i][0] = u; corners[i][1] = v;
    }
    out_uv_corners.reserve(16);
    for (int i = 0; i < 8; ++i)
    {
        out_uv_corners.push_back(corners[i][0]);
        out_uv_corners.push_back(corners[i][1]);
    }
    return true;
}

void* PreviewRenderer::GetTextureID() const { return (void*)m_SRV; }

void PreviewRenderer::Shutdown()
{
    auto rel = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };
    rel(m_VB); rel(m_CB); rel(m_IL); rel(m_VS); rel(m_PS);
    rel(m_TexSRV); rel(m_TexRes); rel(m_Sampler);
    for (int i = 0; i < MaxSubMeshes; ++i) rel(m_SubMeshes[i].srv);
    m_SubMeshCount = 0;
    rel(m_RS); rel(m_DSState); rel(m_DSNoWrite);
    rel(m_BlendOpaque); rel(m_BlendAlpha);
    rel(m_SRV); rel(m_RTV); rel(m_RTTex); rel(m_ResolveTex); rel(m_DSV); rel(m_DepthTex);
    m_BodyVertCount = 0;
    m_WingVertCount = 0;
    m_WingAnimTime = 0.0f;
    m_Ready = false;
    m_LastMVPValid = false;
}

