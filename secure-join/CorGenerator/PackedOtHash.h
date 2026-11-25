#pragma once
#include "secure-join/Util/Simd.h"
#include "cryptoTools/Crypto/AES.h"

// Pack one output bit per input block in the protocol's little-endian bit order.
namespace secJoin::packedOt
{
#if defined(ENABLE_SSE) && defined(__VAES__) && defined(__AVX2__) && !defined(__AVX512F__)
    // The two 128-bit lanes gather the low words of the even and odd inputs.
    OC_FORCEINLINE __m256i lowWords(__m256i a, __m256i b, __m256i c, __m256i d)
    {
        return _mm256_unpacklo_epi64(_mm256_unpacklo_epi32(a, b), _mm256_unpacklo_epi32(c, d));
    }

    OC_FORCEINLINE __m256i inInputOrder(__m256i words)
    {
        return _mm256_permutevar8x32_epi32(words, _mm256_setr_epi32(0, 4, 1, 5, 2, 6, 3, 7));
    }

    template<unsigned bit>
    OC_FORCEINLINE u8 packBit(__m256i ordered)
    {
        return static_cast<u8>(_mm256_movemask_ps(_mm256_castsi256_ps(_mm256_slli_epi32(ordered, 31 - bit))));
    }
#endif
#if defined(ENABLE_SSE) && defined(__VAES__) && defined(__AVX512F__)
    constexpr unsigned groupSize = 32;
    using Word = u32;

    template<unsigned bit>
    OC_FORCEINLINE Word pack(const __m512i (&values)[8])
    {
        // Select the low 64-bit word of each of eight consecutive AES blocks.
        const auto lowWords = _mm512_setr_epi64(0, 2, 4, 6, 8, 10, 12, 14);
        Word bits = 0;
#pragma GCC unroll 4
        for (unsigned j = 0; j < 4; ++j)
        {
            const auto words = _mm512_permutex2var_epi64(values[2 * j], lowWords, values[2 * j + 1]);
            bits |= Word(_mm512_test_epi64_mask(words, _mm512_set1_epi64(1ull << bit))) << (8 * j);
        }
        return bits;
    }

    struct Group
    {
        __m512i raw[8];

        OC_FORCEINLINE explicit Group(const block* messages)
        {
#pragma GCC unroll 8
            for (unsigned j = 0; j < 8; ++j)
                raw[j] = _mm512_loadu_si512(messages + 4 * j);
        }

        template<unsigned bit>
        OC_FORCEINLINE Word choices() const { return pack<bit>(raw); }

        // Return bit zero of H(x) = AES(x) ^ x, where x is message ^ delta
        // with the low clearBits bits cleared.
        template<unsigned clearBits>
        OC_FORCEINLINE Word hashes(block delta = block(0, 0)) const
        {
            const auto& keys = oc::mAesFixedKey.mRoundKey;
            const block messageMask(~0ull, ~((1ull << clearBits) - 1));
            const auto mask = _mm512_broadcast_i32x4(messageMask.mData);
            const auto offset = _mm512_broadcast_i32x4((delta & messageMask).mData);
            const auto first = _mm512_xor_si512(offset, _mm512_broadcast_i32x4(keys[0].mData));
            __m512i state[8];
#pragma GCC unroll 8
            for (unsigned j = 0; j < 8; ++j)
                state[j] = _mm512_xor_si512(_mm512_and_si512(raw[j], mask), first);
#pragma GCC unroll 9
            for (unsigned round = 1; round < 10; ++round)
            {
                const auto key = _mm512_broadcast_i32x4(keys[round].mData);
#pragma GCC unroll 8
                for (unsigned j = 0; j < 8; ++j)
                    state[j] = _mm512_aesenc_epi128(state[j], key);
            }
            const auto last = _mm512_broadcast_i32x4(keys[10].mData);
#pragma GCC unroll 8
            for (unsigned j = 0; j < 8; ++j)
            {
                state[j] = _mm512_aesenclast_epi128(state[j], last);
                // A cleared low bit contributes zero to the Davies-Meyer XOR.
                if constexpr (clearBits == 0)
                    state[j] = _mm512_xor_si512(state[j], _mm512_xor_si512(raw[j], offset));
            }
            return pack<0>(state);
        }
    };
#else
    constexpr unsigned groupSize = 16;
    using Word = u16;

    struct Group
    {
        const block* messages;
        explicit Group(const block* p) : messages(p) {}

        template<unsigned bit>
        OC_FORCEINLINE Word choices() const { return packByteBits<bit>(gatherLowBytes(messages)); }

        template<unsigned clearBits>
        OC_FORCEINLINE Word hashes(block delta = block(0, 0)) const
        {
            const block mask(~0ull, ~((1ull << clearBits) - 1));
            block hashed[groupSize];
            for (unsigned i = 0; i < groupSize; i += 8)
            {
                auto batch = hashed + i;
#if defined(__GNUC__)
#pragma GCC unroll 8
#endif
                for (unsigned j = 0; j < 8; ++j)
                    batch[j] = (messages[i + j] ^ delta) & mask;
                if constexpr (clearBits == 0)
                    oc::mAesFixedKey.hashBlocks<8>(batch, batch);
                else
                    oc::mAesFixedKey.ecbEncBlocks<8>(batch, batch);
            }
            return packByteBits<0>(gatherLowBytes(hashed));
        }
    };
#endif
    OC_FORCEINLINE void store(block* output, u64 i, Word bits)
    {
        std::memcpy(reinterpret_cast<u8*>(output) + i / 8, &bits, sizeof(bits));
    }
}
