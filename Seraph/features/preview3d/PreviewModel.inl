namespace {
    bool UploadVerts(ID3D11Device* device, std::vector<ModelVertex>& verts,
                     ID3D11Buffer** ppVB, unsigned int& vertCount)
    {
        if (verts.empty()) return false;
        if (*ppVB) { (*ppVB)->Release(); *ppVB = nullptr; }
        vertCount = (unsigned int)verts.size();
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = (UINT)(sizeof(ModelVertex) * verts.size());
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA sd{};
        sd.pSysMem = verts.data();
        return SUCCEEDED(device->CreateBuffer(&bd, &sd, ppVB));
    }

    bool UploadPng(ID3D11Device* device, const stbi_uc* data, int w, int h,
                   ID3D11Texture2D** ppTex, ID3D11ShaderResourceView** ppSRV,
                   ID3D11SamplerState** ppSamp)
    {
        if (*ppSRV) { (*ppSRV)->Release(); *ppSRV = nullptr; }
        if (*ppTex) { (*ppTex)->Release(); *ppTex = nullptr; }
        if (*ppSamp) { (*ppSamp)->Release(); *ppSamp = nullptr; }

        D3D11_TEXTURE2D_DESC td{};
        td.Width = (UINT)w; td.Height = (UINT)h; td.MipLevels = 0; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        if (FAILED(device->CreateTexture2D(&td, nullptr, ppTex))) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
        srvd.Format = td.Format;
        srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvd.Texture2D.MipLevels = (UINT)-1;
        if (FAILED(device->CreateShaderResourceView(*ppTex, &srvd, ppSRV))) return false;

        ID3D11DeviceContext* ctx = nullptr;
        device->GetImmediateContext(&ctx);
        if (ctx) {
            ctx->UpdateSubresource(*ppTex, 0, nullptr, data, (UINT)(w * 4), 0);
            ctx->GenerateMips(*ppSRV);
            ctx->Release();
        }

        D3D11_SAMPLER_DESC smd{};
        smd.Filter = D3D11_FILTER_ANISOTROPIC;
        smd.MaxAnisotropy = 8;
        smd.AddressU = smd.AddressV = smd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        smd.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        smd.MinLOD = 0.0f;
        smd.MaxLOD = D3D11_FLOAT32_MAX;
        return SUCCEEDED(device->CreateSamplerState(&smd, ppSamp));
    }

}

