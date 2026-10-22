#include "rbxm_parser.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <utility>

namespace RBXM {
namespace {

// ── byte reader ─────────────────────────────────────────────────────────────
struct Reader {
    const uint8_t* d = nullptr;
    size_t n = 0;
    size_t p = 0;
    bool   ok = true;

    bool need(size_t k) { if (p + k > n) { ok = false; return false; } return true; }
    bool skip(size_t k) { if (!need(k)) return false; p += k; return true; }

    uint8_t u8() { if (!need(1)) return 0; return d[p++]; }

    uint16_t u16le() {
        if (!need(2)) return 0;
        uint16_t v = (uint16_t)((uint16_t)d[p] | ((uint16_t)d[p + 1] << 8));
        p += 2; return v;
    }
    uint32_t u32le() {
        if (!need(4)) return 0;
        uint32_t v = (uint32_t)d[p] | ((uint32_t)d[p + 1] << 8) |
                     ((uint32_t)d[p + 2] << 16) | ((uint32_t)d[p + 3] << 24);
        p += 4; return v;
    }
    uint32_t be32() {
        if (!need(4)) return 0;
        uint32_t v = ((uint32_t)d[p] << 24) | ((uint32_t)d[p + 1] << 16) |
                     ((uint32_t)d[p + 2] << 8) | (uint32_t)d[p + 3];
        p += 4; return v;
    }
    float f32le() { uint32_t v = u32le(); float f; memcpy(&f, &v, 4); return f; }
    double f64le() {
        uint64_t v = 0;
        if (!need(8)) return 0.0;
        for (int i = 0; i < 8; ++i) v |= (uint64_t)d[p + i] << (8 * i);
        p += 8; double f; memcpy(&f, &v, 8); return f;
    }

