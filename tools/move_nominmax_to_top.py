"""
Move `#define NOMINMAX` to the very top of renderer.cpp.

Reason: configs.h (pulled in transitively on line 2 of renderer.cpp
via #include "renderer.h") includes <shlobj.h> which makes Windows
headers define the `min` / `max` preprocessor macros before our
NOMINMAX at line 7 has a chance to take effect. We need NOMINMAX to
be the very first thing the preprocessor sees so the windows-family
headers respect it on first inclusion.
"""
PATH = 'Seraph/overlay/renderer.cpp'

with open(PATH, 'r', encoding='utf-8') as f:
    src = f.read()

OLD = (
    '// Windows headers pulled in below (directly + via <shobjidl.h>) define\n'
    '// `max` / `min` as macros which clash with `std::max` / `std::min`\n'
    '// used by the MenuWeather particle engine. NOMINMAX is the\n'
    '// standard opt-out and is harmless for the rest of the file.\n'
    '#define NOMINMAX\n'
    '#include <shobjidl.h>'
)
NEW = '#include <shobjidl.h>'

if src.count(OLD) != 1:
    raise SystemExit(f'[abort] anchor (old NOMINMAX block) found {src.count(OLD)} times')

src = src.replace(OLD, NEW, 1)

# Prepend NOMINMAX at the very top of the file (before any #include).
PREFIX = (
    '// Windows headers define the `min` / `max` preprocessor macros\n'
    '// which clash with `std::max` / `std::min` used by the\n'
    '// MenuWeather particle engine. Defining NOMINMAX FIRST ensures\n'
    '// every windows-family header (transitively pulled in by the\n'
    '// project headers on lines below) respects the opt-out.\n'
    '#define NOMINMAX\n'
)

src = PREFIX + src

with open(PATH, 'w', encoding='utf-8') as f:
    f.write(src)

print(f'[ok] {PATH}: {len(src)} bytes; NOMINMAX moved to top')
