#pragma once

#include "Permutation.h"
#include "coproto/Socket/LocalAsyncSock.h"
#include "cryptoTools/Common/Matrix.h"
#include "coproto/coproto.h"

#include "secure-join/CorGenerator/CorGenerator.h"

// using coproto::LocalAsyncSocket;


namespace secJoin
{

    class InsecurePerm
    {
    public:
        template<typename T>
        static macoro::task<> apply(
            oc::MatrixView<const T> x1,
            oc::MatrixView<T> sout,
            PRNG& prng,
            coproto::Socket& chl);

        template<typename T>
        static macoro::task<> apply(
            const Perm& pi,
            PermOp op,
            oc::MatrixView<const T> x2,
            oc::MatrixView<T> sout,
            PRNG& prng,
            coproto::Socket& chl);

        template<typename T>
        static macoro::task<> apply(
            const Perm& pi,
            PermOp op,
            oc::MatrixView<T> sout,
            PRNG& prng,
            coproto::Socket& chl);
    };



    template<typename T>
    macoro::task<> InsecurePerm::apply(
        oc::MatrixView<const T> x1,
        oc::MatrixView<T> sout,
        PRNG& prng,
        coproto::Socket& chl)
    {
        return apply<u8>(matrixCast<u8>(x1), matrixCast<u8>(sout), prng, chl);
    }

    template<>
    inline macoro::task<> InsecurePerm::apply<u8>(
        oc::MatrixView<const u8> x1,
        oc::MatrixView<u8> sout,
        PRNG&,
        coproto::Socket& chl)
    {
        MC_BEGIN(macoro::task<>, x1, &chl, sout
        );

        if (x1.rows() != sout.rows() ||
            x1.cols() != sout.cols())
            throw RTE_LOC;

        MC_AWAIT(chl.send(oc::Matrix<u8>(oc::MatrixView<u8>((u8*)x1.data(), x1.rows(), x1.cols()))));

        for (u64 i = 0; i < sout.size(); ++i)
            sout(i) = (0xcc ^ i) + i * 3;
        MC_END();
    }




    template<typename T>
    macoro::task<> InsecurePerm::apply(
        const Perm& pi,
        PermOp op,
        oc::MatrixView<T> sout,
        PRNG& prng,
        coproto::Socket& chl)
    {
        return apply<u8>(pi, op, matrixCast<u8>(sout), prng, chl);
    }

    template<>
    inline macoro::task<> InsecurePerm::apply<u8>(
        const Perm& pi,
        PermOp op,
        oc::MatrixView<u8> sout,
        PRNG&,
        coproto::Socket& chl)
    {
        MC_BEGIN(macoro::task<>, &pi, &chl, sout,  op,
            o = oc::Matrix<u8>{}
        );
        if (sout.rows() != pi.size())
            throw RTE_LOC;

        o.resize(pi.size(), sout.cols());
        MC_AWAIT(chl.recv(o));

        pi.apply<u8>(o, sout, op);

        for (u64 i = 0; i < sout.size(); ++i)
            sout(i) ^= (0xcc ^ i) + i * 3;

        MC_END();
    }






    //template<typename T>
    //macoro::task<> InsecurePerm::apply(
    //    const Perm& pi,
    //    PermOp op,
    //    oc::MatrixView<const T> x2,
    //    oc::MatrixView<T> sout,
    //    PRNG& prng,
    //    coproto::Socket& chl)
    //{
    //    return apply<u8>(pi, op, matrixCast<u8>(x2), matrixCast<u8>(sout), prng, chl, invPerm);
    //}

    template<>
    inline macoro::task<> InsecurePerm::apply<u8>(
        const Perm& pi,
        PermOp op,
        oc::MatrixView<const u8> x2,
        oc::MatrixView<u8> sout,
        PRNG& prng,
        coproto::Socket& chl)
    {

        MC_BEGIN(macoro::task<>, x2, &pi, &chl, sout, &prng, op,
            n = u64(x2.rows()),
            bytesPerRow = u64(x2.cols()),
            x2Perm = oc::Matrix<u8>{}
        );

        MC_AWAIT(InsecurePerm::apply<u8>(pi, op, sout, prng, chl));
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