    // length-prefixed binary string (little-endian u32 length)
    std::string str() {
        uint32_t len = u32le();
        if (!need(len)) { p = n; return std::string(); }
        std::string s((const char*)(d + p), len);
        p += len;
        return s;
    }
};

inline int32_t untransform_i32(uint32_t v) {
    return (int32_t)(v >> 1) ^ -(int32_t)(v & 1);
}
inline int64_t untransform_i64(uint64_t v) {
    return (int64_t)(v >> 1) ^ -(int64_t)(v & 1);
}

// Read N elements of S bytes each, byte-interleaved, into element-major order
// (out[i*S + j] = byte j of element i). Byte order within an element is the
// original stream order.
bool readInterleaved(Reader& r, size_t N, size_t S, std::vector<uint8_t>& out) {
    if (S == 0 || N == 0) { out.clear(); return true; }
    if (!r.need(N * S)) return false;
    const uint8_t* buf = r.d + r.p;
    r.p += N * S;
    out.resize(N * S);
    for (size_t i = 0; i < N; ++i)
        for (size_t j = 0; j < S; ++j)
            out[i * S + j] = buf[j * N + i];
    return true;
}

// Interleaved, big-endian "Roblox" float32 array.
bool readF32Array(Reader& r, size_t N, std::vector<float>& out) {
    std::vector<uint8_t> t;
    if (!readInterleaved(r, N, 4, t)) return false;
    out.resize(N);
    for (size_t i = 0; i < N; ++i) {
        uint32_t v = ((uint32_t)t[i * 4] << 24) | ((uint32_t)t[i * 4 + 1] << 16) |
                     ((uint32_t)t[i * 4 + 2] << 8) | (uint32_t)t[i * 4 + 3];
        v = (v >> 1) | (v << 31); // rotate right by 1 -> IEEE bits
        memcpy(&out[i], &v, 4);
    }
    return true;
}

// Interleaved, big-endian transformed (zig-zag) int32 array.
bool readI32Array(Reader& r, size_t N, std::vector<int32_t>& out) {
    std::vector<uint8_t> t;
    if (!readInterleaved(r, N, 4, t)) return false;
    out.resize(N);
    for (size_t i = 0; i < N; ++i) {
        uint32_t v = ((uint32_t)t[i * 4] << 24) | ((uint32_t)t[i * 4 + 1] << 16) |
                     ((uint32_t)t[i * 4 + 2] << 8) | (uint32_t)t[i * 4 + 3];
        out[i] = untransform_i32(v);
    }
    return true;
}

// Interleaved, big-endian u32 array (Enum / SharedString indices).
bool readU32Array(Reader& r, size_t N, std::vector<uint32_t>& out) {
    std::vector<uint8_t> t;
    if (!readInterleaved(r, N, 4, t)) return false;
    out.resize(N);
    for (size_t i = 0; i < N; ++i)
        out[i] = ((uint32_t)t[i * 4] << 24) | ((uint32_t)t[i * 4 + 1] << 16) |
                 ((uint32_t)t[i * 4 + 2] << 8) | (uint32_t)t[i * 4 + 3];
    return true;
}

// A "referent array": interleaved transformed i32, delta-accumulated.
bool readReferentArray(Reader& r, size_t N, std::vector<int32_t>& out) {
    std::vector<int32_t> deltas;
    if (!readI32Array(r, N, deltas)) return false;
    out.resize(N);
    int32_t last = 0;
    for (size_t i = 0; i < N; ++i) { last += deltas[i]; out[i] = last; }
    return true;
}

// ── CFrame basic-rotation table ─────────────────────────────────────────────
const float* basicRotation(uint8_t id) {
    static const float k02[9] = { 1,0,0, 0,1,0, 0,0,1 };
    static const float k03[9] = { 1,0,0, 0,0,-1, 0,1,0 };
    static const float k05[9] = { 1,0,0, 0,-1,0, 0,0,-1 };
    static const float k06[9] = { 1,0,0, 0,0,1, 0,-1,0 };
    static const float k07[9] = { 0,1,0, 1,0,0, 0,0,-1 };
    static const float k09[9] = { 0,0,1, 1,0,0, 0,1,0 };
    static const float k0a[9] = { 0,-1,0, 1,0,0, 0,0,1 };
    static const float k0c[9] = { 0,0,-1, 1,0,0, 0,-1,0 };
    static const float k0d[9] = { 0,1,0, 0,0,1, 1,0,0 };
    static const float k0e[9] = { 0,0,-1, 0,1,0, 1,0,0 };
    static const float k10[9] = { 0,-1,0, 0,0,-1, 1,0,0 };
    static const float k11[9] = { 0,0,1, 0,-1,0, 1,0,0 };
    static const float k14[9] = { -1,0,0, 0,1,0, 0,0,-1 };
    static const float k15[9] = { -1,0,0, 0,0,1, 0,1,0 };
    static const float k17[9] = { -1,0,0, 0,-1,0, 0,0,1 };
    static const float k18[9] = { -1,0,0, 0,0,-1, 0,-1,0 };
    static const float k19[9] = { 0,1,0, -1,0,0, 0,0,1 };
    static const float k1b[9] = { 0,0,-1, -1,0,0, 0,1,0 };
    static const float k1c[9] = { 0,-1,0, -1,0,0, 0,0,-1 };
    static const float k1e[9] = { 0,0,1, -1,0,0, 0,-1,0 };
    static const float k1f[9] = { 0,1,0, 0,0,-1, -1,0,0 };
    static const float k20[9] = { 0,0,1, 0,1,0, -1,0,0 };
    static const float k22[9] = { 0,-1,0, 0,0,1, -1,0,0 };
    static const float k23[9] = { 0,0,-1, 0,-1,0, -1,0,0 };
    switch (id) {
        case 0x02: return k02; case 0x03: return k03; case 0x05: return k05;
        case 0x06: return k06; case 0x07: return k07; case 0x09: return k09;
        case 0x0a: return k0a; case 0x0c: return k0c; case 0x0d: return k0d;
        case 0x0e: return k0e; case 0x10: return k10; case 0x11: return k11;
        case 0x14: return k14; case 0x15: return k15; case 0x17: return k17;
        case 0x18: return k18; case 0x19: return k19; case 0x1b: return k1b;
        case 0x1c: return k1c; case 0x1e: return k1e; case 0x1f: return k1f;
        case 0x20: return k20; case 0x22: return k22; case 0x23: return k23;
        default:   return nullptr;
    }
}

// Read an array of N CFrames: per-element rotation id (+9 LE floats when id==0),
// then three interleaved f32 component arrays for the positions.
bool readCFrames(Reader& r, size_t N, std::vector<CFrame>& out) {
    out.assign(N, CFrame{});
    for (size_t i = 0; i < N; ++i) {
        uint8_t id = r.u8();
        if (!r.ok) return false;
        if (id == 0) {
            for (int k = 0; k < 9; ++k) out[i].r[k] = r.f32le();
        } else {
            const float* br = basicRotation(id);
            if (!br) return false;
            for (int k = 0; k < 9; ++k) out[i].r[k] = br[k];
        }
    }
    std::vector<float> xs, ys, zs;
    if (!readF32Array(r, N, xs)) return false;
    if (!readF32Array(r, N, ys)) return false;
    if (!readF32Array(r, N, zs)) return false;
    for (size_t i = 0; i < N; ++i) { out[i].x = xs[i]; out[i].y = ys[i]; out[i].z = zs[i]; }
    return true;
}

// Skip the value payload of a property array with the given type and length.
bool skipValues(Reader& r, uint8_t type, size_t N) {
    switch (type) {
        case 0x01: // String / ContentId / BinaryString / Tags / Attributes
            for (size_t i = 0; i < N; ++i) { uint32_t len = r.u32le(); if (!r.skip(len)) return false; }
            return true;
        case 0x02: return r.skip(N);            // Bool
        case 0x03: return r.skip(N * 4);        // Int32
        case 0x04: return r.skip(N * 4);        // Float32
        case 0x05: return r.skip(N * 8);        // Float64
        case 0x06: return r.skip(N * 8);        // UDim
        case 0x07: return r.skip(N * 16);       // UDim2
        case 0x08: return r.skip(N * 24);       // Ray
        case 0x09: return r.skip(N);            // Faces
        case 0x0a: return r.skip(N);            // Axes
        case 0x0b: return r.skip(N * 4);        // BrickColor
        case 0x0c: return r.skip(N * 12);       // Color3
        case 0x0d: return r.skip(N * 8);        // Vector2
        case 0x0e: return r.skip(N * 12);       // Vector3
        case 0x10: { std::vector<CFrame> tmp; return readCFrames(r, N, tmp); } // CFrame
        case 0x12: return r.skip(N * 4);        // Enum
        case 0x13: return r.skip(N * 4);        // Ref
        case 0x14: return r.skip(N * 6);        // Vector3int16
        case 0x15:                              // NumberSequence
            for (size_t i = 0; i < N; ++i) {
                uint32_t cnt = r.u32le();
                if (!r.skip((size_t)cnt * 12)) return false;
            }
            return true;
        case 0x16:                              // ColorSequence
            for (size_t i = 0; i < N; ++i) {
                uint32_t cnt = r.u32le();
                if (!r.skip((size_t)cnt * 20)) return false;
            }
            return true;
        case 0x17: return r.skip(N * 8);        // NumberRange
        case 0x18: return r.skip(N * 16);       // Rect
        case 0x19:                              // PhysicalProperties
            for (size_t i = 0; i < N; ++i) {
                uint8_t disc = r.u8();
                if (disc == 0x01) { if (!r.skip(20)) return false; }
                else if (disc == 0x03) { if (!r.skip(24)) return false; }
                else if (disc != 0x00 && disc != 0x02) return false;
            }
            return true;
        case 0x1a: return r.skip(N * 3);        // Color3uint8
        case 0x1b: return r.skip(N * 8);        // Int64
        case 0x1c: return r.skip(N * 4);        // SharedString
        case 0x1e: {                            // OptionalCFrame
            if (r.u8() != 0x10) return false;
            std::vector<CFrame> tmp;
            if (!readCFrames(r, N, tmp)) return false;
            if (r.u8() != 0x02) return false;
            return r.skip(N);
        }
        case 0x1f: return r.skip(N * 16);       // UniqueId
        case 0x20:                              // Font
            for (size_t i = 0; i < N; ++i) {
                uint32_t fl = r.u32le(); if (!r.skip(fl)) return false;
                if (!r.skip(3)) return false;   // weight u16 + style u8
                uint32_t cl = r.u32le(); if (!r.skip(cl)) return false;
            }
            return true;
        case 0x21: return r.skip(N * 8);        // SecurityCapabilities
        case 0x22: {                            // Content
            if (!r.skip(N * 4)) return false;
            uint32_t uriCount = r.u32le();
            for (uint32_t i = 0; i < uriCount; ++i) {
                uint32_t l = r.u32le(); if (!r.skip(l)) return false;
            }
            uint32_t objCount = r.u32le();
            if (!r.skip((size_t)objCount * 4)) return false;
            uint32_t extCount = r.u32le();
            return r.skip((size_t)extCount * 4);
        }
        default: return false;
    }
}

// ── rotation math ───────────────────────────────────────────────────────────
// Quaternion (x,y,z,w) of a column-vector rotation matrix (rows r[0..8]).
void QuatFromMatCol(const float* m, float q[4]) {
    const float tr = m[0] + m[4] + m[8];
    float x, y, z, w;
    if (tr > 0.0f) {
        float s = sqrtf(tr + 1.0f) * 2.0f;
        w = 0.25f * s; x = (m[7] - m[5]) / s; y = (m[2] - m[6]) / s; z = (m[3] - m[1]) / s;
    } else if (m[0] > m[4] && m[0] > m[8]) {
        float s = sqrtf(1.0f + m[0] - m[4] - m[8]) * 2.0f;
        w = (m[7] - m[5]) / s; x = 0.25f * s; y = (m[1] + m[3]) / s; z = (m[2] + m[6]) / s;
    } else if (m[4] > m[8]) {
        float s = sqrtf(1.0f + m[4] - m[0] - m[8]) * 2.0f;
        w = (m[2] - m[6]) / s; x = (m[1] + m[3]) / s; y = 0.25f * s; z = (m[5] + m[7]) / s;
    } else {
        float s = sqrtf(1.0f + m[8] - m[0] - m[4]) * 2.0f;
        w = (m[3] - m[1]) / s; x = (m[2] + m[6]) / s; y = (m[5] + m[7]) / s; z = 0.25f * s;
    }
    q[0] = x; q[1] = y; q[2] = z; q[3] = w;
}

// Roblox CFrame rotation -> semantic-frame quaternion. Roblox's joint frame has
// X = right, so we re-express it with the player-mirror convention (semantic X
// is the LEFT): negate the X row and column of the rotation matrix.
Quat SemanticQuat(const CFrame& cf) {
    float m[9];
    memcpy(m, cf.r, sizeof(m));
    m[1] = -m[1]; m[2] = -m[2]; m[3] = -m[3]; m[6] = -m[6];
    Quat q;
    QuatFromMatCol(m, &q.x);
    return q;
}

Quat Slerp(const Quat& a, const Quat& b, float u) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    Quat bb = b;
    if (d < 0.0f) { bb.x = -bb.x; bb.y = -bb.y; bb.z = -bb.z; bb.w = -bb.w; d = -d; }
    if (d > 0.9995f) {
        Quat r{ a.x + u * (bb.x - a.x), a.y + u * (bb.y - a.y),
                a.z + u * (bb.z - a.z), a.w + u * (bb.w - a.w) };
        const float n = sqrtf(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
        if (n > 1e-8f) { r.x /= n; r.y /= n; r.z /= n; r.w /= n; }
        return r;
    }
    const float th = acosf(d);
    const float s = sinf(th);
    const float wa = sinf((1.0f - u) * th) / s;
    const float wb = sinf(u * th) / s;
    return Quat{ wa * a.x + wb * bb.x, wa * a.y + wb * bb.y,
                 wa * a.z + wb * bb.z, wa * a.w + wb * bb.w };
}

// ── intermediate instance record ────────────────────────────────────────────
struct Instance {
    std::string cls;
    std::string name;
    float time = 0.0f;
    float length = 1.0f;
    bool  loop = false;
    int   priority = 0;
    bool  hasCF = false;
    CFrame cf;
    int   parent = -1;
};

} // namespace

void Sequence::Sample(float t, std::vector<std::pair<std::string, Quat>>& out) const {
    out.clear();
    for (const Track& tr : tracks) {
        if (tr.keys.empty()) continue;
        const auto& ks = tr.keys;
        if (t <= ks.front().first) { out.push_back({ tr.joint, ks.front().second }); continue; }
        if (t >= ks.back().first)  { out.push_back({ tr.joint, ks.back().second });  continue; }
        size_t j = 1;
        while (j < ks.size() && ks[j].first < t) ++j;
        const float t0 = ks[j - 1].first, t1 = ks[j].first;
        const float u = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0f;
        out.push_back({ tr.joint, Slerp(ks[j - 1].second, ks[j].second, u) });
    }
}

void Parser::Reset() {
    sequences_.clear();
    fileData_.clear();
    error_.clear();
}

bool Parser::LoadFromFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { Reset(); error_ = "cannot open file"; return false; }
    const std::streamoff sz = f.tellg();
    if (sz <= 0) { Reset(); error_ = "empty file"; return false; }
    fileData_.resize((size_t)sz);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(fileData_.data()), sz);
    // LoadFromMemory clears/reuses fileData_; keep a copy of the bytes.
    std::vector<uint8_t> bytes = std::move(fileData_);
    return LoadFromMemory(bytes.data(), bytes.size());
}

