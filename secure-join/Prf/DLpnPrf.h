#pragma once
#include "secure-join/config.h"
#include "secure-join/Defines.h"
#include "secure-join/Prf/DarkMatter22Prf.h"
#include "secure-join/CorGenerator/CorGenerator.h"

#include "cryptoTools/Common/BitIterator.h"
#include <bitset>
#include "libOTe/Tools/Tools.h"
#include "macoro/optional.h"

namespace secJoin
{
    
    template<typename T>
    int bit(T& x, u64 i)
    {
        return  *oc::BitIterator((u8*)&x, i);
    }
    template<typename T>
    int bit2(T& x, u64 i)
    {
        return  *oc::BitIterator((u8*)&x, i * 2) + 2 * *oc::BitIterator((u8*)&x, i * 2 + 1);;
    }
    void mod3BitDecompostion(oc::MatrixView<u16> u, oc::MatrixView<oc::block> u0, oc::MatrixView<oc::block> u1);

    template<int keySize>
    void compressH2(
        oc::Matrix<u16>&& mH,
        oc::Matrix<u16>& mU
    );

    void compressB(
        oc::MatrixView<oc::block> v,
        span<oc::block> y
    );

    //void mod3Add(span<oc::block> a, span<oc::block> b, span<oc::block> c);

    void mod3Add(
        span<oc::block> z1, span<oc::block> z0,
        span<oc::block> x1, span<oc::block> x0,
        span<oc::block> y1, span<oc::block> y0);

    void mod3Add(
        span<oc::block> z1, span<oc::block> z0,
        span<oc::block> x1, span<oc::block> x0,
        span<oc::block> y0);

    class DLpnPrf
    {
    public:
        static const std::array<block256, 128> mB, mBShuffled;
        static const std::array<std::array<u8, 256>, 128> mBExpanded;

        // the bit count of the key
        static constexpr auto KeySize = 128;

        // the bit count of the middle layer
        static constexpr auto MidSize = 256;

        // the bit count of output layer
        static constexpr auto OutSize = 128;

        std::array<oc::block, KeySize / 128> mKey;


        // set the key
        void setKey(oc::block k);

        // compute y = F(k,x)
        void eval(span<oc::block> x,span<oc::block> y)
        {
            for (u64 i = 0; i < x.size(); ++i)
                y[i] = eval(x[i]);
        }

        // compute y = F(k,x)
        oc::block eval(oc::block x);


        void compressH(const std::array<u16, KeySize>& hj, block256m3& uj);
        
        static oc::block compress(block256& w);

        static oc::block shuffledCompress(block256& w);

        static oc::block compress(block256& w, const std::array<block256, 128>& B);
    };


    class DLpnPrfSender : public oc::TimerAdapter
    {
    public:
        static constexpr auto mDebug = false;
        static constexpr auto StepSize = 32;

        // The key OTs, one for each bit of the key mPrf.mKey
        std::vector<oc::PRNG> mKeyOTs;

        // The underlaying PRF
        DLpnPrf mPrf;

        // The number of input we will have.
        u64 mInputSize = 0;

        // Generate the key when we run the protocol.
        bool mDoKeyGen = false;

        // Has the key OTs been set.
        bool mHasKeyOts = false;

        // Has the preprocessing been performed.
        bool mHasPrepro = false;

        // The Ole request that will be used for the mod2 operation
        BinOleRequest mOleReq;

        // The base ot request that will be used for the key
        OtRecvRequest mKeyReq;

        DLpnPrfSender() = default;
        DLpnPrfSender(const DLpnPrfSender&) = default;
        DLpnPrfSender(DLpnPrfSender&&) noexcept = default;
        DLpnPrfSender& operator=(const DLpnPrfSender&) = default;
        DLpnPrfSender& operator=(DLpnPrfSender&&) noexcept = default;

        // initialize the protocol to perform inputSize prf evals.
        // set keyGen if you explicitly want to perform (or not) the 
        // key generation. default = perform if not already set.
        void init(u64 inputSize, macoro::optional<bool> keyGen = {})
        {
            mInputSize = inputSize;
            mDoKeyGen = keyGen ? *keyGen : mHasKeyOts == false;
        }