void PreviewRenderer::BuildR6SkeletonFromParts()
{
    m_SkelSegCount = 0;

    // Detect the model's true up axis from body extents. Supports both Y-up
    // humanoid OBJ exports and Z-up Sketchfab GLBs (Mario/Roblox), so part
    // classification below is up-axis agnostic.
    float ext[3];
    for (int k = 0; k < 3; ++k) ext[k] = m_BodyMax[k] - m_BodyMin[k];
    int up;
    if (m_SkelUpAxis == -1) {
        up = 0;
        for (int k = 1; k < 3; ++k) if (ext[k] > ext[up]) up = k;
    } else {
        up = m_SkelUpAxis >= 0 ? m_SkelUpAxis : -m_SkelUpAxis;
        if (up < 0 || up > 2) { up = 0; }
    }
    const int a = (up + 1) % 3;
    const int b = (up + 2) % 3;
    const int side = (ext[a] >= ext[b]) ? a : b;
    const int depth = 3 - up - side;

    const float upMin = m_BodyMin[up];
    const float H  = (std::max)(0.01f, ext[up]);
    const float Ws = (std::max)(0.01f, ext[side]);
    const float cx = (m_BodyMin[side] + m_BodyMax[side]) * 0.5f;
    const float cd = (m_BodyMin[depth] + m_BodyMax[depth]) * 0.5f;

    // Build a model-space point in up/side/depth coordinates.
    auto P = [&](float upVal, float sd, float dp) -> std::array<float, 3> {
        std::array<float, 3> p{ cx, cd, 0.0f };
        p[up]    = upVal;
        p[side]  += sd;
        p[depth] += dp;
        return p;
    };

    // эвристика по aabb, голова/руки/ноги (в осях up/side)
    const ModelPartAABB* torso = nullptr;
    const ModelPartAABB* lArm = nullptr;
    const ModelPartAABB* rArm = nullptr;
    const ModelPartAABB* lLeg = nullptr;
    const ModelPartAABB* rLeg = nullptr;

    for (const auto& p : m_BodyParts) {
        if (!p.valid) continue;
        const float pu  = (p.min[up] + p.max[up]) * 0.5f;
        const float psz = p.max[up] - p.min[up];
        const float pc  = (p.min[side] + p.max[side]) * 0.5f;

        if (pu > upMin + H * 0.84f && psz < H * 0.42f)
            continue; // high small part (head/cap) -> skip, not a limb
        if (pu < upMin + H * 0.42f) {
            if (pc < cx) lLeg = &p; else rLeg = &p;
            continue;
        }
        if (std::fabs(pc - cx) > Ws * 0.26f && psz < H * 0.60f) {
            if (pc < cx) lArm = &p; else rArm = &p;
            continue;
        }
        if (std::fabs(pc - cx) <= Ws * 0.38f && psz >= H * 0.22f)
            torso = &p;
    }

    // A usable humanoid needs a torso AND both arms AND both legs. For many GLB
    // exports (Mario/TungTung) every mesh AABB overlaps the full body and is
    // X-centred, so the classifier only ever matches the "torso" test and the
    // limb pointers stay null. Drawing without them would collapse the skeleton
    // into a single bare spine line running through the character. Whenever any
    // limb is missing we fall back to the proportional full-limb humanoid, which
    // always stamps head/spine/two-arms/two-legs inside the real body bounds.
    if (!torso || !lArm || !rArm || !lLeg || !rLeg) {
        BuildGenericSkeletonFromBounds();
        return;
    }

    auto addSeg = [&](const std::array<float, 3>& p0, const std::array<float, 3>& p1) {
        if (m_SkelSegCount >= MaxSkelSegs) return;
        for (int k = 0; k < 3; ++k) {
            m_SkelSeg[m_SkelSegCount][0][k] = p0[k];
            m_SkelSeg[m_SkelSegCount][1][k] = p1[k];
        }
        ++m_SkelSegCount;
    };

    // Torso / shoulder / hip along the up axis.
    float torsoTopU = upMin + H * 0.86f;
    float torsoBotU = upMin + H * 0.48f;
    if (torso) {
        torsoTopU = torso->max[up];
        torsoBotU = torso->min[up];
    }
    const auto shoulder = P((std::min)(torsoTopU, upMin + H * 0.92f), 0.0f, 0.0f);
    const auto hip      = P(torsoBotU, 0.0f, 0.0f);
    const auto neck     = P((std::max)((shoulder[up] + upMin + H * 0.92f) * 0.5f, shoulder[up] + H * 0.05f), 0.0f, 0.0f);
    const auto head     = P(upMin + H * 0.97f, 0.0f, 0.0f);

    // Record the head point in model space so external markers (ESP Head Circle)
    // can anchor onto the actual head rather than a real Roblox part.
    m_SkelHead[0] = head[0]; m_SkelHead[1] = head[1]; m_SkelHead[2] = head[2];
    m_SkelHeadValid = true;

    addSeg(head, neck);            // head -> neck
    addSeg(neck, shoulder);        // neck -> shoulder
    addSeg(shoulder, hip);         // spine

    auto armSegs = [&](const ModelPartAABB* arm, float sideSign) {
        if (!arm) return;
        const float am = (arm->min[side] + arm->max[side]) * 0.5f;
        const float ap = (arm->min[depth] + arm->max[depth]) * 0.5f;
        const auto joint = P(shoulder[up], am - cx + sideSign * Ws * 0.06f, ap - cd);
        const auto hand  = P(arm->min[up], am - cx, ap - cd);
        addSeg(neck, joint);
        addSeg(joint, hand);
    };
    armSegs(lArm, -1.0f);
    armSegs(rArm, +1.0f);

    auto legSegs = [&](const ModelPartAABB* leg) {
        if (!leg) return;
        const float lm = (leg->min[side] + leg->max[side]) * 0.5f;
        const float lp = (leg->min[depth] + leg->max[depth]) * 0.5f;
        const auto top = P(hip[up], lm - cx, lp - cd);
        const auto bot = P(leg->min[up], lm - cx, lp - cd);
        addSeg(hip, top);
        addSeg(top, bot);
    };
    legSegs(lLeg);
    legSegs(rLeg);
}

