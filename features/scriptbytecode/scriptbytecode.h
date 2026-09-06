#pragma once
// Ported from jew-dick-hack: src/features/lua/vm/ScriptBytecode.{h,cpp}
// Adapted to Seraph's Memory-> driver and Offsets:: namespace.
// The in-process "Fission" decompile path is not ported; only the built-in
// Luau lifter is used (Fission hosted decompilation is out of scope).
#include <cstdint>
#include <string>
#include <vector>

namespace ScriptBytecode {

bool IsScriptClass(const std::string& cls);

// raw bytes as stored in Roblox (may be RSB1+zstd / signed / encoded)
bool ReadRaw(uintptr_t script_addr, const std::string& class_name, std::vector<std::uint8_t>& out);

// normalize -> raw Luau bytecode (version byte 3..11). status explains path/failure.
bool Normalize(const std::vector<std::uint8_t>& raw, std::vector<std::uint8_t>& out, std::string* status = nullptr);

// alias used by older call sites
bool Decompress(const std::vector<std::uint8_t>& raw, std::vector<std::uint8_t>& out);

bool ReadDecompressed(uintptr_t script_addr, const std::string& class_name, std::vector<std::uint8_t>& out);

// readable source via the built-in linear lifter
std::string Decompile(const std::vector<std::uint8_t>& raw_or_luau, const char* chunk_name = "script");

} // namespace ScriptBytecode
