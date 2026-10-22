#pragma once

// RBXM (Roblox Binary Model) parser.
//
// Extracts every KeyframeSequence from a .rbxm file as a set of per-joint
// rotation tracks so a model can be driven by a real Roblox animation. It reads
// the documented binary format directly (INST / PROP / PRNT chunks, big-endian
// interleaved scalar arrays, the Roblox float encoding and the CFrame basic
// rotation table) — see https://dom.rojo.space/binary.
//
// Only uncompressed chunks are supported (Roblox Studio writes .rbxm files
// uncompressed; LZ4/ZSTD chunks are rejected with a clear error).

#include <cstdint>
#include <string>
#include <vector>

namespace RBXM {

// A CFrame's rotation as a row-major 3x3 (r[0..2] = row 0, ...). The matrix
// columns are the Right / Up / Back vectors, matching Roblox's convention.
struct CFrame {
    float r[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct Quat { float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f; };

struct Track {
    std::string joint;
    // (time, local rotation) sorted ascending by time. The rotation is already
    // expressed in the renderer's semantic frame (X = side, Y = up, Z = depth).
    std::vector<std::pair<float, Quat>> keys;
};

struct Sequence {
    std::string name;
    float length = 1.0f;
    bool  loop = false;
    int   priority = 0;
    std::vector<Track> tracks;

    // Sample every tracked joint at time `t` (seconds). Each joint's local
    // rotation is written as a quaternion in the SEMANTIC frame the renderer's
    // emote/pose helpers expect (X = side, Y = up, Z = depth); callers feed it
    // straight to SetJointLocalQuat. Joints absent from `out` keep the identity.
    void Sample(float t, std::vector<std::pair<std::string, Quat>>& out) const;
};

class Parser {
public:
    bool LoadFromFile(const std::string& path);
    bool LoadFromMemory(const uint8_t* data, size_t size);

    const std::vector<Sequence>& Sequences() const { return sequences_; }
    const std::string& Error() const { return error_; }

private:
    void Reset();

    std::vector<Sequence> sequences_;
    std::vector<uint8_t> fileData_;
    std::string error_;
};

} // namespace RBXM