// Generic silhouette skeleton for models whose submeshes overlap the whole
// body (either Z-up Sketchfab GLBs like Mario/TungTung, or OBJ exports) where
// the humanoid part heuristic can't isolate a recognisable torso/limb layout.
// The figure is stamped inside the real bounding box along the model's true
// "up" axis with proportions tuned per model so the bones roughly fit each
// character rather than collapsing to a bare line or overflowing it. Bones are
// in model-space and projected through the same MVP as the geometry.
void PreviewRenderer::BuildGenericSkeletonFromBounds()
{
    m_SkelSegCount = 0;

    float ext[3];
    for (int k = 0; k < 3; ++k)
        ext[k] = m_BodyMax[k] - m_BodyMin[k];

    // Resolve the signed up axis. A negative value means the model's HEAD is at
    // the MINIMUM end of that axis. Magnitude selects the axis (0=X,1=Y,2=Z).
    int up;
    bool flip = false;
    if (m_SkelUpAxis == -1) {
        up = 0;
        for (int k = 1; k < 3; ++k) if (ext[k] > ext[up]) up = k;
    } else {
        up = m_SkelUpAxis >= 0 ? m_SkelUpAxis : -m_SkelUpAxis;
        flip = m_SkelUpAxis < 0;
        if (up < 0 || up > 2) { up = 0; flip = false; }
    }
    const int a = (up + 1) % 3;
    const int b = (up + 2) % 3;
    int side = (ext[a] >= ext[b]) ? a : b;
    const int depth = 3 - up - side;

    const float H = (std::max)(0.01f, ext[up]);
    const float Wc = (std::max)(0.01f, ext[side]);
    const float upBase = flip ? m_BodyMax[up] : m_BodyMin[up];
    const float upStep = flip ? -H : H;

    auto centerAxis = [&](int k) { return (m_BodyMin[k] + m_BodyMax[k]) * 0.5f; };

    // Record the character's head-pivot point in model space. The head is NOT at
    // the very top boundary of the box - it is inset (a humanoid head sits just
    // below the body box top). We anchor at the head's centre (~92% up the body)
    // so overlays like the ESP Head Circle surround the actual head instead of
    // floating above it. Stored at the body-centre on the lateral axes.
    m_SkelHead[up] = upBase + upStep * 0.92f;
    m_SkelHead[(up + 1) % 3] = centerAxis((up + 1) % 3);
    m_SkelHead[(up + 2) % 3] = centerAxis((up + 2) % 3);
    m_SkelHeadValid = true;

    // Point at fraction along up (0=bottom end,1=top/head end), lateral spread.
    auto pt = [&](float upFrac, float spread) -> std::array<float, 3> {
        std::array<float, 3> p{};
        p[up] = upBase + upStep * upFrac;
        p[side] = centerAxis(side) + Wc * 0.5f * spread;
        p[depth] = centerAxis(depth);
        return p;
    };
    auto add = [&](const std::array<float, 3>& ap, const std::array<float, 3>& bp) {
        if (m_SkelSegCount >= MaxSkelSegs) return;
        for (int k = 0; k < 3; ++k) {
            m_SkelSeg[m_SkelSegCount][0][k] = ap[k];
            m_SkelSeg[m_SkelSegCount][1][k] = bp[k];
        }
        ++m_SkelSegCount;
    };

    // Which character are we fitting? Distinguish by the signed up axis set for
    // each model: Mario uses +2 (head at +Z), TungTung uses -2 (head at -Z).
    const bool isTungTung = (up == 2 && m_SkelUpAxis == -2);
    const bool isMario    = (up == 2 && m_SkelUpAxis == 2);

        // ── Mario: proper humanoid made to fit its real proportions ──
    // Anatomy measured from geometry (0=feet, 1=head): legs span 0-0.5,
    // arms/shoulders spread very wide at ~0.5-0.6, torso 0.65-0.9, head 0.93-1.0.
    if (isMario)
    {
        // Proportions as fractions of height (1 = head). Spread is fraction of
        // half the breadth so the figure sits snugly inside the silhouette.
        const float headTop  = 0.96f;
        const float neck     = 0.88f;
        const float shoulder = 0.62f;
        const float torsoBot = 0.54f;
        const float hip      = 0.52f;
        const float lElbow   = 0.57f; // -0.70
        const float rElbow   = 0.57f; // +0.70
        const float lHand    = 0.55f; // -0.92
        const float rHand    = 0.55f; // +0.92
        const float lKnee    = 0.25f; // -0.28
        const float rKnee    = 0.25f; // +0.28
        const float lFoot    = 0.05f; // -0.30
        const float rFoot    = 0.05f; // +0.30

        const auto headP    = pt(headTop, 0.0f);
        const auto neckP    = pt(neck, 0.0f);
        const auto shouldP  = pt(shoulder, 0.0f);
        const auto hipP     = pt(hip, 0.0f);
        const auto torsoP   = pt(torsoBot, 0.0f);
        const auto lEP      = pt(lElbow, -0.70f);
        const auto rEP      = pt(rElbow,  0.70f);
        const auto lHP      = pt(lHand, -0.92f);
        const auto rHP      = pt(rHand,  0.92f);
        const auto lKP      = pt(lKnee, -0.28f);
        const auto rKP      = pt(rKnee,  0.28f);
        const auto lFP      = pt(lFoot, -0.30f);
        const auto rFP      = pt(rFoot,  0.30f);

        add(headP, neckP);               // head -> neck
        add(neckP, shouldP);             // neck -> shoulders
        add(shouldP, hipP);              // spine
        add(shouldP, torsoP);            // spine to bottom of torso
        add(neckP, lEP); add(lEP, lHP);  // L arm
        add(neckP, rEP); add(rEP, rHP);  // R arm
        add(hipP, lKP);  add(lKP, lFP);  // L leg
        add(hipP, rKP);  add(rKP, rFP);  // R leg
        return;
    }

    // ── TungTung: compact, predominantly non-humanoid mascot ──
    // Its body is a narrow tall column (X=0.41, Y=0.55, Z=1.12). A wide-armed
    // humanoid can never "fit" such a body - limbs extruding along the lateral
    // axis stick out sideways past the thin silhouette. Instead we stamp a
    // compact, tightly centered figure whose limb reach is clamped to the
    // model's narrowest width so nothing ever pokes outside the outline.
    if (isTungTung)
    {
        // TungTung's true left-right axis is the NARROWEST horizontal one.
        // The generic pick (longer horizontal extent) selects the other axis,
        // which lands along the model's facing instead of its sides - the
        // skeleton would look rotated 90 degrees horizontally around the body.
        const int bside  = depth;       // swap side <-> depth
        const int bdepth = side;

        // Narrowest horizontal extent = the safe lateral clamp for THIS body.
        const float narrow = (std::min)(ext[(up + 1) % 3], ext[(up + 2) % 3]);
        const float halfW  = (std::max)(0.01f, narrow) * 0.5f;
        // Center the figure on the side AND depth axes. Limbs spread along the
        // bside axis only (left/right) so the figure stands upright facing
        // forward, never pushing diagonal limbs "in front of" the character.
        const float cs = centerAxis(bside);
        // Seat the figure back onto the model body: the raw +Y offset pushes it
        // toward the viewer, so pull the depth centre the opposite way.
        const float cd = centerAxis(bdepth) - ext[bdepth] * 0.25f;
        // Anchor the ESP head circle to this model's actual skeleton head, which
        // uses the shifted depth centre above (otherwise the circle floats in
        // front of the skeleton). headTip fraction mirrors headT below.
        m_SkelHead[up]     = upBase + upStep * 0.93f;
        m_SkelHead[bside]  = cs;
        m_SkelHead[bdepth] = cd;
        m_SkelHeadValid = true;
        auto ptT = [&](float upFrac, float lat) -> std::array<float, 3> {
            std::array<float, 3> p{};
            p[up]     = upBase + upStep * upFrac;
            p[bside]  = cs + halfW * lat;
            p[bdepth] = cd; // centred, no diagonal push
            return p;
        };
        auto add = [&](const std::array<float, 3>& ap, const std::array<float, 3>& bp) {
            if (m_SkelSegCount >= MaxSkelSegs) return;
            for (int k = 0; k < 3; ++k) {
                m_SkelSeg[m_SkelSegCount][0][k] = ap[k];
                m_SkelSeg[m_SkelSegCount][1][k] = bp[k];
            }
            ++m_SkelSegCount;
        };

        const float headT  = 0.93f;
        const float neck   = 0.78f;
        const float torso  = 0.52f;
        const float hips   = 0.40f;
        const float lEP    = 0.62f; // lat -0.60
        const float rEP    = 0.62f; // +0.60
        const float lHP    = 0.46f; // -0.55
        const float rHP    = 0.46f; // +0.55
        const float lKP    = 0.20f; // -0.38
        const float rKP    = 0.20f; // +0.38
        const float lFP    = 0.04f; // -0.40
        const float rFP    = 0.04f; // +0.40

        const auto headP  = ptT(headT, 0.0f);
        const auto neckP  = ptT(neck, 0.0f);
        const auto torsoP = ptT(torso, 0.0f);
        const auto hipsP  = ptT(hips, 0.0f);
        const auto lEPp   = ptT(lEP, -0.55f);
        const auto rEPp   = ptT(rEP,  0.55f);
        const auto lHPp   = ptT(lHP, -0.45f);
        const auto rHPp   = ptT(rHP,  0.45f);
        const auto lKPp   = ptT(lKP, -0.42f);
        const auto rKPp   = ptT(rKP,  0.42f);
        const auto lFPp   = ptT(lFP, -0.42f);
        const auto rFPp   = ptT(rFP,  0.42f);

        add(headP, neckP);                // head -> neck
        add(neckP, torsoP);               // upper body
        add(torsoP, hipsP);               // lower body
        add(neckP, lEPp); add(lEPp, lHPp);// L arm
        add(neckP, rEPp); add(rEPp, rHPp);// R arm
        add(hipsP, lKPp); add(lKPp, lFPp);// L leg
        add(hipsP, rKPp); add(rKPp, rFPp);// R leg
        return;
    }

    // ── Default (Y-up OBJ / Roblox): standard humanoid proportions ──
    const float headTop   = 0.96f;
    const float neck      = 0.86f;
    const float spineBot  = 0.46f;
    const float lElbow    = 0.70f;  // -0.50
    const float rElbow    = 0.70f;  // +0.50
    const float lHand     = 0.54f;  // -0.44
    const float rHand     = 0.54f;  // +0.44
    const float lKnee     = 0.28f;  // -0.32
    const float rKnee     = 0.28f;  // +0.32
    const float lFoot     = 0.05f;  // -0.32
    const float rFoot     = 0.05f;  // +0.32

    const auto headP  = pt(headTop, 0.0f);
    const auto neckP  = pt(neck, 0.0f);
    const auto spineP = pt(spineBot, 0.0f);
    const auto lEP    = pt(lElbow, -0.50f);
    const auto rEP    = pt(rElbow,  0.50f);
    const auto lHP    = pt(lHand, -0.44f);
    const auto rHP    = pt(rHand,  0.44f);
    const auto lKP    = pt(lKnee, -0.32f);
    const auto rKP    = pt(rKnee,  0.32f);
    const auto lFP    = pt(lFoot, -0.32f);
    const auto rFP    = pt(rFoot,  0.32f);

    add(headP, neckP);            // head -> neck
    add(neckP, spineP);           // spine
    add(neckP, lEP); add(lEP, lHP);   // L arm
    add(neckP, rEP); add(rEP, rHP);   // R arm
    add(spineP, lKP); add(lKP, lFP);  // L leg
    add(spineP, rKP); add(rKP, rFP);  // R leg
}

