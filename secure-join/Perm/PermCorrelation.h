#pragma once
#include "secure-join/Defines.h"

#include "Permutation.h"
#include "macoro/task.h"
#include "cryptoTools/Common/Matrix.h"
#include "coproto/Socket/Socket.h"
#include "cryptoTools/Crypto/PRNG.h"

namespace secJoin
{
    // A permutation correlation.
    //
    // The sender holds a permutation pi and a random vector delta.
    // The receiver holds two random vectors A, B such that
    // 
    // mDelta ^ mB = mPi(mA)
    // 
    // These can be consumed to permute chosen data M by pi.
    //
    struct PermCorSender
    {
        // The permutation for the correlations.
        Perm mPerm;

        // The shares hold by the sender. mDelta ^ mB = mPi(mA)
        oc::Matrix<oc::block> mDelta;

        // The number of bytes that have been used to permute the user's data. 
        // This allows us to perform setup one and permute multiple inputs shares.
        u64 mByteOffset = 0;

        u64 size() const { return mDelta.rows(); }

        u64 corSize() const { return mDelta.cols() * sizeof(block) - mByteOffset; }

        // returns true if there is enough correlated randomness to permute input elements of numBytes bytes.
        bool hasSetup(u64 numBytes) const { return corSize() >= numBytes; }

        // permute a remote x by our permutation and get shares as output. 
        // the permutation must have been set and correlated randomness requested.
        template <typename T>
        macoro::task<> apply(
            PermOp op,
            oc::MatrixView<T> sout,
            coproto::Socket& chl);

        // permute a secret shared input x by our pi and get shares as output
        // the permutation must have been set and correlated randomness requested.
        template <typename T>
        macoro::task<> apply(
            PermOp op,
            oc::MatrixView<const T> in,
            oc::MatrixView<T> sout,
            coproto::Socket& chl
        );

        // change the permutation correlation to
        // hold for `newPerm`. This will reveal the difference
        // between the `newPerm` and the old perm.
        // Only do this if the old perm was random.
        macoro::task<> derandomize(
            Perm newPerm,
            coproto::Socket& chl)
        {
            MC_BEGIN(macoro::task<>, this, 
                d = std::move(newPerm), &chl,
                delta = Perm{});

            // delta = newPerm^-1 o mPerm
            // they are going to update their correlation using delta
            // to translate it to a correlation of pi.
            delta = d.inverse().compose(mPerm);
            MC_AWAIT(chl.send(std::move(delta.mPi)));

            mPerm = std::move(d);

            MC_END();
        }

        // for debugging, check that the correlated randomness is correct.
        macoro::task<> validate(coproto::Socket& sock);

    };


    struct PermCorReceiver
    {

        // The shares held by the receiver. mDelta ^ mB = mPi(mA)
        oc::Matrix<oc::block> mA, mB;

        // The number of bytes that have been used to permute the user's data. 
        // This allows us to perform setup one and permute multiple inputs shares.
        u64 mByteOffset = 0;

        u64 size() const { return mA.rows(); }

        u64 corSize() const { return mA.cols() * sizeof(block) - mByteOffset; }

        // returns true if there is enough correlated randomness to permute input elements of numBytes bytes.
        bool hasSetup(u64 numBytes) const { return corSize() >= numBytes; }

        // Receiver apply: permute a secret shared input x by the other party's pi and get shares as output
        template <typename T>
        macoro::task<> apply(
            PermOp op,
            oc::MatrixView<const T> in,
            oc::MatrixView<T> sout,
            coproto::Socket& chl
        );

        // For debugging. Check that the correlated randomness is correct.
        macoro::task<> validate(coproto::Socket& sock);

        // change the permutation correlation to
        // hold for `newPerm`. This will reveal the difference
        // between the `newPerm` and the old perm.
        // Only do this if the old perm was random.
        macoro::task<> derandomize(coproto::Socket& chl)
        {
            MC_BEGIN(macoro::task<>, this, &chl,
                delta = Perm{});

            // we current have the correlation 
            // 
            //          mDelta ^ mB  = pre(mA)
            //   pre^-1(mDelta ^ mB) = mA
            // 
            // if we multiply both sides by (pi^-1 o pre) we get
            // 
            //   (pi^-1 o pre)( pre^-1(mDelta ^ mB)) = (pi^-1 o pre) (mA)
            //   (pi^-1 o pre o pre^-1)(mDelta ^ mB)) = (pi^-1 o pre) (mA)
            //   (pi^-1)(mDelta ^ mB)) = (pi^-1 o pre)(mA)
            //   mDelta ^ mB = pi((pi^-1 o pre)(mA))
            //   mDelta ^ mB = pi(mA')
            // 
            // where mA' = (pi^-1 o pre)(mA)
            //           = delta(mA)
            delta.mPi.resize(size());
            MC_AWAIT(chl.recv(delta.mPi));

            {
                oc::Matrix<oc::block> AA(mA.rows(), mA.cols());
                delta.apply<oc::block>(mA, AA);
                std::swap(mA, AA);
            }
            MC_END();
        }

    };



    inline void genPerm(Perm p, PermCorSender& s, PermCorReceiver& r, u64 sizeBytes, PRNG& prng)
    {
        // mDelta ^ mB = mPi(mA)
        s.mPerm = std::move(p);
        r.mA.resize(s.mPerm.size(), divCeil(sizeBytes, sizeof(block)));
        r.mB.resize(s.mPerm.size(), divCeil(sizeBytes, sizeof(block)));
        s.mDelta.resize(s.mPerm.size(), divCeil(sizeBytes, sizeof(block)));
        s.mByteOffset = 0;
        r.mByteOffset = 0;

        prng.get<block>(s.mDelta);
        prng.get<block>(r.mB);

        for (u64 i = 0; i < s.mPerm.size(); ++i)
        {
            auto pi = s.mPerm[i];
            for (u64 j = 0; j < r.mA.cols(); ++j)
                r.mA(pi, j) = s.mDelta(i, j) ^ r.mB(i, j);
        }
    }


    inline void validate(const PermCorSender& s, const PermCorReceiver& r)
    {

        for (u64 i = 0; i < s.mPerm.size(); ++i)
        {
            auto pi = s.mPerm[i];
            for (u64 j = 0; j < r.mA.cols(); ++j)
            {
                if (r.mA(pi, j) != (s.mDelta(i, j) ^ r.mB(i, j)))
                    throw RTE_LOC;;
            }
        }
    }
}