bool Parser::LoadFromMemory(const uint8_t* data, size_t size) {
    Reset();
    if (!data || size < 32) { error_ = "file too small"; return false; }

    Reader r{ data, size, 0, true };
    static const char kMagic[8] = { '<', 'r', 'o', 'b', 'l', 'o', 'x', '!' };
    if (memcmp(data, kMagic, 8) != 0) { error_ = "not an RBXM file"; return false; }

    r.p = 8 + 6;                 // magic + signature
    r.u16le();                   // version
    const uint32_t numTypes = r.u32le();
    const uint32_t numInstances = r.u32le();
    r.p += 8;                    // reserved
    (void)numTypes;

    std::vector<Instance> insts;
    insts.reserve(numInstances);
    std::unordered_map<int32_t, int> refToIndex;
    std::unordered_map<uint32_t, std::pair<size_t, size_t>> typeRanges;
    std::vector<std::pair<int32_t, int32_t>> pendingParents;

    while (r.ok) {
        if (!r.need(16)) break;
        char name[4];
        for (int i = 0; i < 4; ++i) name[i] = (char)r.u8();
        const uint32_t compLen = r.u32le();
        const uint32_t uncompLen = r.u32le();
        r.u32le();               // reserved
        if (!r.ok) break;
        if (name[0] == 'E' && name[1] == 'N' && name[2] == 'D') break;

        if (compLen != 0) { error_ = "compressed RBXM chunks are not supported"; return false; }
        const size_t dataStart = r.p;
        if (!r.need(uncompLen)) { error_ = "truncated chunk"; return false; }
        Reader c{ r.d + dataStart, uncompLen, 0, true };
        r.p = dataStart + uncompLen;

        if (name[0] == 'I' && name[1] == 'N' && name[2] == 'S' && name[3] == 'T') {
            // INST
            const uint32_t typeId = c.u32le();
            const std::string cls = c.str();
            c.u8();              // object format
            const uint32_t count = c.u32le();
            std::vector<int32_t> refs;
            if (!readReferentArray(c, count, refs)) { error_ = "bad INST referents"; return false; }
            const size_t start = insts.size();
            for (uint32_t i = 0; i < count; ++i) {
                Instance in;
                in.cls = cls;
                refToIndex[refs[i]] = (int)(start + i);
                insts.push_back(std::move(in));
            }
            typeRanges[typeId] = { start, count };
        } else if (name[0] == 'P' && name[1] == 'R' && name[2] == 'O' && name[3] == 'P') {
            // PROP
            const uint32_t typeId = c.u32le();
            const std::string prop = c.str();
            const uint8_t typeByte = c.u8();
            auto it = typeRanges.find(typeId);
            if (it == typeRanges.end()) continue;
            const size_t start = it->second.first;
            const size_t count = it->second.second;
            if (!c.ok) continue;

            const bool isName = (prop == "Name");
            const bool isTime = (prop == "Time" && typeByte == 0x04);
            const bool isLen  = (prop == "Length" && typeByte == 0x04);
            const bool isLoop = (prop == "Loop" && typeByte == 0x02);
            const bool isPrio = (prop == "Priority" && (typeByte == 0x12 || typeByte == 0x03));

            if (isName) {
                for (size_t i = 0; i < count; ++i) {
                    insts[start + i].name = c.str();
                }
            } else if (isTime || isLen) {
                std::vector<float> vals;
                if (!readF32Array(c, count, vals)) return false;
                for (size_t i = 0; i < count; ++i) {
                    if (isTime) insts[start + i].time = vals[i];
                    else        insts[start + i].length = vals[i];
                }
            } else if (isLoop) {
                for (size_t i = 0; i < count; ++i) insts[start + i].loop = (c.u8() != 0);
            } else if (isPrio) {
                if (typeByte == 0x12) {
                    std::vector<uint32_t> vals;
                    if (!readU32Array(c, count, vals)) return false;
                    for (size_t i = 0; i < count; ++i) insts[start + i].priority = (int)vals[i];
                } else {
                    std::vector<int32_t> vals;
                    if (!readI32Array(c, count, vals)) return false;
                    for (size_t i = 0; i < count; ++i) insts[start + i].priority = vals[i];
                }
            } else if (typeByte == 0x10) {
                std::vector<CFrame> cfs;
                if (!readCFrames(c, count, cfs)) return false;
                for (size_t i = 0; i < count; ++i) {
                    insts[start + i].cf = cfs[i];
                    insts[start + i].hasCF = true;
                }
            } else {
                if (!skipValues(c, typeByte, count)) {
                    error_ = "unsupported property type";
                    return false;
                }
            }
        } else if (name[0] == 'P' && name[1] == 'R' && name[2] == 'N' && name[3] == 'T') {
            // PRNT
            c.u8();              // version
            const uint32_t count = c.u32le();
            std::vector<int32_t> subjects, parents;
            if (!readReferentArray(c, count, subjects)) { error_ = "bad PRNT"; return false; }
            if (!readReferentArray(c, count, parents))  { error_ = "bad PRNT"; return false; }
            for (uint32_t i = 0; i < count; ++i)
                pendingParents.push_back({ subjects[i], parents[i] });
        }
        // SSTR / META / unknown chunks are ignored.
    }

    if (!r.ok) { error_ = "unexpected end of file"; return false; }

    // Resolve parenting.
    for (const auto& pr : pendingParents) {
        auto ci = refToIndex.find(pr.first);
        auto pi = refToIndex.find(pr.second);
        if (ci != refToIndex.end() && pi != refToIndex.end())
            insts[ci->second].parent = pi->second;
    }

    // ── assemble sequences ──────────────────────────────────────────────────
    std::vector<int> seqOf(insts.size(), -1);
    for (size_t i = 0; i < insts.size(); ++i) {
        if (insts[i].cls == "KeyframeSequence") {
            Sequence s;
            s.name = insts[i].name;
            s.length = (insts[i].length > 0.0f) ? insts[i].length : 1.0f;
            s.loop = insts[i].loop;
            s.priority = insts[i].priority;
            seqOf[i] = (int)sequences_.size();
            sequences_.push_back(std::move(s));
        }
    }

    struct KF {
        float time = 0.0f;
        int   seq = -1;
        std::vector<std::pair<std::string, CFrame>> poses;
    };
    std::vector<KF> kfs;
    std::vector<int> kfOf(insts.size(), -1);
    for (size_t i = 0; i < insts.size(); ++i) {
        if (insts[i].cls != "Keyframe") continue;
        KF k;
        k.time = insts[i].time;
        k.seq = (insts[i].parent >= 0) ? seqOf[insts[i].parent] : -1;
        kfOf[i] = (int)kfs.size();
        kfs.push_back(std::move(k));
    }
    for (size_t i = 0; i < insts.size(); ++i) {
        if (insts[i].cls != "Pose" || !insts[i].hasCF) continue;
        const int ki = (insts[i].parent >= 0) ? kfOf[insts[i].parent] : -1;
        if (ki >= 0) kfs[ki].poses.push_back({ insts[i].name, insts[i].cf });
    }

    // Build per-joint tracks (semantic quats, sorted by time).
    std::vector<std::unordered_map<std::string, std::vector<std::pair<float, Quat>>>> perSeq(sequences_.size());
    for (const KF& k : kfs) {
        if (k.seq < 0 || k.seq >= (int)perSeq.size()) continue;
        for (const auto& p : k.poses)
            perSeq[k.seq][p.first].push_back({ k.time, SemanticQuat(p.second) });
    }
    for (size_t s = 0; s < sequences_.size(); ++s) {
        for (auto& kv : perSeq[s]) {
            Track tr;
            tr.joint = kv.first;
            tr.keys = std::move(kv.second);
            std::sort(tr.keys.begin(), tr.keys.end(),
                      [](const std::pair<float, Quat>& a, const std::pair<float, Quat>& b) {
                          return a.first < b.first;
                      });
            sequences_[s].tracks.push_back(std::move(tr));
        }
    }

    // If a length is missing, fall back to the last keyframe time.
    for (Sequence& s : sequences_) {
        float maxT = 0.0f;
        for (const Track& tr : s.tracks)
            if (!tr.keys.empty()) maxT = (std::max)(maxT, tr.keys.back().first);
        if (maxT > s.length) s.length = maxT;
        if (s.length < 0.01f) s.length = 0.01f;
    }

    if (sequences_.empty()) { error_ = "no KeyframeSequence found"; return false; }
    return true;
}

} // namespace RBXM
