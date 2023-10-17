#pragma once

#include "secure-join/Prf/LowMC.h"

#include "coproto/Common/Defines.h"
#include "coproto/Common/span.h"
// namespace coproto
// {
//     namespace internal
//     {
//         inline span<u8> asSpan(std::vector<std::bitset<256>>& v)
//         {
//             return span<u8>((u8*)v.data(), v.size() * sizeof(std::bitset<256>));
//         }
//     }
// }

#include "Permutation.h"
#include "secure-join/GMW/Gmw.h"
#include "coproto/Socket/LocalAsyncSock.h"
#include <bitset>
#include "cryptoTools/Common/Matrix.h"
#include "coproto/coproto.h"
#include <numeric>
// using coproto::LocalAsyncSocket;


namespace secJoin
{

    template<int n>
    inline std::string hex(std::bitset<n> b)
    {
        auto bb = *(std::array<u8, sizeof(b)>*) & b;
        std::stringstream ss;
        for (u64 i = 0; i < bb.size(); ++i)
            ss << std::hex << std::setw(2) << std::setfill('0') << int(bb[i]);
        return ss.str();
    }

    class LowMCPerm
    {
    public:
        static const LowMC2<>& mLowMc();
        static const oc::BetaCircuit& mLowMcCir();

        template<typename T>
        static macoro::task<> apply(
            oc::MatrixView<const T> x1,
            oc::MatrixView<T> sout,
            PRNG& prng,
            coproto::Socket& chl,
            CorGenerator& ole);

        template<typename T>
        static macoro::task<> apply(
            const Perm& pi,
            PermOp op,
            oc::MatrixView<const T> x2,
            oc::MatrixView<T> sout,
            PRNG& prng,
            coproto::Socket& chl,
            CorGenerator& ole);

        template<typename T>
        static macoro::task<> apply(
            const Perm& pi,
            PermOp op,
            oc::MatrixView<T> sout,
            PRNG& prng,
            coproto::Socket& chl,
            CorGenerator& ole);
    };



    template<typename T>
    macoro::task<> LowMCPerm::apply(
        oc::MatrixView<const T> x1,
        oc::MatrixView<T> sout,
        PRNG& prng,
        coproto::Socket& chl,
        CorGenerator& ole)
    {
        oc::MatrixView<u8> xx((u8*)x1.data(), x1.rows(), x1.cols() * sizeof(T));
        oc::MatrixView<u8> oo((u8*)sout.data(), sout.rows(), sout.cols() * sizeof(T));

        return apply<u8>(xx, oo, prng, chl, ole);
    }

