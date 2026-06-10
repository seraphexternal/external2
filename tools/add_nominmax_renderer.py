"""
Fix compile errors C2589/C2059 at Seraph/overlay/renderer.cpp (55, 57).

Root cause: Windows headers (pulled in transitively via <shobjidl.h> in this
file) define `max` and `min` as preprocessor macros. Once those macros are
defined, the C++ template function `std::max(...)` used by the MenuWeather
particle engine (SeedParticle around line 53-58) gets the `max` token
macro-expanded and fails to parse as a member of the `std` namespace.

Standard Windows-targeted C++ fix: define NOMINMAX before any Windows
header is included. NOMINMAX is harmless for the rest of the codebase:
the few places that prefer the min/max macros don't exist; everything
else already gets the standard std::min / std::max.
"""
PATH = 'Seraph/overlay/renderer.cpp'

with open(PATH, 'r', encoding='utf-8') as f:
    src = f.read()

OLD = '#include <shobjidl.h>'
NEW = (
    '// Windows headers pulled in below (directly + via <shobjidl.h>) define\n'
    '// `max` / `min` as macros which clash with `std::max` / `std::min`\n'
    '// used by the MenuWeather particle engine. NOMINMAX is the\n'
    '// standard opt-out and is harmless for the rest of the file.\n'
    '#define NOMINMAX\n'
    '#include <shobjidl.h>'
)

if src.count(OLD) != 1:
    raise SystemExit(f'[abort] anchor found {src.count(OLD)} times')

src = src.replace(OLD, NEW, 1)

with open(PATH, 'w', encoding='utf-8') as f:
    f.write(src)

print(f'[ok] {PATH}: {len(src)} bytes; NOMINMAX installed')