bool PreviewRenderer::ApplyLoadedModel(LoadedModel& model)
{
    std::vector<ModelVertex> combined = model.vertices;
    m_BodyVertCount = (unsigned int)model.vertices.size();
    m_WingVertCount = (unsigned int)model.wing_vertices.size();
    if (!model.wing_vertices.empty())
        combined.insert(combined.end(), model.wing_vertices.begin(), model.wing_vertices.end());

    unsigned int total = 0;
    if (!UploadVerts(m_Device, combined, &m_VB, total)) return false;
    if (m_BodyVertCount == 0 && m_WingVertCount > 0) {
        m_BodyVertCount = 0; // только крылья, ок
    }

    m_ModelScale = model.scale;
    for (int i = 0; i < 3; ++i) {
        m_ModelCenter[i] = model.center[i];
        m_RawMin[i] = model.raw_min[i];
        m_RawMax[i] = model.raw_max[i];
        m_BodyMin[i] = model.body_min[i];
        m_BodyMax[i] = model.body_max[i];
        m_BoxMin[i] = model.box_min[i];
        m_BoxMax[i] = model.box_max[i];
    }

    m_FrameCenter[0] = (m_BoxMin[0] + m_BoxMax[0]) * 0.5f;
    m_FrameCenter[1] = (m_BoxMin[1] + m_BoxMax[1]) * 0.5f;
    m_FrameCenter[2] = (m_BoxMin[2] + m_BoxMax[2]) * 0.5f;
    {
        const float ext[3] = {
            m_BoxMax[0] - m_BoxMin[0],
            m_BoxMax[1] - m_BoxMin[1],
            m_BoxMax[2] - m_BoxMin[2]
        };
        const float maxExt = (std::max)({ ext[0], ext[1], ext[2] });

        m_FrameScale = maxExt > 0.0f ? (0.82f / maxExt) : 1.0f;
    }

    m_UniquePos = std::move(model.positions);
    m_BodyPos = std::move(model.body_positions);
    m_BoxPos = std::move(model.box_positions);
    m_BodyParts = std::move(model.body_parts);
    m_BoxParts = std::move(model.box_parts);

    // Rebuild per-submesh SRVs (multi-material GLB). Any previous submesh SRVs
    // are released; if the model has no submeshes the single m_TexSRV path is
    // used instead (OBJ / humanoid / low-poly single-texture case).
    m_SubMeshCount = 0;
    if (!model.submeshes.empty())
    {
        const int n = (std::min)((int)model.submeshes.size(), (int)MaxSubMeshes);
        for (int i = 0; i < n; ++i)
        {
            m_SubMeshes[i].base = model.submeshes[i].base_vertex;
            m_SubMeshes[i].count = model.submeshes[i].vertex_count;
            m_SubMeshes[i].srv = RecreateSubTexSRV(m_SubMeshes[i].srv,
                                                   model.submeshes[i].texture);
        }
        m_SubMeshCount = n;
    }

    BuildR6SkeletonFromParts();
    BuildAimPartBoxes();

    m_SceneDirty = true;
    m_CachedBoundsValid = false;
    return true;
}

