#pragma once
#include "libOTe/Tools/CoeffCtx.h"
#include "secure-join/Defines.h"

namespace secJoin
{
    // a value in the range {0,1,2,3}.
    struct F4 
    {
        u8 mVal;

        F4 operator^(const F4& rhs) const
        {
            return { static_cast<u8>(mVal ^ rhs.mVal) };
        }


        bool operator==(const F4& rhs) const
        {
            return mVal == rhs.mVal;
        }
    };

    // A coefficient context for GF4 used in the VOLE protocol.
    struct CoeffCtxGF4 : oc::CoeffCtxInteger
    {
        // the subfield as GF4
        using G = F4;

        // the extension field as GF(2^128)
        using F = block;

        template<typename T>
        OC_FORCEINLINE void plus(T& ret, const T& lhs, const T& rhs) {
            ret = lhs ^ rhs;
        }

        template<typename T>
        OC_FORCEINLINE void minus(T& ret, const T& lhs, const T& rhs) {
            ret = lhs ^ rhs;
        }

        OC_FORCEINLINE void mul(F& ret, const F& lhs, const G& rhs) {
            assert(rhs.mVal < 4);
            switch (rhs.mVal)
            {
            case 0:
                ret = zeroElem<F>();
                break;
            case 1:
                ret = lhs;
                break;
            case 2:
            {

                auto lsb = lhs & block::allSame<u8>(0b01010101);
                auto msb = (lhs >> 1) & block::allSame<u8>(0b01010101);

                auto rlsb = msb;
                auto rmsb = lsb ^ msb;
                
                ret = rlsb | (rmsb << 1);
                break;

            }
            case 3:
            {

                auto lsb = lhs & block::allSame<u8>(0b01010101);
                auto msb = (lhs >> 1) & block::allSame<u8>(0b01010101);

                auto rlsb = lsb ^ msb;
                auto rmsb = lsb;

                ret = rlsb | (rmsb << 1);
                break;
            }
            default:
                assert(0);
            }
        }

        OC_FORCEINLINE void mul(G& ret, const G& lhs, const G& rhs) {
            assert(rhs.mVal < 4);
            switch (rhs.mVal)
            {
            case 0:
                ret = zeroElem<G>();
                break;
            case 1:
                ret = lhs;
                break;
            case 2:
            {

                auto lsb = lhs.mVal & 1;
                auto msb = (lhs.mVal >> 1) & 1;

                auto rlsb = msb;
                auto rmsb = lsb ^ msb;

                ret = { static_cast<u8>(rlsb | (rmsb << 1)) };
                break;

            }
            case 3:
            {

                auto lsb = lhs.mVal & 1;
                auto msb = (lhs.mVal >> 1) & 1;

                auto rlsb = lsb ^ msb;
                auto rmsb = lsb;

                ret = { static_cast<u8>(rlsb | (rmsb << 1)) };
                break;
            }
            default:
                assert(0);
            }
        }

        template<typename T>
        OC_FORCEINLINE void mulConst(T& ret, const F& x)
        {
            mul(ret, x, G{ 3 });
        }

        template<typename T>
        OC_FORCEINLINE void mulConst(T& ret, const G& x)
        {
            mul(ret, x, G{ 3 });
        }

        // the bit size require to prepresent F
        // the protocol will perform binary decomposition
        // of F using this many bits
        template<typename F2>
        u64 bitSize() const
        {
            if (std::is_same<G, F2>::value)
                return 2;
            else
                return sizeof(F) * 8;
        }

        // is F a field?
        template<typename F>
        OC_FORCEINLINE bool isField() {
            return true; // default.
        }

        template<typename F>
        static OC_FORCEINLINE constexpr F zeroElem()
        {
            static_assert(std::is_trivially_copyable<F>::value, "memset is used so must be trivially_copyable.");
            F r;
            osuCrypto::setBytes(r, 0);
            return r;
        }

        template<typename T>
        OC_FORCEINLINE void fromBlock(T& ret, const block& b) {
            
            if constexpr (std::is_same<T, F>::value)
            {
                // if F is a block, just return the block with the LSB masked to zero.
                ret = b & block(~0ull, ~0ull << 2);
            }
            else if constexpr (std::is_same<T, G>::value)
            {
                static_assert(std::is_same_v<T, F4>, "");
                ret = { static_cast<u8>(b.get<u8>(0) & 3) };
            }
            else
            {
                static_assert(std::is_same<T, F>::value ||std::is_same<T, G>::value, "request type not supported");
            }
        }

    };


}