namespace Cheat::Core {
namespace {

bool IsHandleGroup(const std::string& g)
{
    return g.size() >= 6 && g.compare(0, 6, "Handle") == 0;
}

void ExpandAABB(ModelPartAABB& a, float x, float y, float z)
{
	if (!a.valid)
	{
		a.min[0] = a.max[0] = x;
		a.min[1] = a.max[1] = y;
		a.min[2] = a.max[2] = z;
		a.valid = true;
		return;
	}

	if (x < a.min[0]) a.min[0] = x;
	if (x > a.max[0]) a.max[0] = x;
	if (y < a.min[1]) a.min[1] = y;
	if (y > a.max[1]) a.max[1] = y;
	if (z < a.min[2]) a.min[2] = z;
	if (z > a.max[2]) a.max[2] = z;
}

void BoundsFromPos(const std::vector<float>& pos, float mn[3], float mx[3])
{
    mn[0] = mn[1] = mn[2] = 1e9f;
    mx[0] = mx[1] = mx[2] = -1e9f;
    for (size_t i = 0; i + 2 < pos.size(); i += 3) {
        mn[0] = (std::min)(mn[0], pos[i]);     mx[0] = (std::max)(mx[0], pos[i]);
        mn[1] = (std::min)(mn[1], pos[i + 1]); mx[1] = (std::max)(mx[1], pos[i + 1]);
        mn[2] = (std::min)(mn[2], pos[i + 2]); mx[2] = (std::max)(mx[2], pos[i + 2]);
    }
}

// крылья / огромные аксы в отдельный батч
bool IsWingLikeAccessory(const ModelPartAABB& a, float bodyW, float bodyH)
{
	if (!a.valid)
		return false;

	float ex = a.max[0] - a.min[0];
	float ey = a.max[1] - a.min[1];
	float ez = a.max[2] - a.min[2];

	float wingW = bodyW * 1.25f;
	if (wingW < 3.2f) wingW = 3.2f;
	float wingD = bodyW * 1.05f;
	if (wingD < 2.4f) wingD = 2.4f;

	if (ex >= wingW)
		return true;

	if (ez >= wingD && ez > ey * 0.85f)
		return true;

	if (ex >= bodyH * 0.85f && ex > ey * 1.4f)
		return true;

	return false;
}

struct GroupAccum {
    std::string name;
    bool handle = false;
    ModelPartAABB aabb{};
    std::vector<float> pos;
    std::vector<ModelVertex> mesh;
};

void FinalizeModel(LoadedModel& out, std::vector<GroupAccum>& groups)
{
    out.vertices.clear();
    out.wing_vertices.clear();
    out.body_positions.clear();
    out.body_parts.clear();
    out.box_positions.clear();
    out.box_parts.clear();

    for (auto& g : groups) {
        if (!g.handle && g.aabb.valid) {
            out.body_parts.push_back(g.aabb);
            out.body_positions.insert(out.body_positions.end(), g.pos.begin(), g.pos.end());
        }
    }

    if (!out.body_positions.empty())
        BoundsFromPos(out.body_positions, out.body_min, out.body_max);

    else if (!out.positions.empty())
        BoundsFromPos(out.positions, out.body_min, out.body_max);

    float bodyW = out.body_max[0] - out.body_min[0];
    float bodyH = out.body_max[1] - out.body_min[1];
    if (bodyW < 0.01f) bodyW = 0.01f;
    if (bodyH < 0.01f) bodyH = 0.01f;

    for (auto& g : groups)
    {
        bool wing = g.handle && IsWingLikeAccessory(g.aabb, bodyW, bodyH);
        if (wing)
        {
            out.wing_vertices.insert(out.wing_vertices.end(), g.mesh.begin(), g.mesh.end());
            continue;
        }

        out.vertices.insert(out.vertices.end(), g.mesh.begin(), g.mesh.end());

        if (!g.handle && g.aabb.valid)
        {
            out.box_parts.push_back(g.aabb);
            out.box_positions.insert(out.box_positions.end(), g.pos.begin(), g.pos.end());
        }

        else if (g.handle && g.aabb.valid)
        {
            // handle тоже в бокс, иначе рамка кривая
            out.box_parts.push_back(g.aabb);
            out.box_positions.insert(out.box_positions.end(), g.pos.begin(), g.pos.end());
        }
    }

    if (out.box_positions.empty())
        out.box_positions = out.body_positions;

    if (!out.box_positions.empty())
    {
        BoundsFromPos(out.box_positions, out.box_min, out.box_max);
    }

    else
    {
        for (int i = 0; i < 3; ++i)
        {
            out.box_min[i] = out.body_min[i];
            out.box_max[i] = out.body_max[i];
        }
    }

    std::vector<float> framePos = out.box_positions;
    if (framePos.empty())
    {
        for (const auto& v : out.vertices)
        {
            framePos.push_back(v.x);
            framePos.push_back(v.y);
            framePos.push_back(v.z);
        }
    }
    if (!framePos.empty())
        BoundsFromPos(framePos, out.raw_min, out.raw_max);

    else if (!out.positions.empty())
        BoundsFromPos(out.positions, out.raw_min, out.raw_max);

    out.center[0] = (out.raw_min[0] + out.raw_max[0]) * 0.5f;
    out.center[1] = (out.raw_min[1] + out.raw_max[1]) * 0.5f;
    out.center[2] = (out.raw_min[2] + out.raw_max[2]) * 0.5f;

    float ext0 = out.raw_max[0] - out.raw_min[0];
    float ext1 = out.raw_max[1] - out.raw_min[1];
    float ext2 = out.raw_max[2] - out.raw_min[2];
    float maxExt = ext0;
    if (ext1 > maxExt) maxExt = ext1;
    if (ext2 > maxExt) maxExt = ext2;

    out.scale = 1.0f;
    if (maxExt > 0.0f)
        out.scale = 1.0f / maxExt;
}

bool ParseOBJStream(std::istream& stream, LoadedModel& out)
{
	out = {};
	std::vector<float> uvs;
	std::vector<float> norms;
	std::vector<GroupAccum> groups;
	GroupAccum* cur = nullptr;
	std::string line;

	auto beginGroup = [&](const std::string& name)
	{
		groups.push_back({});
		cur = &groups.back();
		cur->name = name;
		cur->handle = IsHandleGroup(name);
	};

	beginGroup("default");

	while (std::getline(stream, line))
	{
		if (line.empty() || line[0] == '#')
			continue;
		const char* s = line.c_str();

		if (s[0] == 'g' && (s[1] == ' ' || s[1] == '\t'))
		{
			std::string group = line.substr(2);
			while (!group.empty() && (group.back() == '\r' || group.back() == ' '))
				group.pop_back();
			beginGroup(group);
			continue;
		}

		if (s[0] == 'v' && s[1] == ' ')
		{
			float x, y, z;
			if (sscanf_s(s + 2, "%f %f %f", &x, &y, &z) == 3)
			{
				out.positions.push_back(x);
				out.positions.push_back(y);
				out.positions.push_back(z);
				if (cur)
				{
					cur->pos.push_back(x);
					cur->pos.push_back(y);
					cur->pos.push_back(z);
					ExpandAABB(cur->aabb, x, y, z);
				}
			}
		}

		else if (s[0] == 'v' && s[1] == 't')
		{
			float u, v;
			if (sscanf_s(s + 3, "%f %f", &u, &v) == 2)
			{
				uvs.push_back(u);
				uvs.push_back(v);
			}
		}

		else if (s[0] == 'v' && s[1] == 'n')
		{
			float nx, ny, nz;
			if (sscanf_s(s + 3, "%f %f %f", &nx, &ny, &nz) == 3)
			{
				norms.push_back(nx);
				norms.push_back(ny);
				norms.push_back(nz);
			}
		}

		else if (s[0] == 'f' && s[1] == ' ')
		{
            struct Idx { int v, vt, vn; };
            Idx idx[8];
            int count = 0;
            const char* p = s + 2;
            while (*p && count < 8) {
                while (*p == ' ') ++p;
                if (!*p) break;
                int vi = 0, vti = 0, vni = 0;
                while (*p >= '0' && *p <= '9') vi = vi * 10 + (*p++ - '0');
                if (*p == '/') {
                    ++p;
                    if (*p != '/') {
                        while (*p >= '0' && *p <= '9') vti = vti * 10 + (*p++ - '0');
                    }
                    if (*p == '/') {
                        ++p;
                        while (*p >= '0' && *p <= '9') vni = vni * 10 + (*p++ - '0');
                    }
                }
                if (vi > 0)
                    idx[count++] = { vi - 1, vti - 1, vni - 1 };

                else
                    break;
            }

            const bool bodyFace = cur && !cur->handle;
            auto emit = [&](Idx a, Idx b, Idx c) {
                auto make = [&](Idx i) {
                    ModelVertex mv{};
                    mv.nx = 0.0f; mv.ny = 1.0f; mv.nz = 0.0f;
                    if (i.v >= 0 && i.v * 3 + 2 < (int)out.positions.size()) {
                        mv.x = out.positions[i.v * 3 + 0];
                        mv.y = out.positions[i.v * 3 + 1];
                        mv.z = out.positions[i.v * 3 + 2];
                    }
                    if (i.vn >= 0 && i.vn * 3 + 2 < (int)norms.size()) {
                        mv.nx = norms[i.vn * 3 + 0];
                        mv.ny = norms[i.vn * 3 + 1];
                        mv.nz = norms[i.vn * 3 + 2];
                    }
                    if (i.vt >= 0 && i.vt * 2 + 1 < (int)uvs.size()) {
                        mv.u = uvs[i.vt * 2 + 0];
                        mv.v = 1.0f - uvs[i.vt * 2 + 1]; // obj v вверх ногами
                    }
                    return mv;
                };
                if (cur) {
                    cur->mesh.push_back(make(a));
                    cur->mesh.push_back(make(b));
                    cur->mesh.push_back(make(c));
                }
                if (a.v >= 0 && b.v >= 0 && c.v >= 0) {
                    ModelTri t{ a.v, b.v, c.v };
                    out.tris.push_back(t);
                    if (bodyFace) out.body_tris.push_back(t);
                }
            };

			if (count == 3)
				emit(idx[0], idx[1], idx[2]);

			else if (count == 4)
			{
				emit(idx[0], idx[1], idx[2]);
				emit(idx[0], idx[2], idx[3]);
			}

			else if (count > 4)
			{
				for (int i = 1; i + 1 < count; ++i)
					emit(idx[0], idx[i], idx[i + 1]);
			}
		}
	}

	FinalizeModel(out, groups);
	return !out.vertices.empty() || !out.wing_vertices.empty();
}

}

bool LoadOBJ(const std::string& path, LoadedModel& out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    return ParseOBJStream(file, out);
}

bool LoadOBJFromMemory(const char* src, std::size_t len, LoadedModel& out)
{
    if (!src || len == 0) return false;
    std::string buf(src, src + len);
    std::istringstream stream(buf);
    return ParseOBJStream(stream, out);
}

// ── GLB (binary glTF 2.0) loader ────────────────────────────────────────────
namespace {

struct GLBAccessor {
    int bufferView = -1;
    int byteOffset = 0;
    int componentType = 0;
    int count = 0;
    std::string type;
};

struct GLBBufferView {
    int buffer = 0;
    int byteOffset = 0;
    int byteLength = 0;
    int byteStride = 0;
};

bool GLBReadScalar(const unsigned char* base, int byteOffset,
                   int componentType, int index, unsigned int& out)
{
    switch (componentType) {
    case 5121: out = base[byteOffset + index]; return true;
    case 5123: { unsigned short v; memcpy(&v, base + byteOffset + index * 2, 2); out = v; return true; }
    case 5125: { memcpy(&out, base + byteOffset + index * 4, 4); return true; }
    default: return false;
    }
}

bool GLBReadVec3(const unsigned char* base, int byteOffset,
                 int componentType, int stride, int index, float out[3])
{
    const unsigned char* p = base + byteOffset + index * stride;
    if (componentType == 5126) {
        memcpy(out, p, 12);
        return true;
    }
    return false;
}

bool GLBReadVec2(const unsigned char* base, int byteOffset,
                 int componentType, int stride, int index, float out[2])
{
    const unsigned char* p = base + byteOffset + index * stride;
    if (componentType == 5126) {
        memcpy(out, p, 8);
        return true;
    }
    return false;
}

bool ParseGLB(const std::vector<unsigned char>& fileData,
              LoadedModel& out, GLBTexture* embedded_tex)
{
    if (fileData.size() < 12) return false;

    unsigned int magic, version, length;
    memcpy(&magic, fileData.data(), 4);
    memcpy(&version, fileData.data() + 4, 4);
    memcpy(&length, fileData.data() + 8, 4);
    if (magic != 0x46546C67 || version != 2) return false;

    std::string jsonChunk;
    const unsigned char* binChunk = nullptr;
    unsigned int binLen = 0;

    unsigned int offset = 12;
    while (offset + 8 <= fileData.size()) {
        unsigned int chunkLen, chunkType;
        memcpy(&chunkLen, fileData.data() + offset, 4);
        memcpy(&chunkType, fileData.data() + offset + 4, 4);
        offset += 8;
        if (offset + chunkLen > fileData.size()) return false;
        if (chunkType == 0x4E4F534A) {
            jsonChunk.assign(reinterpret_cast<const char*>(fileData.data() + offset), chunkLen);
        } else if (chunkType == 0x004E4942) {
            binChunk = fileData.data() + offset;
            binLen = chunkLen;
        }
        offset += chunkLen;
    }

    if (jsonChunk.empty() || !binChunk) return false;

    nlohmann::json root;
    try { root = nlohmann::json::parse(jsonChunk); }
    catch (...) { return false; }

    std::vector<GLBBufferView> bufferViews;
    if (root.contains("bufferViews")) {
        for (auto& bv : root["bufferViews"]) {
            GLBBufferView v;
            v.buffer = bv.value("buffer", 0);
            v.byteOffset = bv.value("byteOffset", 0);
            v.byteLength = bv.value("byteLength", 0);
            v.byteStride = bv.value("byteStride", 0);
            bufferViews.push_back(v);
        }
    }

    std::vector<GLBAccessor> accessors;
    if (root.contains("accessors")) {
        for (auto& a : root["accessors"]) {
            GLBAccessor acc;
            acc.bufferView = a.value("bufferView", -1);
            acc.byteOffset = a.value("byteOffset", 0);
            acc.componentType = a.value("componentType", 0);
            acc.count = a.value("count", 0);
            acc.type = a.value("type", "");
            accessors.push_back(acc);
        }
    }

    auto getBufferData = [&](const GLBAccessor& acc) -> const unsigned char* {
        if (acc.bufferView < 0 || acc.bufferView >= (int)bufferViews.size()) return nullptr;
        auto& bv = bufferViews[acc.bufferView];
        if (bv.buffer != 0) return nullptr;
        if ((unsigned int)(bv.byteOffset + acc.byteOffset) >= binLen) return nullptr;
        return binChunk + bv.byteOffset + acc.byteOffset;
    };

    auto getStride = [&](const GLBAccessor& acc) -> int {
        if (acc.bufferView >= 0 && acc.bufferView < (int)bufferViews.size()) {
            int s = bufferViews[acc.bufferView].byteStride;
            if (s > 0) return s;
        }
        if (acc.type == "SCALAR") return 4;
        if (acc.type == "VEC2") return 8;
        if (acc.type == "VEC3") return 12;
        if (acc.type == "VEC4") return 16;
        return 0;
    };

    std::vector<ModelPartAABB> groupParts;

    if (!root.contains("meshes")) return false;

    // Pre-extract every embedded image's raw bytes once, keyed by image index,
    // so per-submesh textures can be resolved below.
    std::vector<std::vector<unsigned char>> imageBytes;
    if (root.contains("images"))
    {
        for (auto& img : root["images"])
        {
            std::vector<unsigned char> bytes;
            int bvIdx = img.value("bufferView", -1);
            if (bvIdx >= 0 && bvIdx < (int)bufferViews.size())
            {
                auto& bv = bufferViews[bvIdx];
                if (bv.buffer == 0 && bv.byteLength > 0 &&
                    (unsigned int)(bv.byteOffset + bv.byteLength) <= binLen)
                {
                    bytes.assign(binChunk + bv.byteOffset,
                                 binChunk + bv.byteOffset + bv.byteLength);
                }
            }
            imageBytes.push_back(std::move(bytes));
        }
    }

    // texture index -> image index via the glTF "textures" array (fall back to
    // using the texture index directly, which some exporters use as an image id).
    auto imageForTex = [&](int texIdx) -> int {
        if (texIdx < 0) return -1;
        if (root.contains("textures") && texIdx < (int)root["textures"].size())
            return root["textures"][texIdx].value("source", -1);
        return texIdx;
    };

    for (auto& mesh : root["meshes"]) {
        if (!mesh.contains("primitives")) continue;

        const unsigned int subStart = (unsigned int)out.vertices.size();
        int meshTexImage = -1;
        if (!mesh["primitives"].empty())
        {
            const auto& prim0 = mesh["primitives"][0];
            int matIdx = prim0.value("material", -1);
            if (matIdx >= 0 && root.contains("materials") &&
                matIdx < (int)root["materials"].size())
            {
                const auto& mat = root["materials"][matIdx];
                const auto& pbr = mat.contains("pbrMetallicRoughness")
                    ? mat["pbrMetallicRoughness"] : nlohmann::json();
                if (pbr.contains("baseColorTexture"))
                    meshTexImage = imageForTex(pbr["baseColorTexture"].value("index", -1));
                else if (mat.contains("baseColorTexture"))
                    meshTexImage = imageForTex(mat["baseColorTexture"].value("index", -1));
            }
        }

        for (auto& prim : mesh["primitives"]) {
            if (!prim.contains("attributes")) continue;
            auto& attrs = prim["attributes"];

            int posIdx = attrs.value("POSITION", -1);
            int normIdx = attrs.value("NORMAL", -1);
            int uvIdx = attrs.value("TEXCOORD_0", -1);
            int idxIdx = prim.value("indices", -1);

            if (posIdx < 0 || posIdx >= (int)accessors.size()) continue;

            auto& posAcc = accessors[posIdx];
            const unsigned char* posData = getBufferData(posAcc);
            int posStride = getStride(posAcc);
            if (!posData || posAcc.componentType != 5126 || posAcc.type != "VEC3") continue;

            const unsigned char* normData = nullptr;
            int normStride = 0;
            if (normIdx >= 0 && normIdx < (int)accessors.size()) {
                normData = getBufferData(accessors[normIdx]);
                normStride = getStride(accessors[normIdx]);
            }

            const unsigned char* uvData = nullptr;
            int uvStride = 0;
            if (uvIdx >= 0 && uvIdx < (int)accessors.size()) {
                uvData = getBufferData(accessors[uvIdx]);
                uvStride = getStride(accessors[uvIdx]);
            }

            ModelPartAABB partAABB{};
            for (int i = 0; i < posAcc.count; ++i) {
                float p[3];
                GLBReadVec3(posData, 0, posAcc.componentType, posStride, i, p);
                ExpandAABB(partAABB, p[0], p[1], p[2]);
            }

            if (idxIdx >= 0 && idxIdx < (int)accessors.size()) {
                auto& idxAcc = accessors[idxIdx];
                const unsigned char* idxData = getBufferData(idxAcc);
                if (idxData && (idxAcc.type == "SCALAR")) {
                    std::vector<unsigned int> indices(idxAcc.count);
                    for (int i = 0; i < idxAcc.count; ++i)
                        GLBReadScalar(idxData, 0, idxAcc.componentType, i, indices[i]);

                    for (int i = 0; i + 2 < idxAcc.count; i += 3) {
                        for (int j = 0; j < 3; ++j) {
                            unsigned int vi = indices[i + j];
                            ModelVertex mv{};
                            mv.nx = 0.0f; mv.ny = 1.0f; mv.nz = 0.0f;
                            if ((int)vi < posAcc.count) {
                                float p[3];
                                GLBReadVec3(posData, 0, posAcc.componentType, posStride, vi, p);
                                mv.x = p[0]; mv.y = p[1]; mv.z = p[2];
                            }
                            if (normData && normStride > 0 && (int)vi < accessors[normIdx].count) {
                                float n[3];
                                GLBReadVec3(normData, 0, accessors[normIdx].componentType, normStride, vi, n);
                                mv.nx = n[0]; mv.ny = n[1]; mv.nz = n[2];
                            }
                            if (uvData && uvStride > 0 && (int)vi < accessors[uvIdx].count) {
                                float uv[2];
                                GLBReadVec2(uvData, 0, accessors[uvIdx].componentType, uvStride, vi, uv);
                                mv.u = uv[0]; mv.v = uv[1];
                            }
                            out.vertices.push_back(mv);
                            out.positions.push_back(mv.x);
                            out.positions.push_back(mv.y);
                            out.positions.push_back(mv.z);
                            out.body_positions.push_back(mv.x);
                            out.body_positions.push_back(mv.y);
                            out.body_positions.push_back(mv.z);

                            ModelTri t;
                            t.i0 = (int)out.tris.size();
                            out.tris.push_back(t);
                        }
                    }
                }
            } else {
                for (int i = 0; i < posAcc.count; ++i) {
                    ModelVertex mv{};
                    mv.nx = 0.0f; mv.ny = 1.0f; mv.nz = 0.0f;
                    float p[3];
                    GLBReadVec3(posData, 0, posAcc.componentType, posStride, i, p);
                    mv.x = p[0]; mv.y = p[1]; mv.z = p[2];
                    if (normData && normStride > 0 && i < accessors[normIdx].count) {
                        float n[3];
                        GLBReadVec3(normData, 0, accessors[normIdx].componentType, normStride, i, n);
                        mv.nx = n[0]; mv.ny = n[1]; mv.nz = n[2];
                    }
                    if (uvData && uvStride > 0 && i < accessors[uvIdx].count) {
                        float uv[2];
                        GLBReadVec2(uvData, 0, accessors[uvIdx].componentType, uvStride, i, uv);
                        mv.u = uv[0]; mv.v = uv[1];
                    }
                    out.vertices.push_back(mv);
                    out.positions.push_back(mv.x);
                    out.positions.push_back(mv.y);
                    out.positions.push_back(mv.z);
                }
            }

            if (partAABB.valid)
                groupParts.push_back(partAABB);
        }

        // Record a drawable slice for this mesh with its resolved texture.
        const unsigned int subCount = (unsigned int)out.vertices.size() - subStart;
        if (subStart < out.vertices.size() && subCount > 0)
        {
            LoadedSubMesh sm;
            sm.base_vertex = subStart;
            sm.vertex_count = subCount;
            if (meshTexImage >= 0 && meshTexImage < (int)imageBytes.size())
                sm.texture = imageBytes[meshTexImage];
            out.submeshes.push_back(std::move(sm));
        }
    }

    if (out.vertices.empty()) return false;

    // Log vertex extents for orientation diagnosis
    {
        float mn[3] = { FLT_MAX, FLT_MAX, FLT_MAX };
        float mx[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
        for (const auto& v : out.vertices) {
            if (v.x < mn[0]) mn[0] = v.x; if (v.x > mx[0]) mx[0] = v.x;
            if (v.y < mn[1]) mn[1] = v.y; if (v.y > mx[1]) mx[1] = v.y;
            if (v.z < mn[2]) mn[2] = v.z; if (v.z > mx[2]) mx[2] = v.z;
        }
        char tmpPath[MAX_PATH];
        GetTempPathA(MAX_PATH, tmpPath);
        strcat_s(tmpPath, "p3d_diag.log");
        FILE* f = nullptr;
        fopen_s(&f, tmpPath, "a");
        if (f) {
            fprintf(f, "[GLB] verts=%zu X[%.3f..%.3f] Y[%.3f..%.3f] Z[%.3f..%.3f] extX=%.3f extY=%.3f extZ=%.3f\n",
                out.vertices.size(),
                mn[0], mx[0], mn[1], mx[1], mn[2], mx[2],
                mx[0]-mn[0], mx[1]-mn[1], mx[2]-mn[2]);
            fclose(f);
        }
    }

    out.body_parts = groupParts;
    out.box_parts = groupParts;

    if (!out.body_positions.empty())
        BoundsFromPos(out.body_positions, out.body_min, out.body_max);
    else if (!out.positions.empty())
        BoundsFromPos(out.positions, out.body_min, out.body_max);

    float bodyW = out.body_max[0] - out.body_min[0];
    float bodyH = out.body_max[1] - out.body_min[1];
    if (bodyW < 0.01f) bodyW = 0.01f;
    if (bodyH < 0.01f) bodyH = 0.01f;

    if (out.box_positions.empty())
        out.box_positions = out.body_positions;
    if (!out.box_positions.empty())
        BoundsFromPos(out.box_positions, out.box_min, out.box_max);
    else {
        for (int i = 0; i < 3; ++i) {
            out.box_min[i] = out.body_min[i];
            out.box_max[i] = out.body_max[i];
        }
    }

    std::vector<float> framePos = out.box_positions;
    if (framePos.empty()) {
        for (const auto& v : out.vertices) {
            framePos.push_back(v.x);
            framePos.push_back(v.y);
            framePos.push_back(v.z);
        }
    }
    if (!framePos.empty())
        BoundsFromPos(framePos, out.raw_min, out.raw_max);
    else if (!out.positions.empty())
        BoundsFromPos(out.positions, out.raw_min, out.raw_max);

    out.center[0] = (out.raw_min[0] + out.raw_max[0]) * 0.5f;
    out.center[1] = (out.raw_min[1] + out.raw_max[1]) * 0.5f;
    out.center[2] = (out.raw_min[2] + out.raw_max[2]) * 0.5f;

    float ext0 = out.raw_max[0] - out.raw_min[0];
    float ext1 = out.raw_max[1] - out.raw_min[1];
    float ext2 = out.raw_max[2] - out.raw_min[2];
    float maxExt = ext0;
    if (ext1 > maxExt) maxExt = ext1;
    if (ext2 > maxExt) maxExt = ext2;
    out.scale = maxExt > 0.0f ? (1.0f / maxExt) : 1.0f;

    if (embedded_tex && root.contains("images")) {
        for (auto& img : root["images"]) {
            int bvIdx = img.value("bufferView", -1);
            if (bvIdx < 0 || bvIdx >= (int)bufferViews.size()) continue;
            auto& bv = bufferViews[bvIdx];
            if (bv.buffer != 0 || bv.byteLength <= 0) continue;
            if ((unsigned int)(bv.byteOffset + bv.byteLength) > binLen) continue;
            embedded_tex->data.assign(binChunk + bv.byteOffset,
                                      binChunk + bv.byteOffset + bv.byteLength);
            break;
        }
    }

    return true;
}

}

bool LoadGLB(const std::string& path, LoadedModel& out, GLBTexture* embedded_tex)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;

    std::streamsize sz = file.tellg();
    if (sz < 12) return false;

    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> data((std::size_t)sz);
    if (!file.read(reinterpret_cast<char*>(data.data()), sz)) return false;

    return ParseGLB(data, out, embedded_tex);
}

}