void PreviewRenderer::BuildAimPartBoxes()
{
    for (int i = 0; i < AimPartCount; ++i)
        m_AimParts[i] = {};

    const float bx0 = m_BodyMin[0], bx1 = m_BodyMax[0];
    const float by0 = m_BodyMin[1], by1 = m_BodyMax[1];
    const float cx = (bx0 + bx1) * 0.5f;
    const float h = (std::max)(0.01f, by1 - by0);
    const float w = (std::max)(0.01f, bx1 - bx0);

    const ModelPartAABB* head = nullptr;
    const ModelPartAABB* torso = nullptr;
    const ModelPartAABB* lArm = nullptr;
    const ModelPartAABB* rArm = nullptr;
    const ModelPartAABB* lLeg = nullptr;
    const ModelPartAABB* rLeg = nullptr;

    for (const auto& p : m_BodyParts) {
        if (!p.valid) continue;
        const float pcx = (p.min[0] + p.max[0]) * 0.5f;
        const float pcy = (p.min[1] + p.max[1]) * 0.5f;
        const float psy = p.max[1] - p.min[1];
        const float psx = p.max[0] - p.min[0];

        if (pcy > by0 + h * 0.82f && psy < h * 0.45f) {
            head = &p;
            continue;
        }
        if (pcy < by0 + h * 0.45f) {
            if (pcx < cx) lLeg = &p; else rLeg = &p;
            continue;
        }
        if (std::fabs(pcx - cx) > w * 0.28f && psx < w * 0.55f) {
            if (pcx < cx) lArm = &p; else rArm = &p;
            continue;
        }
        if (std::fabs(pcx - cx) <= w * 0.35f && psy >= h * 0.25f)
            torso = &p;
    }

    auto copy = [](AimPartAABB& dst, const ModelPartAABB& src) {
        for (int i = 0; i < 3; ++i) {
            dst.min[i] = src.min[i];
            dst.max[i] = src.max[i];
        }
        dst.valid = true;
    };

    if (head) copy(m_AimParts[0], *head);
    if (torso) copy(m_AimParts[1], *torso);
    if (lArm) copy(m_AimParts[4], *lArm);
    if (rArm) copy(m_AimParts[5], *rArm);
    if (lLeg) copy(m_AimParts[6], *lLeg);
    if (rLeg) copy(m_AimParts[7], *rLeg);
}