    template<>
    inline macoro::task<> LowMCPerm::apply<u8>(
        oc::MatrixView<const u8> x1,
        oc::MatrixView<u8> sout,
        PRNG& prng,
        coproto::Socket& chl,
        CorGenerator& ole)
    {
        MC_BEGIN(macoro::task<>, x1, &chl, sout, &prng, &ole,
            n = u64(x1.rows()),
            bytesPerRow = u64(x1.cols()),
            xEncrypted_ = std::vector<u8>{},
            xEncrypted = span<LowMC2<>::block>{},
            roundkeysMatrix = std::vector<oc::Matrix<u8>>{},
            counterMode = u64(),
            blocksPerRow = u64(),
            lowMc = mLowMc(),
            gmw0 = Gmw{}
        );

        if (x1.rows() != sout.rows() ||
            x1.cols() != sout.cols())
            throw RTE_LOC;

        {
            LowMC2<>::keyblock key;
            prng.get((u8*)&key, sizeof(key));
            lowMc.set_key(key);
        }

        blocksPerRow = oc::divCeil(bytesPerRow, sizeof(LowMC2<>::block));
        xEncrypted_.resize(blocksPerRow * n * sizeof(LowMC2<>::block));
        xEncrypted = span<LowMC2<>::block>((LowMC2<>::block*)xEncrypted_.data(),blocksPerRow * n); 

        // Encrypting the vector x
        counterMode = 0;
        for (u64 i = 0; i < n; ++i)
        {
            xEncrypted[counterMode + blocksPerRow - 1] = 0;
            memcpy(&xEncrypted[counterMode], &x1(i, 0), x1.cols());

            for (u64 j = 0; j < blocksPerRow; ++j)
            {
                xEncrypted[counterMode] ^= lowMc.encrypt(counterMode);
                ++counterMode;
            }
        }


        MC_AWAIT(chl.send(std::move(xEncrypted_)));

        // To enable debugging in the circuit
        // gmw0.mO.mDebug = true;
        // gmw0.mDebugPrintIdx = 1;


        gmw0.init(n * blocksPerRow, mLowMcCir());

        // Indexes are set by other party because they have the permutation pi
        gmw0.setZeroInput(0);

        // Encrypted x is set by other party because they have permuted the encrypted x
        gmw0.setZeroInput(1);


        // Setting up the lowmc round keys
        roundkeysMatrix.resize(lowMc.roundkeys.size());
        for (u64 i = 0; i < roundkeysMatrix.size(); i++)
        {
            // std::cout << "Setting up round key " << i << std::endl;
            roundkeysMatrix[i].resize((n * blocksPerRow), sizeof(lowMc.roundkeys[i]));

            for (u64 j = 0; j < (n * blocksPerRow); j++)
            {
                memcpy(roundkeysMatrix[i][j].data(), &lowMc.roundkeys[i], sizeof(lowMc.roundkeys[i]));
            }

            // Adding the round keys to the evaluation circuit
            gmw0.setInput(2 + i, roundkeysMatrix[i]);
        }

        MC_AWAIT(gmw0.run(ole, chl, prng));

        if (bytesPerRow % sizeof(LowMC2<>::block) == 0)
        {
            sout.reshape(n * blocksPerRow, sizeof(LowMC2<>::block));

            gmw0.getOutput(0, sout);
            sout.reshape(n, bytesPerRow);
        }
        else
        {
            oc::Matrix<u8> temp(n * blocksPerRow, sizeof(LowMC2<>::block), oc::AllocType::Uninitialized);
            gmw0.getOutput(0, temp);
            temp.reshape(n, blocksPerRow * sizeof(LowMC2<>::block));

            //sout.resize(n, bytesPerRow, oc::AllocType::Uninitialized);
            for (u64 i = 0; i < n; ++i)
            {
                memcpy(sout.data(i), temp.data(i), bytesPerRow);
            }
        }
        MC_END();
    }




    template<typename T>
    macoro::task<> LowMCPerm::apply(
        const Perm& pi,
        PermOp op,
        oc::MatrixView<T> sout,
        PRNG& prng,
        coproto::Socket& chl,
        CorGenerator& ole)
    {
        oc::MatrixView<u8> oo((u8*)sout.data(), sout.rows(), sout.cols() * sizeof(T));
        return apply<u8>(pi, op, oo, prng, chl, ole);
    }

