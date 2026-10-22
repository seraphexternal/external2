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

    // ── Small row-major 4x4 helpers (D3D convention: p' = p * M) ─────────────
    void MatIdentity(float* m) {
        memset(m, 0, 64);
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }
    void MatMul(const float* a, const float* b, float* out) {
        float r[16];
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                float s = 0.0f;
                for (int k = 0; k < 4; ++k) s += a[i * 4 + k] * b[k * 4 + j];
                r[i * 4 + j] = s;
            }
        memcpy(out, r, 64);
    }
    // Rotation about an arbitrary axis (Rodrigues) as a row-major 4x4.
    void MatFromAxisAngle(const float axis[3], float angle, float* out) {
        float x = axis[0], y = axis[1], z = axis[2];
        float len = sqrtf(x * x + y * y + z * z);
        if (len < 1e-8f) { MatIdentity(out); return; }
        x /= len; y /= len; z /= len;
        const float c = cosf(angle), s = sinf(angle), t = 1.0f - c;
        out[0] = c + x * x * t;     out[1] = x * y * t + z * s; out[2] = x * z * t - y * s; out[3] = 0;
        out[4] = y * x * t - z * s; out[5] = c + y * y * t;     out[6] = y * z * t + x * s; out[7] = 0;
        out[8] = z * x * t + y * s; out[9] = z * y * t - x * s; out[10] = c + z * z * t;     out[11] = 0;
        out[12] = 0; out[13] = 0; out[14] = 0; out[15] = 1;
    }
    // A = T(-p) * R * T(p): rotation about pivot p, in model space.
    void MatPivotRot(const float pivot[3], const float axis[3], float angle, float* out) {
        float R[16]; MatFromAxisAngle(axis, angle, R);
        // Pivot rotate = R with translation (p - p*R)
        float rot[16]; memcpy(rot, R, 64);
        for (int j = 0; j < 3; ++j)
            rot[12 + j] = pivot[j] - (pivot[0] * R[0 * 4 + j] + pivot[1] * R[1 * 4 + j] + pivot[2] * R[2 * 4 + j]);
        memcpy(out, rot, 64);
    }

    // Quaternion helpers (rotation matrices in row-vector convention, matching
    // MatFromAxisAngle above).
    void QuatFromAxisAngle(const float axis[3], float angle, float q[4]) {
        float x = axis[0], y = axis[1], z = axis[2];
        float len = sqrtf(x * x + y * y + z * z);
        if (len < 1e-8f) { q[0] = q[1] = q[2] = 0.0f; q[3] = 1.0f; return; }
        x /= len; y /= len; z /= len;
        const float h = angle * 0.5f, s = sinf(h);
        q[0] = x * s; q[1] = y * s; q[2] = z * s; q[3] = cosf(h);
    }
    // q = a * b (same order as rotation-matrix multiplication).
    void QuatMul(const float a[4], const float b[4], float out[4]) {
        float r[4];
        r[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
        r[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
        r[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
        r[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
        memcpy(out, r, 16);
    }
    void MatFromQuat(const float q[4], float* out) {
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        out[0] = 1 - 2 * (y * y + z * z); out[1] = 2 * (x * y + w * z); out[2] = 2 * (x * z - w * y); out[3] = 0;
        out[4] = 2 * (x * y - w * z);     out[5] = 1 - 2 * (x * x + z * z); out[6] = 2 * (y * z + w * x); out[7] = 0;
        out[8] = 2 * (x * z + w * y);     out[9] = 2 * (y * z - w * x);     out[10] = 1 - 2 * (x * x + y * y); out[11] = 0;
        out[12] = 0; out[13] = 0; out[14] = 0; out[15] = 1;
    }
    void MatPivotQuat(const float pivot[3], const float q[4], float* out) {
        float R[16]; MatFromQuat(q, R);
        for (int j = 0; j < 3; ++j)
            R[12 + j] = pivot[j] - (pivot[0] * R[0 * 4 + j] + pivot[1] * R[1 * 4 + j] + pivot[2] * R[2 * 4 + j]);
        memcpy(out, R, 64);
    }
    // Pure-rotation transpose as a row-major 4x4 (translation cleared).
    void MatRotTranspose(const float* m, float* out) {
        out[0] = m[0]; out[1] = m[4]; out[2] = m[8];  out[3] = 0;
        out[4] = m[1]; out[5] = m[5]; out[6] = m[9];  out[7] = 0;
        out[8] = m[2]; out[9] = m[6]; out[10] = m[10]; out[11] = 0;
        out[12] = 0; out[13] = 0; out[14] = 0; out[15] = 1;
    }
    // Same as MatPivotQuat but from an already-built rotation matrix.
    void MatPivotFromRot(const float pivot[3], const float R[16], float* out) {
        memcpy(out, R, 64);
        for (int j = 0; j < 3; ++j)
            out[12 + j] = pivot[j] - (pivot[0] * R[0 * 4 + j] + pivot[1] * R[1 * 4 + j] + pivot[2] * R[2 * 4 + j]);
    }
    // Quaternion for a row-vector rotation matrix (inverse of MatFromQuat).
    void QuatFromMatRow(const float R[3][3], float q[4]) {
        // MatFromQuat(q) stores R_c^T; recover the column-vector matrix first.
        float c[3][3];
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) c[i][j] = R[j][i];
        const float tr = c[0][0] + c[1][1] + c[2][2];
        float x, y, z, w;
        if (tr > 0.0f) {
            float s = sqrtf(tr + 1.0f) * 2.0f;
            w = 0.25f * s; x = (c[2][1] - c[1][2]) / s; y = (c[0][2] - c[2][0]) / s; z = (c[1][0] - c[0][1]) / s;
        } else if (c[0][0] > c[1][1] && c[0][0] > c[2][2]) {
            float s = sqrtf(1.0f + c[0][0] - c[1][1] - c[2][2]) * 2.0f;
            w = (c[2][1] - c[1][2]) / s; x = 0.25f * s; y = (c[0][1] + c[1][0]) / s; z = (c[0][2] + c[2][0]) / s;
        } else if (c[1][1] > c[2][2]) {
            float s = sqrtf(1.0f + c[1][1] - c[0][0] - c[2][2]) * 2.0f;
            w = (c[0][2] - c[2][0]) / s; x = (c[0][1] + c[1][0]) / s; y = 0.25f * s; z = (c[1][2] + c[2][1]) / s;
        } else {
            float s = sqrtf(1.0f + c[2][2] - c[0][0] - c[1][1]) * 2.0f;
            w = (c[1][0] - c[0][1]) / s; x = (c[0][2] + c[2][0]) / s; y = (c[1][2] + c[2][1]) / s; z = 0.25f * s;
        }
        q[0] = x; q[1] = y; q[2] = z; q[3] = w;
    }
    // General 4x4 inverse (Gauss-Jordan, double precision). Returns false if
    // the matrix is singular (then `out` is left untouched).
    bool MatInverse(const float* m, float* out) {
        double a[4][8];
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) {
                a[r][c] = m[r * 4 + c];
                a[r][4 + c] = (r == c) ? 1.0 : 0.0;
            }
        for (int i = 0; i < 4; ++i) {
            int piv = i;
            for (int r = i + 1; r < 4; ++r)
                if (fabs(a[r][i]) > fabs(a[piv][i])) piv = r;
            if (fabs(a[piv][i]) < 1e-12) return false;
            if (piv != i)
                for (int c = 0; c < 8; ++c) { const double t = a[i][c]; a[i][c] = a[piv][c]; a[piv][c] = t; }
            const double d = a[i][i];
            for (int c = 0; c < 8; ++c) a[i][c] /= d;
            for (int r = 0; r < 4; ++r) {
                if (r == i) continue;
                const double f = a[r][i];
                if (f == 0.0) continue;
                for (int c = 0; c < 8; ++c) a[r][c] -= f * a[i][c];
            }
        }
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                out[r * 4 + c] = (float)a[r][4 + c];
        return true;
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

    // Skeleton / skin data (filled only for rigged glTF models).
    m_Nodes = std::move(model.nodes);
    m_SkinJoints = std::move(model.skinJoints);
    m_InvBind = std::move(model.invBind);
    m_HasSkin = model.hasSkin && !m_SkinJoints.empty();
    m_JointQuat.assign(m_Nodes.size() * 4, 0.0f);
    for (size_t i = 0; i < m_Nodes.size(); ++i) m_JointQuat[i * 4 + 3] = 1.0f;
    m_AbsQuat.assign(m_Nodes.size() * 4, 0.0f);
    m_AbsSet.assign(m_Nodes.size(), 0);
    m_BoneMatrices.assign(m_SkinJoints.size() * 16, 0.0f);
    m_PoseDirty = true;
    ComputeBindWorlds();
    // Derive inverse binds from the (correctly parsed) node hierarchy.
    FinalizeSkin();

    BuildR6SkeletonFromParts();

    // Unskinned models (Mario, TungTung, OBJ fallback): synthesize a humanoid
    // rig + per-vertex weights so the emotes drive them too.
    if (!m_HasSkin)
        SynthesizeSkin(combined);

    // Derive the semantic axes the emote poses are expressed against (X=side,
    // Y=up, Z=depth) from the bind pivots, so emotes behave the same on a Y-up
    // humanoid and a Z-up plush.
    UpdateEmoteAxes();

    unsigned int total = 0;
    if (!UploadVerts(m_Device, combined, &m_VB, total)) return false;
    if (m_BodyVertCount == 0 && m_WingVertCount > 0) {
        m_BodyVertCount = 0; // только крылья, ок
    }

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

// ── Skeletal pose implementation ────────────────────────────────────────────

void PreviewRenderer::ComputeBindWorlds()
{
    const int n = (int)m_Nodes.size();
    m_NodeWorld.assign(n * 16, 0.0f);
    m_NodeOrder.clear();
    if (n == 0) return;

    std::vector<int> depth(n, 0);
    for (int i = 0; i < n; ++i) {
        int d = 0, p = m_Nodes[i].parent;
        while (p >= 0 && p < n) { d++; p = m_Nodes[p].parent; if (d > n) break; }
        depth[i] = d;
    }
    m_NodeOrder.resize(n);
    for (int i = 0; i < n; ++i) m_NodeOrder[i] = i;
    std::sort(m_NodeOrder.begin(), m_NodeOrder.end(),
              [&](int a, int b) { return depth[a] < depth[b]; });

    for (int idx : m_NodeOrder) {
        float* w = &m_NodeWorld[idx * 16];
        int p = m_Nodes[idx].parent;
        if (p >= 0 && p < n)
            MatMul(m_Nodes[idx].local, &m_NodeWorld[p * 16], w);
        else
            memcpy(w, m_Nodes[idx].local, 64);
    }
}

// Finalizes skin data after ComputeBindWorlds(): recomputes inverse bind
// matrices from the node hierarchy. glTF guarantees inverseBindMatrices ==
// inverse(joint rest world), so deriving them from the (now correctly parsed)
// hierarchy keeps the bind pose undistorted and the pivots exactly on the
// geometry, and works even when a file ships no inverseBindMatrices.
void PreviewRenderer::FinalizeSkin()
{
    if (!m_HasSkin || m_SkinJoints.empty()) return;
    const int n = (int)m_Nodes.size();
    if (n == 0) return;

    m_InvBind.assign(m_SkinJoints.size() * 16, 0.0f);
    for (int s = 0; s < (int)m_SkinJoints.size(); ++s) {
        float* dst = &m_InvBind[s * 16];
        const int ni = m_SkinJoints[s];
        if (ni >= 0 && ni < n && MatInverse(&m_NodeWorld[ni * 16], dst))
            continue;
        MatIdentity(dst); // degenerate joint: leave its verts untouched
    }
}

// Procedural humanoid rig for models WITHOUT a glTF skin (Mario, TungTung,
// OBJ fallbacks). Classifies every vertex into head / torso / arms / legs
// using the same signed up axis and side conventions as the aim skeleton, and
// synthesizes joints named exactly like the emote system's targets, so the
// emotes drive unskinned models too. Weights blend toward the torso near the
// region borders so joints don't tear holes when they rotate.
void PreviewRenderer::SynthesizeSkin(std::vector<ModelVertex>& verts)
{
    if (verts.empty()) return;

    float ext[3];
    for (int k = 0; k < 3; ++k) ext[k] = m_BodyMax[k] - m_BodyMin[k];

    // Signed up axis: negative m_SkelUpAxis = head at the MINIMUM end.
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
    int dep  = 3 - up - side;
    // TungTung's true left-right axis is the NARROWEST horizontal one (the
    // generic widest pick lands along its facing) - mirror the skeleton.
    if (up == 2 && m_SkelUpAxis == -2) { std::swap(side, dep); }

    const float H  = (std::max)(0.01f, ext[up]);
    const float Ws = (std::max)(0.01f, ext[side]);
    const float upBase = flip ? m_BodyMax[up] : m_BodyMin[up];
    const float upStep = flip ? -H : H;   // + goes toward the head
    const float halfW  = Ws * 0.5f;
    auto centerAxis = [&](int k) { return (m_BodyMin[k] + m_BodyMax[k]) * 0.5f; };
    const float sideC  = centerAxis(side);
    const float depthC = centerAxis(dep);

    // Joint pivot at height u (0=feet,1=head), lateral offset in half-widths.
    auto P = [&](float u, float lat) -> std::array<float, 3> {
        std::array<float, 3> p{};
        p[up]  = upBase + upStep * u;
        p[side] = sideC + halfW * lat;
        p[dep]  = depthC;
        return p;
    };
    const auto torsoP = P(0.55f, 0.0f);
    const auto headP  = P(0.86f, 0.0f);
    const auto lArmP  = P(0.64f, -0.50f);
    const auto rArmP  = P(0.64f,  0.50f);
    const auto lLegP  = P(0.46f, -0.16f);
    const auto rLegP  = P(0.46f,  0.16f);

    m_Nodes.clear();
    m_SkinJoints.clear();
    auto addJoint = [&](const char* name, int parent,
                        const std::array<float, 3>& pivot,
                        const std::array<float, 3>& parentPivot) {
        ModelNode mn;
        mn.name = name;
        mn.parent = parent;
        for (int k = 0; k < 3; ++k) mn.local[12 + k] = pivot[k] - parentPivot[k];
        mn.jointSlot = (int)m_SkinJoints.size();
        m_Nodes.push_back(mn);
        m_SkinJoints.push_back((int)m_Nodes.size() - 1);
    };
    const std::array<float, 3> origin{};
    addJoint("Torso",     -1, torsoP, origin);
    addJoint("Head",       0, headP,  torsoP);
    addJoint("Left Arm",   0, lArmP,  torsoP);
    addJoint("Right Arm",  0, rArmP,  torsoP);
    addJoint("Left Leg",   0, lLegP,  torsoP);
    addJoint("Right Leg",  0, rLegP,  torsoP);

    // ── per-vertex classification (mirrors the skeleton proportions) ──
    const int kTorso = 0, kHead = 1, kLArm = 2, kRArm = 3, kLLeg = 4, kRLeg = 5;
    const float kHeadEdge = 0.86f, kHeadBlend = 0.05f; // head above 0.86
    const float kLegEdge  = 0.46f, kLegBlend  = 0.06f; // legs below 0.46
    const float kArmEdge  = 0.50f, kArmBlend  = 0.10f; // arms outside 0.50 halfW
    const float kArmTop   = 0.72f;                     // and not above 0.72
    for (auto& v : verts) {
        const float p[3] = { v.x, v.y, v.z };
        const float u = (p[up] - upBase) / upStep;  // 0=feet end, 1=head end
        const float latN = (p[side] - sideC) / halfW;

        int   primary = kTorso;
        float w = 1.0f;
        if (u > kHeadEdge) {
            primary = kHead;
            w = (u - kHeadEdge) / kHeadBlend;
        } else if (u < kLegEdge) {
            primary = (latN < 0.0f) ? kLLeg : kRLeg;
            w = (kLegEdge - u) / kLegBlend;
        } else if (u < kArmTop && (latN < 0.0f ? -latN : latN) > kArmEdge) {
            primary = (latN < 0.0f) ? kLArm : kRArm;
            w = ((latN < 0.0f ? -latN : latN) - kArmEdge) / kArmBlend;
        }
        if (w < 0.0f) w = 0.0f;
        if (w > 1.0f) w = 1.0f;

        v.boneIndices[0] = (unsigned char)primary;
        v.boneWeights[0] = w;
        v.boneIndices[1] = (unsigned char)kTorso;
        v.boneWeights[1] = (primary == kTorso) ? 0.0f : (1.0f - w);
        v.boneIndices[2] = v.boneIndices[3] = 0;
        v.boneWeights[2] = v.boneWeights[3] = 0.0f;
    }

    m_HasSkin = true;
    m_JointQuat.assign(m_Nodes.size() * 4, 0.0f);
    for (size_t i = 0; i < m_Nodes.size(); ++i) m_JointQuat[i * 4 + 3] = 1.0f;
    m_AbsQuat.assign(m_Nodes.size() * 4, 0.0f);
    m_AbsSet.assign(m_Nodes.size(), 0);
    m_BoneMatrices.assign(m_SkinJoints.size() * 16, 0.0f);
    m_PoseDirty = true;
    ComputeBindWorlds();
    FinalizeSkin();    // vertices are already in model space: no frame shift
}

int PreviewRenderer::FindJointSlot(const char* jointName) const
{
    if (!jointName || !*jointName) return -1;
    const size_t want = strlen(jointName);
    for (int s = 0; s < (int)m_SkinJoints.size(); ++s) {
        int ni = m_SkinJoints[s];
        if (ni < 0 || ni >= (int)m_Nodes.size()) continue;
        const std::string& nm = m_Nodes[ni].name;
        if (nm.size() < want) continue;
        bool match = true;
        for (size_t i = 0; i < want; ++i) {
            char a = (char)tolower((unsigned char)nm[i]);
            char b = (char)tolower((unsigned char)jointName[i]);
            if (a != b) { match = false; break; }
        }
        if (match) return s;
    }
    return -1;
}

const char* PreviewRenderer::JointName(int slot) const
{
    if (slot < 0 || slot >= (int)m_SkinJoints.size()) return "";
    int ni = m_SkinJoints[slot];
    if (ni < 0 || ni >= (int)m_Nodes.size()) return "";
    return m_Nodes[ni].name.c_str();
}

void PreviewRenderer::ClearPose()
{
    for (size_t i = 0; i < m_Nodes.size(); ++i) {
        m_JointQuat[i * 4 + 0] = 0.0f;
        m_JointQuat[i * 4 + 1] = 0.0f;
        m_JointQuat[i * 4 + 2] = 0.0f;
        m_JointQuat[i * 4 + 3] = 1.0f;
    }
    ClearWorldDeltas();
    m_PoseDirty = true;
}

bool PreviewRenderer::SetJointRotationAxis(const char* jointName, float ax, float ay, float az, float angleRad)
{
    const int slot = FindJointSlot(jointName);
    if (slot < 0) return false;
    const int ni = m_SkinJoints[slot];
    if (ni < 0 || ni >= (int)m_Nodes.size()) return false;
    float axis[3];
    MapEmoteAxis(ax, ay, az, axis);
    QuatFromAxisAngle(axis, angleRad, &m_JointQuat[ni * 4]);
    m_PoseDirty = true;
    return true;
}

bool PreviewRenderer::AddJointRotationAxis(const char* jointName, float ax, float ay, float az, float angleRad)
{
    const int slot = FindJointSlot(jointName);
    if (slot < 0) return false;
    const int ni = m_SkinJoints[slot];
    if (ni < 0 || ni >= (int)m_Nodes.size()) return false;
    float axis[3];
    MapEmoteAxis(ax, ay, az, axis);
    float nq[4];
    QuatFromAxisAngle(axis, angleRad, nq);
    float rq[4];
    // Compose so the new rotation is applied after the existing one.
    QuatMul(&m_JointQuat[ni * 4], nq, rq);
    memcpy(&m_JointQuat[ni * 4], rq, 16);
    m_PoseDirty = true;
    return true;
}

// Absolute joint orientation for the player mirror. `q` is the joint's new
// orientation expressed in the same semantic frame the emotes use (X = side,
// Y = up, Z = depth); the renderer re-expresses it against the model's axes.
// Stored separately from m_JointQuat so it composes down the hierarchy as an
// ABSOLUTE world delta instead of a per-joint delta.
bool PreviewRenderer::SetJointDeltaSemantic(const char* jointName,
                                            float qx, float qy, float qz, float qw)
{
    const int slot = FindJointSlot(jointName);
    if (slot < 0) return false;
    const int ni = m_SkinJoints[slot];
    if (ni < 0 || ni >= (int)m_Nodes.size()) return false;
    if (m_AbsSet.size() != m_Nodes.size())
        m_AbsSet.assign(m_Nodes.size(), 0);
    if (m_AbsQuat.size() != m_Nodes.size() * 4)
        m_AbsQuat.assign(m_Nodes.size() * 4, 0.0f);

    float nrm = sqrtf(qx * qx + qy * qy + qz * qz + qw * qw);
    if (nrm < 1e-8f) { m_AbsSet[ni] = 0; m_PoseDirty = true; return true; }
    qx /= nrm; qy /= nrm; qz /= nrm; qw /= nrm;

    float qsem[4] = { qx, qy, qz, qw };
    float q2[4]; SemanticToModelQuat(qsem, q2);
    memcpy(&m_AbsQuat[ni * 4], q2, 16);
    m_AbsSet[ni] = 1;
    m_PoseDirty = true;
    return true;
}

// Re-express a semantic-frame quaternion (X = side, Y = up, Z = depth) as a
// model-space quaternion: R_model = B * R_sem * B^T, where the columns of B are
// the semantic axes in the model's own space.
void PreviewRenderer::SemanticToModelQuat(const float qsem[4], float out[4])
{
    float Rs[16]; MatFromQuat(qsem, Rs);
    float B[3][3] = {
        { m_EmoteAxisSide[0],  m_EmoteAxisUp[0],  m_EmoteAxisDepth[0] },
        { m_EmoteAxisSide[1],  m_EmoteAxisUp[1],  m_EmoteAxisDepth[1] },
        { m_EmoteAxisSide[2],  m_EmoteAxisUp[2],  m_EmoteAxisDepth[2] }
    };
    float tmp[3][3], Rm[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            float s = 0.0f;
            for (int k = 0; k < 3; ++k) s += B[i][k] * Rs[k * 4 + j];
            tmp[i][j] = s;
        }
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            float s = 0.0f;
            for (int k = 0; k < 3; ++k) s += tmp[i][k] * B[j][k]; // B^T[k][j] = B[j][k]
            Rm[i][j] = s;
        }
    QuatFromMatRow(Rm, out);
}

// Local (parent-relative) joint rotation for keyframe animation. Same axis
// mapping as the emote helpers, but stored in m_JointQuat so it composes down
// the hierarchy (a limb stays attached to its rotated parent).
bool PreviewRenderer::SetJointLocalQuat(const char* jointName,
                                        float qx, float qy, float qz, float qw)
{
    const int slot = FindJointSlot(jointName);
    if (slot < 0) return false;
    const int ni = m_SkinJoints[slot];
    if (ni < 0 || ni >= (int)m_Nodes.size()) return false;

    float nrm = sqrtf(qx * qx + qy * qy + qz * qz + qw * qw);
    if (nrm < 1e-8f) {
        m_JointQuat[ni * 4 + 0] = 0.0f;
        m_JointQuat[ni * 4 + 1] = 0.0f;
        m_JointQuat[ni * 4 + 2] = 0.0f;
        m_JointQuat[ni * 4 + 3] = 1.0f;
        m_PoseDirty = true;
        return true;
    }
    float qsem[4] = { qx / nrm, qy / nrm, qz / nrm, qw / nrm };
    float qm[4]; SemanticToModelQuat(qsem, qm);
    memcpy(&m_JointQuat[ni * 4], qm, 16);
    m_PoseDirty = true;
    return true;
}

void PreviewRenderer::ClearWorldDeltas()
{
    if (!m_AbsSet.empty()) memset(m_AbsSet.data(), 0, m_AbsSet.size());
    m_PoseDirty = true;
}

// The emote poses are authored against a frame where X = side, Y = up, Z =
// depth. Re-express such an axis in the loaded model's actual orientation.
void PreviewRenderer::MapEmoteAxis(float ax, float ay, float az, float out[3]) const
{
    for (int k = 0; k < 3; ++k)
        out[k] = ax * m_EmoteAxisSide[k] + ay * m_EmoteAxisUp[k] + az * m_EmoteAxisDepth[k];
    const float len = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    if (len > 1e-6f) { out[0] /= len; out[1] /= len; out[2] /= len; }
}

// Build the semantic axes from the bind joint pivots:
//   up    = from torso toward head,
//   side  = from the right limb toward the left limb (i.e. points to the left),
//   depth = up x side (right-handed).
// Falls back to the skeleton up axis / model bounds when a joint is missing.
void PreviewRenderer::UpdateEmoteAxes()
{
    m_EmoteAxisUp[0] = 0.0f; m_EmoteAxisUp[1] = 1.0f; m_EmoteAxisUp[2] = 0.0f;
    m_EmoteAxisSide[0] = 1.0f; m_EmoteAxisSide[1] = 0.0f; m_EmoteAxisSide[2] = 0.0f;
    m_EmoteAxisDepth[0] = 0.0f; m_EmoteAxisDepth[1] = 0.0f; m_EmoteAxisDepth[2] = 1.0f;
    if (m_SkinJoints.empty() || m_Nodes.empty()) return;

    auto pivotOf = [&](const char* nm, float out[3]) -> bool {
        const int slot = FindJointSlot(nm);
        if (slot < 0) return false;
        const int ni = m_SkinJoints[slot];
        if (ni < 0 || ni >= (int)m_Nodes.size()) return false;
        const float* w = &m_NodeWorld[ni * 16];
        out[0] = w[12]; out[1] = w[13]; out[2] = w[14];
        return true;
    };

    float head[3], torso[3], larm[3], rarm[3];
    const bool haveUp = pivotOf("Head", head) && pivotOf("Torso", torso);
    const bool haveSide = pivotOf("Left Arm", larm) && pivotOf("Right Arm", rarm);

    if (haveUp) {
        float u[3] = { head[0] - torso[0], head[1] - torso[1], head[2] - torso[2] };
        const float len = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
        if (len > 1e-6f) { m_EmoteAxisUp[0] = u[0] / len; m_EmoteAxisUp[1] = u[1] / len; m_EmoteAxisUp[2] = u[2] / len; }
    }
    if (haveSide) {
        float s[3] = { larm[0] - rarm[0], larm[1] - rarm[1], larm[2] - rarm[2] };
        // Remove any up component so the two axes are orthogonal.
        const float d = s[0] * m_EmoteAxisUp[0] + s[1] * m_EmoteAxisUp[1] + s[2] * m_EmoteAxisUp[2];
        s[0] -= d * m_EmoteAxisUp[0]; s[1] -= d * m_EmoteAxisUp[1]; s[2] -= d * m_EmoteAxisUp[2];
        const float len = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
        if (len > 1e-6f) { m_EmoteAxisSide[0] = s[0] / len; m_EmoteAxisSide[1] = s[1] / len; m_EmoteAxisSide[2] = s[2] / len; }
    }
    // depth = up x side
    m_EmoteAxisDepth[0] = m_EmoteAxisUp[1] * m_EmoteAxisSide[2] - m_EmoteAxisUp[2] * m_EmoteAxisSide[1];
    m_EmoteAxisDepth[1] = m_EmoteAxisUp[2] * m_EmoteAxisSide[0] - m_EmoteAxisUp[0] * m_EmoteAxisSide[2];
    m_EmoteAxisDepth[2] = m_EmoteAxisUp[0] * m_EmoteAxisSide[1] - m_EmoteAxisUp[1] * m_EmoteAxisSide[0];
}

bool PreviewRenderer::RotateJointX(const char* n, float a) { return SetJointRotationAxis(n, 1, 0, 0, a); }
bool PreviewRenderer::RotateJointY(const char* n, float a) { return SetJointRotationAxis(n, 0, 1, 0, a); }
bool PreviewRenderer::RotateJointZ(const char* n, float a) { return SetJointRotationAxis(n, 0, 0, 1, a); }

void PreviewRenderer::RecomputeBoneMatrices()
{
    const int n = (int)m_Nodes.size();
    if (n == 0 || m_SkinJoints.empty()) return;

    // Animated world matrix per node, in the mesh node's frame (see
    // FinalizeSkin). Parents are visited before children (m_NodeOrder).
    // `habs` tracks each node's ABSOLUTE model-space rotation delta (identity
    // unless driven by the player mirror) so an absolute child can subtract its
    // parent's delta and stay attached while following it.
    std::vector<float> ja(n * 16, 0.0f);
    std::vector<float> habs(n * 16, 0.0f);
    const bool haveAbsBuf = (int)m_AbsSet.size() == n && (int)m_AbsQuat.size() == n * 4;
    for (int idx : m_NodeOrder) {
        const float* loc = m_Nodes[idx].local;
        int p = m_Nodes[idx].parent;
        if (p >= 0 && p < n)
            MatMul(loc, &ja[p * 16], &ja[idx * 16]);
        else
            memcpy(&ja[idx * 16], loc, 64);

        float parentH[16];
        if (p >= 0 && p < n) memcpy(parentH, &habs[p * 16], 64);
        else                 MatIdentity(parentH);

        const bool haveAbs = haveAbsBuf && m_AbsSet[idx] != 0;
        if (haveAbs) {
            // Absolute model-space orientation: H = desired delta, and the
            // per-joint delta we must apply is P = H_parent^-1 * H.
            float H[16]; MatFromQuat(&m_AbsQuat[idx * 4], H);
            float pH[16]; MatRotTranspose(parentH, pH);
            float P[16];  MatMul(pH, H, P);
            const float pivot[3] = {
                ja[idx * 16 + 12], ja[idx * 16 + 13], ja[idx * 16 + 14]
            };
            float Pm[16]; MatPivotFromRot(pivot, P, Pm);
            MatMul(&ja[idx * 16], Pm, &ja[idx * 16]);
            memcpy(&habs[idx * 16], H, 64);
        } else {
            memcpy(&habs[idx * 16], parentH, 64);
            const float* q = &m_JointQuat[idx * 4];
            if (!(q[0] == 0.0f && q[1] == 0.0f && q[2] == 0.0f && q[3] == 1.0f)) {
                // Rotate this node's subtree about its CURRENT origin: the pivot
                // is taken after the parent chain was applied, so a limb stays
                // attached when its parent (torso, etc.) is animated.
                const float pivot[3] = {
                    ja[idx * 16 + 12], ja[idx * 16 + 13], ja[idx * 16 + 14]
                };
                float P[16];
                MatPivotQuat(pivot, q, P);
                MatMul(&ja[idx * 16], P, &ja[idx * 16]);
            }
        }
    }

    for (int s = 0; s < (int)m_SkinJoints.size(); ++s) {
        int ni = m_SkinJoints[s];
        if (ni < 0 || ni >= n) { MatIdentity(&m_BoneMatrices[s * 16]); continue; }
        MatMul(&ja[ni * 16], &m_InvBind[s * 16], &m_BoneMatrices[s * 16]);
    }
}