bool PreviewRenderer::LoadModel(const std::string& obj_path)
{
    LoadedModel model;
    if (!LoadOBJ(obj_path, model)) return false;
    return ApplyLoadedModel(model);
}

bool PreviewRenderer::LoadModelFromMemory(const char* obj_src, std::size_t obj_len)
{
    LoadedModel model;
    if (!LoadOBJFromMemory(obj_src, obj_len, model)) return false;
    return ApplyLoadedModel(model);
}

bool PreviewRenderer::LoadTexture(const std::string& tex_path)
{
    int w = 0, h = 0, n = 0;
    stbi_uc* data = stbi_load(tex_path.c_str(), &w, &h, &n, 4);
    if (!data) return false;
    const bool ok = UploadPng(m_Device, data, w, h, &m_TexRes, &m_TexSRV, &m_Sampler);
    stbi_image_free(data);
    if (ok) m_SceneDirty = true;
    return ok;
}

bool PreviewRenderer::LoadTextureFromMemory(const unsigned char* png, std::size_t png_len)
{
    if (!png || png_len == 0) return false;
    int w = 0, h = 0, n = 0;
    stbi_uc* data = stbi_load_from_memory(png, (int)png_len, &w, &h, &n, 4);
    if (!data) return false;
    const bool ok = UploadPng(m_Device, data, w, h, &m_TexRes, &m_TexSRV, &m_Sampler);
    stbi_image_free(data);
    if (ok) m_SceneDirty = true;
    return ok;
}

