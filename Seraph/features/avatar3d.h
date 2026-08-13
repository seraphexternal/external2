#pragma once

#include "stb_image.h"

#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include "../rbx/configs/json.hpp"
#include "../overlay/imgui/imgui.h"

extern ID3D11Device* g_pd3dDevice;

namespace Avatar3D
{
    struct Vec3 { float x = 0, y = 0, z = 0; };

    struct Material {
        float diffuse[3] = { 0.75f, 0.75f, 0.75f };
    };

    struct Face {
        std::vector<int> verts;
        int matIdx = 0;
        float normal[3] = { 0, 1, 0 };
    };

    struct Model {
        std::vector<Vec3> vertices;
        std::vector<Face> faces;
        std::vector<Material> materials;
        Vec3 aabbMin = {}, aabbMax = {};
        bool valid = false;
    };

    struct CacheEntry {
        Model model;
        bool loaded = false;
        bool loading = false;
        std::chrono::steady_clock::time_point lastAttempt;
    };

    static std::unordered_map<uint64_t, CacheEntry> s_Cache;
    static std::mutex s_CacheMutex;

    inline std::string GetCDNUrl(const std::string& hash)
    {
        if (hash.length() < 10) return "";
        int i = 31;
        for (int t = 0; t < 40 && t < (int)hash.length(); t++)
            i ^= (unsigned char)hash[t];
        return "https://t" + std::to_string(i % 8) + ".rbxcdn.com/" + hash;
    }

