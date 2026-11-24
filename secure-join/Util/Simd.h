#pragma once
#include "secure-join/Defines.h"
#include <cstring>
#ifdef ENABLE_SSE
#include <immintrin.h>
#endif


namespace secJoin
{

#ifdef ENABLE_SSE

    OC_FORCEINLINE block shuffle_epi8(const block& a, const block& b)
    {
        return _mm_shuffle_epi8(a, b);
    }

    template<int s>
    OC_FORCEINLINE block slli_epi16(const block& v)
    {
        return _mm_slli_epi16(v, s);
    }

    OC_FORCEINLINE  int movemask_epi8(const block v)
    {
        return _mm_movemask_epi8(v);
    }
#else
    OC_FORCEINLINE block shuffle_epi8(const block& a, const block& b)
    {
        // _mm_shuffle_epi8(a, b): 
        //     FOR j := 0 to 15
        //         i: = j * 8
        //         IF b[i + 7] == 1
        //             dst[i + 7:i] : = 0
        //         ELSE
        //             index[3:0] : = b[i + 3:i]
        //             dst[i + 7:i] : = a[index * 8 + 7:index * 8]
        //         FI
        //     ENDFOR

        block dst;
        for (u64 i = 0; i < 16; ++i)
        {
            auto bi = b.get<i8>(i);

            // 0 if bi < 0. otherwise 11111111
            u8 mask = ~(-i8(bi >> 7));
            u8 idx = bi & 15;

            dst.set<i8>(i, a.get<i8>(idx) & mask);
        }
        return dst;
    }

    template<int s>
    OC_FORCEINLINE block slli_epi16(const block& v)
    {
        block r;
        auto rr = (i16*)&r;
        auto vv = (const i16*)&v;
        for (u64 i = 0; i < 8; ++i)
            rr[i] = vv[i] << s;
        return r;
    }

    OC_FORCEINLINE int movemask_epi8(const block v)
    {
        // extract all the of MSBs if each byte.
        u64 mask = 1;
        int r = 0;
        for (i64 i = 0; i < 16; ++i)
        {
            r |= (v.get<u8>(0) >> i) & mask;
            mask <<= 1;
        }
        return r;
    }

#endif



    //FOR j := 0 to 7
    //	i := j*16
    //	dst[i+15:i] := ( a[i+15:i] == b[i+15:i] ) ? 0xFFFF : 0
    //ENDFOR
    OC_FORCEINLINE  block cmpeq_epi16(const block& a, const block& b)
    {
#ifdef ENABLE_SSE
        return _mm_cmpeq_epi16(a, b);
#else
        block r;
        for (i64 i = 0; i < 8; ++i)
        {
            r.set(i, a.get<i16>(i) == b.get<i16>(i) ? 0xFFFF : 0);
        }
        return r;
#endif // ENABLE_SSE
    }


    OC_FORCEINLINE void storeu_si32(void* mem, const block& a)
    {
#ifdef ENABLE_SSE
        _mm_storeu_si32(mem, a);
#else
        i32 lane = a.get<i32>(0);
        std::memcpy(mem, &lane, sizeof(lane));
#endif
    }
}