// Create (or replace) an SRV for a per-submesh texture. Reuses the shared
// m_Sampler at bind time, so it only allocates the texture+SRV.
ID3D11ShaderResourceView* PreviewRenderer::RecreateSubTexSRV(
    ID3D11ShaderResourceView*& srv, const std::vector<unsigned char>& bytes)
{
    if (srv) { srv->Release(); srv = nullptr; }
    if (bytes.empty()) return nullptr;

    int w = 0, h = 0, n = 0;
    stbi_uc* data = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &n, 4);
    if (!data) return nullptr;

    ID3D11Texture2D* tex = nullptr;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = (UINT)w; td.Height = (UINT)h; td.MipLevels = 0; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    if (FAILED(m_Device->CreateTexture2D(&td, nullptr, &tex))) { stbi_image_free(data); return nullptr; }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
    srvd.Format = td.Format;
    srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvd.Texture2D.MipLevels = (UINT)-1;
    ID3D11ShaderResourceView* nsrv = nullptr;
    if (FAILED(m_Device->CreateShaderResourceView(tex, &srvd, &nsrv))) {
        tex->Release(); stbi_image_free(data); return nullptr;
    }
    if (m_Ctx) {
        m_Ctx->UpdateSubresource(tex, 0, nullptr, data, (UINT)(w * 4), 0);
        m_Ctx->GenerateMips(nsrv);
    }
    stbi_image_free(data);
    tex->Release(); // the SRV keeps its own reference

    srv = nsrv;
    return nsrv;
}