    inline std::string HttpGet(const std::string& url)
    {
        std::string response;
        HINTERNET hIn = InternetOpenA("Seraph3D", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
        if (!hIn) return response;
        HINTERNET hUrl = InternetOpenUrlA(hIn, url.c_str(), NULL, 0,
            INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI, 0);
        if (hUrl)
        {
            char buf[4096];
            DWORD got = 0;
            while (InternetReadFile(hUrl, buf, sizeof(buf) - 1, &got) && got > 0)
            {
                buf[got] = '\0';
                response.append(buf, got);
            }
            InternetCloseHandle(hUrl);
        }
        InternetCloseHandle(hIn);
        return response;
    }

    inline std::vector<unsigned char> HttpGetBinary(const std::string& url)
    {
        std::vector<unsigned char> data;
        HINTERNET hIn = InternetOpenA("Seraph3D", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
        if (!hIn) return data;
        HINTERNET hUrl = InternetOpenUrlA(hIn, url.c_str(), NULL, 0,
            INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI | INTERNET_FLAG_NEED_FILE, 0);
        if (hUrl)
        {
            char buf[8192];
            DWORD got = 0;
            while (InternetReadFile(hUrl, buf, sizeof(buf), &got) && got > 0)
                data.insert(data.end(), buf, buf + got);
            InternetCloseHandle(hUrl);
        }
        InternetCloseHandle(hIn);
        return data;
    }

    inline std::vector<unsigned char> DecompressGzip(const std::vector<unsigned char>& data)
    {
        if (data.size() < 2 || data[0] != 0x1F || data[1] != 0x8B)
            return data;
        int outLen = 0;
        char* out = stbi_zlib_decode_malloc((const char*)data.data(), (int)data.size(), &outLen);
        if (out && outLen > 0)
        {
            std::vector<unsigned char> result(out, out + outLen);
            free(out);
            return result;
        }
        if (out) free(out);
        return {};
    }

    inline std::string CleanData(const std::vector<unsigned char>& data)
    {
        std::string s;
        s.reserve(data.size());
        for (unsigned char c : data)
        {
            if (c == 0) break;
            if (c >= 32 || c == '\n' || c == '\r' || c == '\t')
                s += (char)c;
        }
        return s;
    }

    inline void Trim(std::string& s)
    {
        s.erase(0, s.find_first_not_of(" \t\r\n"));
        s.erase(s.find_last_not_of(" \t\r\n") + 1);
    }

    // ==================== OBJ PARSER ====================

    inline bool ParseOBJ(const std::vector<unsigned char>& objData, Model& model)
    {
        auto decompressed = DecompressGzip(objData);
        std::string content = CleanData(decompressed.empty() ? objData : decompressed);
        if (content.size() < 10 || content.find("v ") == std::string::npos)
            return false;

        model.vertices.clear();
        model.faces.clear();
        model.materials.clear();
        model.valid = false;
        model.vertices.reserve(5000);
        model.faces.reserve(3000);

        int curMat = 0;
        size_t pos = 0, len = content.size();

        while (pos < len)
        {
            size_t eol = content.find('\n', pos);
            if (eol == std::string::npos) eol = len;
            std::string line = content.substr(pos, eol - pos);
            pos = eol + 1;
            Trim(line);
            if (line.empty() || line[0] == '#' || line.size() < 2) continue;

            if (line.compare(0, 7, "usemtl ") == 0)
            {
                std::string mname = line.substr(7);
                Trim(mname);
                bool found = false;
                for (int i = 0; i < (int)model.materials.size(); i++)
                {
                    (void)i;
                    (void)mname;
                    (void)found;
                }
                curMat = (int)model.materials.size();
                Material m;
                model.materials.push_back(m);
                continue;
            }

            size_t sp = line.find(' ');
            if (sp == std::string::npos) continue;
            std::string prefix = line.substr(0, sp);
            std::string data = line.substr(sp + 1);
            Trim(data);

            if (prefix == "v")
            {
                Vec3 v;
                const char* s = data.c_str();
                char* e;
                v.x = strtof(s, &e);
                if (e == s) continue;
                v.y = strtof(e, &e);
                v.z = strtof(e, &e);
                model.vertices.push_back(v);
            }
            else if (prefix == "f")
            {
                Face f;
                f.matIdx = curMat;
                const char* s = data.c_str();
                while (*s)
                {
                    while (*s == ' ') s++;
                    if (!*s) break;
                    char* e;
                    int idx = strtol(s, &e, 10);
                    if (e == s) break;
                    if (idx > 0 && idx <= (int)model.vertices.size())
                        f.verts.push_back(idx - 1);
                    else if (idx < 0 && -idx <= (int)model.vertices.size())
                        f.verts.push_back((int)model.vertices.size() + idx);
                    s = e;
                    while (*s && *s != ' ') s++;
                }
                if (f.verts.size() >= 3)
                {
                    Vec3& v0 = model.vertices[f.verts[0]];
                    Vec3& v1 = model.vertices[f.verts[1]];
                    Vec3& v2 = model.vertices[f.verts[2]];
                    float ax = v1.x - v0.x, ay = v1.y - v0.y, az = v1.z - v0.z;
                    float bx = v2.x - v0.x, by = v2.y - v0.y, bz = v2.z - v0.z;
                    float nx = ay * bz - az * by;
                    float ny = az * bx - ax * bz;
                    float nz = ax * by - ay * bx;
                    float nl = sqrtf(nx * nx + ny * ny + nz * nz);
                    if (nl > 0.0001f) { f.normal[0] = nx / nl; f.normal[1] = ny / nl; f.normal[2] = nz / nl; }
                    model.faces.push_back(f);
                }
            }
        }

        if (model.vertices.empty() || model.faces.empty()) return false;

        model.aabbMin = model.aabbMax = model.vertices[0];
        for (auto& v : model.vertices)
        {
            if (v.x < model.aabbMin.x) model.aabbMin.x = v.x;
            if (v.y < model.aabbMin.y) model.aabbMin.y = v.y;
            if (v.z < model.aabbMin.z) model.aabbMin.z = v.z;
            if (v.x > model.aabbMax.x) model.aabbMax.x = v.x;
            if (v.y > model.aabbMax.y) model.aabbMax.y = v.y;
            if (v.z > model.aabbMax.z) model.aabbMax.z = v.z;
        }
        model.valid = true;
        return true;
    }

    // ==================== MTL PARSER ====================

    inline void ParseMTL(const std::vector<unsigned char>& mtlData, Model& model)
    {
        auto decompressed = DecompressGzip(mtlData);
        std::string content = CleanData(decompressed.empty() ? mtlData : decompressed);
        if (content.size() < 5) return;

        std::string curName;
        size_t pos = 0, len = content.size();
        while (pos < len)
        {
            size_t eol = content.find('\n', pos);
            if (eol == std::string::npos) eol = len;
            std::string line = content.substr(pos, eol - pos);
            pos = eol + 1;
            Trim(line);
            if (line.empty() || line[0] == '#' || line.size() < 3) continue;

            size_t sp = line.find(' ');
            if (sp == std::string::npos) continue;
            std::string type = line.substr(0, sp);
            std::string data = line.substr(sp + 1);
            Trim(data);

            if (type == "newmtl")
            {
                curName = data;
                Material m;
                model.materials.push_back(m);
            }
            else if (type == "Kd" && !model.materials.empty())
            {
                Material& mat = model.materials.back();
                const char* s = data.c_str();
                char* e;
                mat.diffuse[0] = fmaxf(0.f, fminf(1.f, strtof(s, &e)));
                mat.diffuse[1] = fmaxf(0.f, fminf(1.f, strtof(e, &e)));
                mat.diffuse[2] = fmaxf(0.f, fminf(1.f, strtof(e, &e)));
            }
        }
    }

    // ==================== 3D FETCH ====================

    inline bool FetchAndParse(uint64_t userId, Model& outModel)
    {
        std::string apiUrl = "https://thumbnails.roblox.com/v1/users/avatar-3d?userIds=" + std::to_string(userId);
        std::string resp = HttpGet(apiUrl);
        if (resp.empty()) return false;

        nlohmann::json apiJson;
        try { apiJson = nlohmann::json::parse(resp); }
        catch (...) { return false; }

        if (apiJson.contains("data") && apiJson["data"].is_array() && !apiJson["data"].empty())
            apiJson = apiJson["data"][0];

        if (!apiJson.contains("imageUrl") || apiJson.value("state", "") != "Completed")
            return false;

        std::string modelUrl = apiJson["imageUrl"].get<std::string>();

        std::string modelResp = HttpGet(modelUrl);
        if (modelResp.empty()) return false;

        nlohmann::json modelJson;
        try { modelJson = nlohmann::json::parse(modelResp); }
        catch (...) { return false; }

        if (modelJson.contains("data") && modelJson["data"].is_array() && !modelJson["data"].empty())
            modelJson = modelJson["data"][0];

        std::string objHash = modelJson.value("obj", "");
        std::string mtlHash = modelJson.value("mtl", "");
        if (objHash.empty()) return false;

        std::vector<unsigned char> objData = HttpGetBinary(GetCDNUrl(objHash));
        if (objData.empty()) return false;

        if (!ParseOBJ(objData, outModel)) return false;

        if (!mtlHash.empty())
        {
            std::vector<unsigned char> mtlData = HttpGetBinary(GetCDNUrl(mtlHash));
            if (!mtlData.empty())
                ParseMTL(mtlData, outModel);
        }

        // If MTL gave us materials, make sure every face has a valid material
        if (outModel.materials.empty())
        {
            Material m;
            m.diffuse[0] = m.diffuse[1] = m.diffuse[2] = 0.75f;
            outModel.materials.push_back(m);
        }
        for (auto& f : outModel.faces)
        {
            if (f.matIdx < 0 || f.matIdx >= (int)outModel.materials.size())
                f.matIdx = 0;
        }

        return true;
    }

    // ==================== DEFAULT (FALLBACK) HUMANOID ====================

    inline void AddBox(Model& m, Vec3 c, Vec3 sz, int matIdx)
    {
        float hx = sz.x * 0.5f, hy = sz.y * 0.5f, hz = sz.z * 0.5f;
        int base = (int)m.vertices.size();
        m.vertices.push_back({c.x - hx, c.y - hy, c.z - hz});
        m.vertices.push_back({c.x + hx, c.y - hy, c.z - hz});
        m.vertices.push_back({c.x - hx, c.y + hy, c.z - hz});
        m.vertices.push_back({c.x + hx, c.y + hy, c.z - hz});
        m.vertices.push_back({c.x - hx, c.y - hy, c.z + hz});
        m.vertices.push_back({c.x + hx, c.y - hy, c.z + hz});
        m.vertices.push_back({c.x - hx, c.y + hy, c.z + hz});
        m.vertices.push_back({c.x + hx, c.y + hy, c.z + hz});
        static const int quads[6][4] = {
            {2,3,1,0},{4,5,7,6},{1,5,4,0},{2,6,7,3},{0,4,6,2},{1,3,7,5}
        };
        for (int q = 0; q < 6; q++)
        {
            Face f; f.matIdx = matIdx;
            f.verts = {base + quads[q][0], base + quads[q][1],
                       base + quads[q][2], base + quads[q][3]};
            Vec3& v0 = m.vertices[f.verts[0]];
            Vec3& v1 = m.vertices[f.verts[1]];
            Vec3& v2 = m.vertices[f.verts[2]];
            float ax = v1.x - v0.x, ay = v1.y - v0.y, az = v1.z - v0.z;
            float bx = v2.x - v0.x, by = v2.y - v0.y, bz = v2.z - v0.z;
            float nx = ay * bz - az * by;
            float ny = az * bx - ax * bz;
            float nz = ax * by - ay * bx;
            float nl = sqrtf(nx * nx + ny * ny + nz * nz);
            if (nl > 0.0001f) { f.normal[0] = nx/nl; f.normal[1] = ny/nl; f.normal[2] = nz/nl; }
            m.faces.push_back(f);
        }
    }

    inline Model CreateDefaultModel()
    {
        Model m;
        float skin[3] = {0.90f, 0.72f, 0.58f};
        float shirt[3] = {0.20f, 0.40f, 0.80f};
        float pants[3] = {0.15f, 0.15f, 0.35f};
        float shoes[3] = {0.10f, 0.10f, 0.10f};
        m.materials.push_back({skin[0],skin[1],skin[2]});
        m.materials.push_back({shirt[0],shirt[1],shirt[2]});
        m.materials.push_back({pants[0],pants[1],pants[2]});
        m.materials.push_back({shoes[0],shoes[1],shoes[2]});
        AddBox(m, {0,1.70f,0}, {0.30f,0.30f,0.30f}, 0);
        AddBox(m, {0,1.00f,0}, {0.50f,0.55f,0.25f}, 1);
        AddBox(m, {-0.45f,1.15f,0}, {0.14f,0.30f,0.14f}, 0);
        AddBox(m, {0.45f,1.15f,0}, {0.14f,0.30f,0.14f}, 0);
        AddBox(m, {-0.45f,0.78f,0}, {0.12f,0.28f,0.12f}, 1);
        AddBox(m, {0.45f,0.78f,0}, {0.12f,0.28f,0.12f}, 1);
        AddBox(m, {-0.14f,0.30f,0}, {0.16f,0.40f,0.16f}, 2);
        AddBox(m, {0.14f,0.30f,0}, {0.16f,0.40f,0.16f}, 2);
        AddBox(m, {-0.14f,-0.10f,0}, {0.14f,0.30f,0.14f}, 3);
        AddBox(m, {0.14f,-0.10f,0}, {0.14f,0.30f,0.14f}, 3);
        if (m.vertices.empty()) return m;
        m.aabbMin = m.aabbMax = m.vertices[0];
        for (auto& v : m.vertices) {
            if (v.x<m.aabbMin.x) m.aabbMin.x=v.x; if (v.y<m.aabbMin.y) m.aabbMin.y=v.y; if (v.z<m.aabbMin.z) m.aabbMin.z=v.z;
            if (v.x>m.aabbMax.x) m.aabbMax.x=v.x; if (v.y>m.aabbMax.y) m.aabbMax.y=v.y; if (v.z>m.aabbMax.z) m.aabbMax.z=v.z;
        }
        m.valid = true;
        return m;
    }

    inline const Model& GetDefaultModel()
    {
        static Model defaultModel;
        static bool created = false;
        if (!created) { defaultModel = CreateDefaultModel(); created = true; }
        return defaultModel;
    }

    // ==================== SOFT RASTERIZER ====================

    struct ProjectedVert { float x, y, z; };

    inline ProjectedVert ProjectVertex(const Vec3& v, const Vec3& center, float scale, float cosA, float sinA, float fov, ImVec2 screenCenter)
    {
        float rx = (v.x - center.x) * cosA - (v.z - center.z) * sinA;
        float rz = (v.x - center.x) * sinA + (v.z - center.z) * cosA;
        float ry = v.y - center.y;

        float depth = rz + fov * 2.0f;
        if (depth < 0.1f) depth = 0.1f;
        float perspScale = fov / depth * scale;

        return {
            screenCenter.x + rx * perspScale,
            screenCenter.y - ry * perspScale,
            depth
        };
    }

    inline void RenderModel(ImDrawList* dl, const Model& model, ImVec2 center, ImVec2 size, float angleDeg, const float accentCol[3])
    {
        if (!model.valid || model.faces.empty()) return;

        float cosA = cosf(angleDeg * 3.14159265f / 180.0f);
        float sinA = sinf(angleDeg * 3.14159265f / 180.0f);

        Vec3 mdlCenter;
        mdlCenter.x = (model.aabbMin.x + model.aabbMax.x) * 0.5f;
        mdlCenter.y = (model.aabbMin.y + model.aabbMax.y) * 0.5f;
        mdlCenter.z = (model.aabbMin.z + model.aabbMax.z) * 0.5f;

        float extentX = (model.aabbMax.x - model.aabbMin.x) * 0.5f;
        float extentY = (model.aabbMax.y - model.aabbMin.y) * 0.5f;
        float extentZ = (model.aabbMax.z - model.aabbMin.z) * 0.5f;
        float maxExtent = fmaxf(extentX, fmaxf(extentY, extentZ));
        if (maxExtent < 0.01f) maxExtent = 1.0f;

        float fitSize = fminf(size.x, size.y) * 0.65f;
        float scale = fitSize / maxExtent;
        float fov = 4.5f;

        ImVec2 scr(center.x + size.x * 0.5f, center.y + size.y * 0.5f);

        // Project all vertices
        std::vector<ProjectedVert> projected(model.vertices.size());
        for (size_t i = 0; i < model.vertices.size(); i++)
            projected[i] = ProjectVertex(model.vertices[i], mdlCenter, scale, cosA, sinA, fov, scr);

        // Simple directional light (upper-front-left)
        float lightDir[3] = { -0.3f, 0.6f, -0.7f };
        float ll = sqrtf(lightDir[0] * lightDir[0] + lightDir[1] * lightDir[1] + lightDir[2] * lightDir[2]);
        lightDir[0] /= ll; lightDir[1] /= ll; lightDir[2] /= ll;

        // Build face list with depth for sorting
        struct FaceSort {
            float avgZ;
            int idx;
        };
        std::vector<FaceSort> faceSort(model.faces.size());
        for (size_t i = 0; i < model.faces.size(); i++)
        {
            float z = 0;
            for (int vi : model.faces[i].verts)
                z += projected[vi].z;
            faceSort[i] = { z / (float)model.faces[i].verts.size(), (int)i };
        }
        std::sort(faceSort.begin(), faceSort.end(), [](const FaceSort& a, const FaceSort& b) {
            return a.avgZ > b.avgZ; // far first (painter's)
        });

        // Draw faces
        for (auto& fs : faceSort)
        {
            const Face& face = model.faces[fs.idx];
            if (face.verts.size() < 3) continue;

            // Backface culling
            const ProjectedVert& p0 = projected[face.verts[0]];
            const ProjectedVert& p1 = projected[face.verts[1]];
            const ProjectedVert& p2 = projected[face.verts[2]];
            float e1x = p1.x - p0.x, e1y = p1.y - p0.y;
            float e2x = p2.x - p0.x, e2y = p2.y - p0.y;
            if (e1x * e2y - e1y * e2x > 0) continue; // back-facing

            // Lighting
            float ndotl = face.normal[0] * lightDir[0] + face.normal[1] * lightDir[1] + face.normal[2] * lightDir[2];
            float light = fmaxf(0.15f, ndotl * 0.5f + 0.5f);

            const Material& mat = model.materials[face.matIdx];
            int r = (int)(mat.diffuse[0] * light * 255.f);
            int g = (int)(mat.diffuse[1] * light * 255.f);
            int b = (int)(mat.diffuse[2] * light * 255.f);
            ImU32 col = IM_COL32(r, g, b, 220);

            if (face.verts.size() == 3)
            {
                dl->AddTriangleFilled(
                    ImVec2(p0.x, p0.y), ImVec2(p1.x, p1.y), ImVec2(p2.x, p2.y), col);
            }
            else if (face.verts.size() == 4)
            {
                dl->AddQuadFilled(
                    ImVec2(projected[face.verts[0]].x, projected[face.verts[0]].y),
                    ImVec2(projected[face.verts[1]].x, projected[face.verts[1]].y),
                    ImVec2(projected[face.verts[2]].x, projected[face.verts[2]].y),
                    ImVec2(projected[face.verts[3]].x, projected[face.verts[3]].y), col);
            }
            else
            {
                // Fan from first vertex
                for (int j = 1; j + 1 < (int)face.verts.size(); j++)
                {
                    dl->AddTriangleFilled(
                        ImVec2(projected[face.verts[0]].x, projected[face.verts[0]].y),
                        ImVec2(projected[face.verts[j]].x, projected[face.verts[j]].y),
                        ImVec2(projected[face.verts[j + 1]].x, projected[face.verts[j + 1]].y), col);
                }
            }
        }

        // Wireframe overlay (subtle)
        ImU32 wireCol = IM_COL32(
            (int)(accentCol[0] * 255), (int)(accentCol[1] * 255), (int)(accentCol[2] * 255), 35);
        for (auto& fs : faceSort)
        {
            const Face& face = model.faces[fs.idx];
            if (face.verts.size() < 3) continue;
            for (int j = 0; j < (int)face.verts.size(); j++)
            {
                int a = face.verts[j], b = face.verts[(j + 1) % face.verts.size()];
                dl->AddLine(ImVec2(projected[a].x, projected[a].y),
                    ImVec2(projected[b].x, projected[b].y), wireCol, 0.4f);
            }
        }
    }

    // Get model's projected bounding box (for ESP overlays)
    struct ProjectedBounds {
        float left, right, top, bottom, centerX, centerY;
    };

    inline ProjectedBounds GetProjectedBounds(const Model& model, ImVec2 center, ImVec2 size, float angleDeg)
    {
        if (!model.valid)
            return { center.x, center.x, center.y, center.y, center.x, center.y };

        float cosA = cosf(angleDeg * 3.14159265f / 180.0f);
        float sinA = sinf(angleDeg * 3.14159265f / 180.0f);

        Vec3 mdlCenter;
        mdlCenter.x = (model.aabbMin.x + model.aabbMax.x) * 0.5f;
        mdlCenter.y = (model.aabbMin.y + model.aabbMax.y) * 0.5f;
        mdlCenter.z = (model.aabbMin.z + model.aabbMax.z) * 0.5f;

        float extX = (model.aabbMax.x - model.aabbMin.x) * 0.5f;
        float extY = (model.aabbMax.y - model.aabbMin.y) * 0.5f;
        float extZ = (model.aabbMax.z - model.aabbMin.z) * 0.5f;
        float maxExt = fmaxf(extX, fmaxf(extY, extZ));
        if (maxExt < 0.01f) maxExt = 1.0f;

        // MUST match RenderModel's projection (fitSize/fov) so the ESP overlay
        // bounding box lines up with the rendered avatar — otherwise the skeleton
        // and ESP boxes drift relative to the model on screen.
        float fitSize = fminf(size.x, size.y) * 0.65f;
        float scale = fitSize / maxExt;
        float fov = 4.5f;
        ImVec2 scr(center.x + size.x * 0.5f, center.y + size.y * 0.5f);

        float minSX = 1e9f, maxSX = -1e9f, minSY = 1e9f, maxSY = -1e9f;
        for (auto& v : model.vertices)
        {
            auto p = ProjectVertex(v, mdlCenter, scale, cosA, sinA, fov, scr);
            if (p.x < minSX) minSX = p.x;
            if (p.x > maxSX) maxSX = p.x;
            if (p.y < minSY) minSY = p.y;
            if (p.y > maxSY) maxSY = p.y;
        }
        return { minSX, maxSX, minSY, maxSY, (minSX + maxSX) * 0.5f, (minSY + maxSY) * 0.5f };
    }

    // ==================== PUBLIC API ====================

    inline void RequestModel(uint64_t userId)
    {
        if (userId == 0) return;
        {
            std::lock_guard<std::mutex> lock(s_CacheMutex);
            auto& entry = s_Cache[userId];
            if (entry.loaded || entry.loading) return;
            auto now = std::chrono::steady_clock::now();
            if (entry.lastAttempt.time_since_epoch().count() > 0 &&
                std::chrono::duration_cast<std::chrono::seconds>(now - entry.lastAttempt).count() < 10)
                return;
            entry.loading = true;
            entry.lastAttempt = now;
        }

        std::thread([userId]() {
            Model model;
            bool ok = FetchAndParse(userId, model);
            std::lock_guard<std::mutex> lock(s_CacheMutex);
            auto it = s_Cache.find(userId);
            if (it != s_Cache.end())
            {
                it->second.loading = false;
                it->second.loaded = ok;
                if (ok) it->second.model = model;
            }
        }).detach();
    }

    inline const Model* GetModel(uint64_t userId)
    {
        std::lock_guard<std::mutex> lock(s_CacheMutex);
        auto it = s_Cache.find(userId);
        if (it != s_Cache.end() && it->second.loaded)
            return &it->second.model;
        return nullptr;
    }
}
