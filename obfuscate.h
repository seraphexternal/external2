#pragma once

namespace obf
{
    template <int N>
    inline const char* Cat(const char* a, const char* b)
    {
        static char buf[512];
        char* d = buf;
        while (*a) *d++ = *a++;
        while (*b) *d++ = *b++;
        *d = 0;
        return buf;
    }
}

#define OBS(a, b) obf::Cat<__COUNTER__>((a), (b))