bool PreviewRenderer::SaveRT(const char* path)
{
    if (!m_ResolveTex || !m_Device || !m_Ctx) return false;
    ID3D11Texture2D* stage = nullptr;
    D3D11_TEXTURE2D_DESC sd{};
    sd.Width = m_Width; sd.Height = m_Height; sd.MipLevels = 1; sd.ArraySize = 1;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.SampleDesc.Count = 1; sd.SampleDesc.Quality = 0;
    sd.Usage = D3D11_USAGE_STAGING; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(m_Device->CreateTexture2D(&sd, nullptr, &stage))) return false;
    m_Ctx->CopyResource(stage, m_ResolveTex);
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (FAILED(m_Ctx->Map(stage, 0, D3D11_MAP_READ, 0, &ms)))
    {
        stage->Release();
        return false;
    }
    FILE* f = nullptr;
    if (fopen_s(&f, path, "wb") != 0 || !f) { m_Ctx->Unmap(stage, 0); stage->Release(); return false; }
    int w = (int)m_Width, h = (int)m_Height;
    int rowBytes = w * 3;
    int pad = (4 - (rowBytes % 4)) % 4;
    int stride = rowBytes + pad;
    int dataSize = stride * h;
    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih{};
    fh.bfType = 0x4D42;
    fh.bfSize = (DWORD)(sizeof(fh) + sizeof(ih) + dataSize);
    fh.bfOffBits = (DWORD)(sizeof(fh) + sizeof(ih));
    ih.biSize = sizeof(ih);
    ih.biWidth = w; ih.biHeight = h; ih.biPlanes = 1;
    ih.biBitCount = 24; ih.biCompression = 0;
    fwrite(&fh, 1, sizeof(fh), f);
    fwrite(&ih, 1, sizeof(ih), f);
    unsigned char* row = new unsigned char[stride];
    for (int y = 0; y < h; ++y)
    {
        const unsigned char* src = (const unsigned char*)ms.pData + y * ms.RowPitch;
        for (int x = 0; x < w; ++x)
        {
            row[x * 3 + 0] = src[x * 4 + 0];
            row[x * 3 + 1] = src[x * 4 + 1];
            row[x * 3 + 2] = src[x * 4 + 2];
        }
        fwrite(row, 1, stride, f);
    }
    delete[] row;
    fclose(f);
    m_Ctx->Unmap(stage, 0);
    stage->Release();
    return true;
}

