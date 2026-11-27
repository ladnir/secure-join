
#pragma once
#include "secure-join/config.h"
#include "secure-join/Defines.h"
#include "secure-join/CorGenerator/CorGenerator.h"

#include "macoro/optional.h"
#include "cryptoTools/Crypto/PRNG.h"
#include "AltModPrf.h"

namespace secJoin
{
    // Alt Mod Prf subprotocol to multiply either an F2 or F3 sharing x
    // by a fixed F2 key k. The protocol is designed to do this
    // for many x. that is, for i = 1,...,n; compute a F3 sharing
    //    y[i] = x[i] . k
    // where `.` denote componentwise multiplication. This is done by performing
    // a base OT for each bit of k and then performing an OT derandomiation
    // to either get a sharing of zero or the bit of x[i].
    //
    // The sender party holds the input x while the receiver party holds the key k.
    // The protocol also supports allowing the key to be secret shared while x remains 
    // the input of the sender.
    struct AltModKeyMultSender
    {
        static constexpr auto StepSize = 32;

        // base OTs, where the sender has the OT msg based on the bits of their key
        std::vector<std::array<PRNG, 2>> mKeySendOTs;

        // a share of the key. This can be disengaged if the other party 
        // knows the key in full.
        std::optional<AltModPrf::BinaryKey> mOptionalKeyShare;

        // The base ot request that will be used for the key
        OtSendRequest mSendKeyReq;

        // performs additional debugging if true. Insecure.
        bool mDebug = false;

        // debugging values
        oc::Matrix<oc::block> mDebugXk0, mDebugXk1;

        // intializes this protocol and generate OTs for the key
        // if required. This function allows setting the OTs corresponding to the
        // other party's key. Additionally, if they key is shared
        // then the local key share is provided.
        void init(CorGenerator& ole,
            std::optional<AltModPrf::BinaryKey> keyShare = {},
            span<const std::array<block, 2>> keyOts = {})
        {
            if (keyOts.size() == 0)
            {
                if (keyShare.has_value())
                    throw RTE_LOC;
                mOptionalKeyShare.reset();
                mSendKeyReq = ole.sendOtRequest(AltModPrf::KeySize);
            }
            else
            {
                setKeyOts(keyShare, keyOts);
            }
        }


        // invokes the actual multipliction protocol for computing
        //     x[i] . k
        // If x if F2 shared, xMsb should be empty. The result is always 
        // an F3 sharing.
        macoro::task<> mult(
            oc::MatrixView<const block> xLsb,
            oc::MatrixView<const block> xMsb,
            oc::Matrix<block>& xkLsb,
            oc::Matrix<block>& xkMsb,
            coproto::Socket& sock);

        // This function allows setting the OTs corresponding to the
        // other party's key. Additionally, if they key is shared
        // then the local key share is provided.
        void setKeyOts(
            std::optional<AltModPrf::BinaryKey> keyShared,
            span<const std::array<block, 2>> ots)
        {
            if (ots.size() != AltModPrf::KeySize)
                throw RTE_LOC;

            mOptionalKeyShare = keyShared;

            mKeySendOTs.resize(AltModPrf::KeySize);
            for (u64 i = 0; i < AltModPrf::KeySize; ++i)
            {
                mKeySendOTs[i][0].SetSeed(ots[i][0]);
                mKeySendOTs[i][1].SetSeed(ots[i][1]);
            }
        }

        // clears internal state.
        void clear()
        {
            mKeySendOTs.clear();
            mOptionalKeyShare = {};
            mSendKeyReq.clear();
        }

        // starts the preprocessing for this protocol (if any).
        void preprocess()
        {
            if (mSendKeyReq.size())
                mSendKeyReq.start();
        }

    };

    // Alt Mod Prf subprotocol to multiply either an F2 or F3 sharing x
    // by a fixed F2 key k. The protocol is designed to do this
    // for many x. that is, for i = 1,...,n; compute a F3 sharing
    //    y[i] = x[i] . k
    // where `.` denote componentwise multiplication. This is done by performing
    // a base OT for each bit of k and then performing an OT derandomiation
    // to either get a sharing of zero or the bit of x[i].
    //
    // The sender party holds the input x while the receiver party holds the key k.
    // The protocol also supports allowing the key to be secret shared while x remains 
    // the input of the sender.
    struct AltModKeyMultReceiver
    {
        static constexpr auto StepSize = 32;

        // The key OTs, one for each bit of the key mKey
        std::vector<PRNG> mKeyRecvOTs;

        // the key that is used to multiply with. This should
        // also be the choice bits of mKeyRecvOTs.
        AltModPrf::BinaryKey mKey;

        // The base ot request that will be used for the key
        OtRecvRequest mRecvKeyReq;

        macoro::task<> receiveKey();

        // performs additional debugging if true. Insecure.
        bool mDebug = false;

        // debug values.
        oc::Matrix<oc::block> mDebugXk0, mDebugXk1;

        // intializes this protocol and generate OTs for the key
        // if required. This function allows setting the OTs corresponding to the
        // other party's key. If OTs are not provided, they key is sampled randomly.
        void init(CorGenerator& ole,
            std::optional<AltModPrf::BinaryKey> key,
            span<const block> keyOts)
        {
            if (key.has_value() ^ bool(keyOts.size()))
                throw RTE_LOC;

            if (keyOts.size() == 0)
            {
                mRecvKeyReq = ole.recvOtRequest(AltModPrf::KeySize);
            }
            else
            {
                setKeyOts(*key, keyOts);
            }
        }

        // invokes the actual multipliction protocol for computing
        //     x[i] . k
        // If x if F2 shared, xMsb should be empty. The result is always 
        // an F3 sharing.
        macoro::task<> mult(
            u64 n,
            oc::Matrix<block>& xk0,
            oc::Matrix<block>& xk1,
            coproto::Socket& sock);

        // sets the key to be used and the OTs corresponding to it.
        // the choice bits of `ots` must be `k`.
        void setKeyOts(
            AltModPrf::BinaryKey k,
            span<const block> ots)
        {
            if (ots.size() != AltModPrf::KeySize)
                throw RTE_LOC;
            mKey = k;

            mKeyRecvOTs.resize(AltModPrf::KeySize);
            for (u64 i = 0; i < AltModPrf::KeySize; ++i)
            {
                mKeyRecvOTs[i].SetSeed(ots[i]);
            }

        }

        // clears the internal state.
        void clear()
        {
            mKeyRecvOTs.clear();
            mRecvKeyReq.clear();
            osuCrypto::setBytes(mKey, 0);
        }

        // starts the preprocessing, if any.
        void preprocess()
        {
            if (mRecvKeyReq.size())
                mRecvKeyReq.start();
        }
    };
}