    template<>
    inline macoro::task<> LowMCPerm::apply<u8>(
        const Perm& pi,
        PermOp op,
        oc::MatrixView<u8> sout,
        PRNG& prng,
        coproto::Socket& chl,
        CorGenerator& ole)
    {
        MC_BEGIN(macoro::task<>, &pi, &chl, sout, op, &ole, &prng,
            xEncrypted_ = std::vector<u8>{},
            xEncrypted = span<LowMC2<>::block>{},
            xPermuted = std::vector<LowMC2<>::block>{},
            indexMatrix = std::vector<LowMC2<>::block>{},
            blocksPerRow = u64(),
            gmw1 = Gmw{},
            n = sout.rows(),
            bytesPerRow = sout.cols()
        );

        if (pi.size() != n)
            throw RTE_LOC;

        using lowBlock = LowMC2<>::block;
        blocksPerRow = oc::divCeil(bytesPerRow, sizeof(LowMC2<>::block));
        xEncrypted_.resize(n * blocksPerRow * sizeof(LowMC2<>::block));
        xEncrypted = span<LowMC2<>::block>((LowMC2<>::block*)xEncrypted_.data(),blocksPerRow * n); 

        MC_AWAIT(chl.recv(xEncrypted_));

        indexMatrix.resize(n * blocksPerRow);
        xPermuted.resize(n * blocksPerRow);

        for (u64 i = 0; i < n; ++i)
        {
            std::vector<LowMC2<>::block>::iterator dst, idx;
            span<LowMC2<>::block>::iterator src;
            u64 srcIdx;
            auto counterMode = i * blocksPerRow;
            auto pi_i = pi[i] * blocksPerRow;
            if (op == PermOp::Regular)
            {
                dst = xPermuted.begin() + counterMode;
                idx = indexMatrix.begin() + counterMode;
                src = xEncrypted.begin() + pi_i;
                srcIdx = pi_i;
            }
            else
            {
                dst = xPermuted.begin() + pi_i;
                idx = indexMatrix.begin() + pi_i;
                src = xEncrypted.begin() + counterMode;
                srcIdx = counterMode;
            }

            std::copy(src, src + blocksPerRow, dst);
            std::iota(idx, idx + blocksPerRow, srcIdx);
        }

        gmw1.init(n * blocksPerRow, mLowMcCir());

        // Setting the permuted indexes (since we are using the counter mode)
        gmw1.setInput(0, oc::MatrixView<u8>((u8*)indexMatrix.data(), indexMatrix.size(), sizeof(lowBlock)));

        // Setting the permuted vector
        gmw1.setInput(1, oc::MatrixView<u8>((u8*)xPermuted.data(), xPermuted.size(), sizeof(lowBlock)));


        for (u8 i = 0; i < mLowMc().roundkeys.size(); i++)
        {
            gmw1.setZeroInput(2 + i);
        }

        MC_AWAIT(gmw1.run(ole, chl, prng));

        if (bytesPerRow % sizeof(LowMC2<>::block) == 0)
        {
            sout.reshape(n * blocksPerRow, sizeof(LowMC2<>::block));
            gmw1.getOutput(0, sout);

            sout.reshape(n, bytesPerRow);
        }
        else
        {
            oc::Matrix<u8> temp(n * blocksPerRow, sizeof(LowMC2<>::block), oc::AllocType::Uninitialized);
            gmw1.getOutput(0, temp);
            temp.reshape(n, blocksPerRow * sizeof(LowMC2<>::block));

            //sout.resize(n, bytesPerRow, oc::AllocType::Uninitialized);
            for (u64 i = 0; i < n; ++i)
            {
                memcpy(sout.data(i), temp.data(i), bytesPerRow);
            }
        }

        MC_END();
    }






    template<typename T>
    macoro::task<> LowMCPerm::apply(
        const Perm& pi,
        PermOp op,
        oc::MatrixView<const T> x2,
        oc::MatrixView<T> sout,
        PRNG& prng,
        coproto::Socket& chl,
        CorGenerator& ole)
    {
        oc::MatrixView<u8> xx((u8*)x2.data(), x2.rows(), x2.cols() * sizeof(T));
        oc::MatrixView<u8> oo((u8*)sout.data(), sout.rows(), sout.cols() * sizeof(T));

        return apply<u8>(pi, op, xx, oo, prng, chl, ole);
    }

    template<>
    inline macoro::task<> LowMCPerm::apply<u8>(
        const Perm& pi,
        PermOp op,
        oc::MatrixView<const u8> x2,
        oc::MatrixView<u8> sout,
        PRNG& prng,
        coproto::Socket& chl,
        CorGenerator& ole)
    {

        MC_BEGIN(macoro::task<>, x2, &pi, &chl, sout, &prng, op, &ole,
            n = u64(x2.rows()),
            bytesPerRow = u64(x2.cols()),
            x2Perm = oc::Matrix<u8>{}
        );

        MC_AWAIT(LowMCPerm::apply(pi, op, sout, prng, chl, ole));
        x2Perm.resize(x2.rows(), x2.cols());

        // Permuting the secret shares x2
        for (u64 i = 0; i < n; ++i)
        {
            if (op == PermOp::Regular)
                memcpy(x2Perm.data(i), x2.data(pi[i]), bytesPerRow);
            else
                memcpy(x2Perm.data(pi[i]), x2.data(i), bytesPerRow);
        }

        for (u64 i = 0; i < sout.rows(); ++i)
        {
            for (u64 j = 0; j < sout.cols(); j++)
            {
                // sout combined with x Permuted
                sout(i, j) = sout(i, j) ^ x2Perm(i, j);
            }
        }

        MC_END();
    }
}