        // clear the state. Removes any key that is set can cancels the prepro (if any).
        void clear()
        {
            mOleReq.clear();
            mKeyReq.clear();
            mKeyOTs.clear();
            mDoKeyGen = false;
            mHasKeyOts = false;
            mHasPrepro = false;
            mInputSize = 0;
            memset(mPrf.mKey.data(), 0, sizeof(mPrf.mKey));
        }

        // return true if correlated randomness has been requested.
        bool hasRequest()
        {
            return mOleReq.size() > 0;
        }

        // request correlated randomness. init must be called first.
        void request(CorGenerator& ole)
        {
            if(mInputSize == 0)
                throw std::runtime_error("DLpnPrfSender::init must be called first. " LOCATION);

            auto numOle = oc::roundUpTo(mInputSize, 128) * DLpnPrf::MidSize * 2;
            mOleReq = ole.binOleRequest(numOle);
            if (mDoKeyGen)
            {
                mKeyReq = ole.otRecvRequest(DLpnPrf::KeySize);
                mDoKeyGen = false;
                mHasKeyOts = true;
            }
        }

        // perform the correlated randomness generation. 
        macoro::task<> preprocess()
        {
            if (mOleReq.size() == 0)
                throw std::runtime_error("DLpnPrfReceiver::preprocess() was called without calling request. " LOCATION);

            mHasPrepro = true;
            if (mKeyReq.size())
            {
                MC_BEGIN(macoro::task<>, this);
                MC_AWAIT(macoro::when_all_ready(mOleReq.start(), mKeyReq.start()));
                MC_END();
            }
            else
                return mOleReq.start();
        }

        // returns true if the key has been set (or is being generated).
        bool hasKeyOts() const {return mHasKeyOts;}

        // explicitly set the key and key OTs.
        void setKeyOts(oc::block k, span<oc::block> ots);

        // return the key that is currently set.
        oc::block getKey() const {
            if (mHasKeyOts == false)
                throw RTE_LOC;
            return mPrf.mKey;
        }

        // returns a key derivation of the key OTs. This can safely be used by another instance.
        std::vector<oc::block> getKeyOts() const
        {

            if (mHasKeyOts == false)
                throw RTE_LOC;
            std::vector<oc::block> r(mKeyOTs.size());
            for (u64 i = 0; i < r.size(); ++i)
            {
                r[i] = mKeyOTs[i].getSeed();
            }
            return r;
        }

        // Run the prf protocol and write the result to y. If correlated randomness has
        // not been generated then it will be in the protocol.
        coproto::task<> evaluate(
            span<oc::block> y,
            coproto::Socket& sock,
            oc::PRNG& _,
            CorGenerator& gen);

        // Run the prf protocol and write the result to y. Requires that correlated 
        // randomness has already been requested using the request() function.
        coproto::task<> evaluate(
            span<oc::block> y,
            coproto::Socket& sock,
            oc::PRNG& _);

        // re-randomize the OTs seeds with the tweak.
        void tweakKeyOts(oc::block tweak)
        {
            if (!mKeyOTs.size())
                throw RTE_LOC;
            oc::AES aes(tweak);
            for (u64 i = 0; i < mKeyOTs.size(); ++i)
            {
                mKeyOTs[i].SetSeed(aes.hashBlock(mKeyOTs[i].getSeed()));
            }
        }

        // the mod 2 subprotocol.
        macoro::task<> mod2(
            oc::MatrixView<oc::block> u0,
            oc::MatrixView<oc::block> u1,
            oc::MatrixView<oc::block> out,
            coproto::Socket& sock);

    };



    class DLpnPrfReceiver : public oc::TimerAdapter
    {
    public:
        static constexpr auto mDebug = DLpnPrfSender::mDebug;
        static constexpr auto StepSize = 32;

        // base OTs, where the sender has the OT msg based on the bits of their key
        std::vector<std::array<oc::PRNG, 2>> mKeyOTs;

        // The number of input we will have.
        u64 mInputSize = 0;

        // Generate the key when we run the protocol.
        bool mDoKeyGen = false;

        // have the OTs been set.
        bool mHasKeyOts = false;

        // Have we performed preprocessing
        bool mHasPrepro = false;

        // The Ole request that will be used for the mod2 operation
        BinOleRequest mOleReq;

        // The base ot request that will be used for the key
        OtSendRequest mKeyReq;

