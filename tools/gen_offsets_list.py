"""
Generates offsets_list.inc from Seraph/rbx/offsets.h.

Produces a flat list of OFFSET_X(Namespace, Name, 0xVALUE) entries, one per line,
that can be reused twice via #include with different OFFSET_X macro definitions.

Single-level namespaces only (matches the existing offsets.h structure).
"""
import re
from pathlib import Path

SRC = Path("Seraph/rbx/offsets.h")
DST = Path("Seraph/rbx/offsets_list.inc")

text = SRC.read_text(encoding="utf-8")

# State machine: current namespace name; "" = top level
current_ns = ""
entries = []

# Patterns
ns_open = re.compile(r"^\s*namespace\s+(\w+)\s*\{")
ns_close = re.compile(r"^\s*\}\s*$")
offset_line = re.compile(r"^\s*inline\s+constexpr\s+uintptr_t\s+(\w+)\s*=\s*(0x[0-9a-fA-F]+)\s*;\s*$")

for line in text.splitlines():
    m_open = ns_open.match(line)
    m_close = ns_close.match(line)
    m_off = offset_line.match(line)
    if m_open:
        # Only set namespace when we're at top level (the Offsets::ClientVersion line
        # sits at the top level inside `namespace Offsets {`).
        new_ns = m_open.group(1)
        if new_ns != "Offsets":
            current_ns = new_ns
        else:
            current_ns = ""
        continue
    if m_close:
        # Closing brace — pop one level. We don't track depth explicitly
        # because namespaces are single-level in this file.
        current_ns = ""
        continue
    if m_off:
        if not current_ns:
            # Top-level constant like ClientVersion (which is std::string anyway) — skip.
            continue
        name = m_off.group(1)
        value = m_off.group(2).lower()
        entries.append((current_ns, name, value))

# Write the .inc file
with DST.open("w", encoding="utf-8", newline="\n") as f:
    for ns, name, val in entries:
        f.write(f"OFFSET_X({ns}, {name}, {val})\n")

print(f"Wrote {len(entries)} entries to {DST}")