        DLpnPrfReceiver() = default;
        DLpnPrfReceiver(const DLpnPrfReceiver&) = default;
        DLpnPrfReceiver(DLpnPrfReceiver&&)  = default;
        DLpnPrfReceiver& operator=(const DLpnPrfReceiver&) = default;
        DLpnPrfReceiver& operator=(DLpnPrfReceiver&&) noexcept = default;

        // returns true if we have key OTs set.
        bool hasKeyOts() const {return mHasKeyOts; }

        // returns true if correlated randomness has been requested.
        bool hasRequest() const { return mOleReq.size() > 0; }

        // clears any internal state.
        void clear()
        {
            mOleReq.clear();
            mKeyReq.clear();
            mKeyOTs.clear();
            mDoKeyGen = false;
            mHasKeyOts = false;
            mHasPrepro = false;
            mInputSize = 0;
        }

        // initialize the protocol to perform inputSize prf evals.
        // set keyGen if you explicitly want to perform (or not) the 
        // key generation. default = perform if not already set.
        void init(u64 size, macoro::optional<bool> keyGen = {})
        { 
            mInputSize = size;
            mDoKeyGen = keyGen ? *keyGen : mHasKeyOts == false;
        }

        // request the required correlated randomness. 
        void request(CorGenerator& ole)
        {
            if (mInputSize == 0)
                throw std::runtime_error("DLpnPrfSender::init must be called first. " LOCATION);

            auto numOle = oc::roundUpTo(mInputSize, 128) * DLpnPrf::MidSize * 2;
            mOleReq = ole.binOleRequest(numOle);
            if (mDoKeyGen)
            {
                mKeyReq = ole.otSendRequest(DLpnPrf::KeySize);
                mDoKeyGen = false;
                mHasKeyOts = true;
            }
        }

        // Perform the preprocessing for the correlated randomness and key gen (if requested).
        macoro::task<> preprocess()
        {
            if (mOleReq.size() == 0)
                throw std::runtime_error("DLpnPrfReceiver::preprocess() was called without calling request. " LOCATION);

            mHasPrepro = true;
            if (mKeyReq.size())
            {
                MC_BEGIN(macoro::task<>, this);
                MC_AWAIT(macoro::when_all_ready(mOleReq.start(), mKeyReq.start()));
                MC_END();
            }
            else
                return mOleReq.start();
        }

        // explicitly set the OTs for the key.
        void setKeyOts(span<std::array<oc::block, 2>> ots);

        // returns a key derivation of the key OTs. This can safely be used by another instance.
        std::vector<std::array<oc::block, 2>> getKeyOts() const
        {
            if (mHasKeyOts == false)
                throw RTE_LOC;
            std::vector<std::array<oc::block, 2>> r(mKeyOTs.size());
            for (u64 i = 0; i < r.size(); ++i)
            {
                r[i][0] = mKeyOTs[i][0].getSeed();
                r[i][1] = mKeyOTs[i][1].getSeed();
            }
            return r;
        }

        // re-randomize the OTs seeds with the tweak.
        void tweakKeyOts(oc::block tweak)
        {
            if (!mKeyOTs.size())
                throw RTE_LOC;
            oc::AES aes(tweak);
            for (u64 i = 0; i < mKeyOTs.size(); ++i)
            {
                mKeyOTs[i][0].SetSeed(aes.hashBlock(mKeyOTs[i][0].getSeed()));
                mKeyOTs[i][1].SetSeed(aes.hashBlock(mKeyOTs[i][1].getSeed()));
            }
        }

        // Run the prf protocol and write the result to y. If correlated randomness has
        // not been generated then it will be in the protocol.
        coproto::task<> evaluate(
            span<oc::block> x,
            span<oc::block> y,
            coproto::Socket& sock,
            oc::PRNG&,
            CorGenerator& gen);

        // Run the prf protocol and write the result to y. Requires that correlated 
        // randomness has already been requested using the request() function.
        coproto::task<> evaluate(
            span<oc::block> x,
            span<oc::block> y,
            coproto::Socket& sock,
            oc::PRNG&);

        // the mod 2 subprotocol.
        macoro::task<> mod2(
            oc::MatrixView<oc::block> u0,
            oc::MatrixView<oc::block> u1,
            oc::MatrixView<oc::block> out,
            coproto::Socket& sock);
    };
}
