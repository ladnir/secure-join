#pragma once
// � 2022 Visa.
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


#include "secure-join/Defines.h"

#include <vector>
#include <libOTe/TwoChooseOne/Silent/SilentOtExtSender.h>
#include <libOTe/TwoChooseOne/Silent/SilentOtExtReceiver.h>
#include "macoro/thread_pool.h"
#include "macoro/channel_spsc.h"
#include "macoro/manual_reset_event.h"

namespace secJoin
{
    class OleGenerator;
    using SessionID = coproto::SessionID;
    struct OtSend
    {
        OtSend() = default;
        OtSend(const OtSend&) = delete;
        OtSend& operator=(const OtSend&) = delete;
        OtSend(OtSend&&) = default;
        OtSend& operator=(OtSend&&) = default;

        oc::AlignedUnVector<std::array<oc::block, 2>> mMsg;
        u64 size() const
        {
            return mMsg.size();
        }
    };

    struct OtRecv
    {

        OtRecv() = default;
        OtRecv(const OtRecv&) = delete;
        OtRecv& operator=(const OtRecv&) = delete;
        OtRecv(OtRecv&&) = default;
        OtRecv& operator=(OtRecv&&) = default;

        oc::BitVector mChoice;
        oc::AlignedUnVector<oc::block> mMsg;

        u64 size() const
        {
            return mMsg.size();
        }
    };

    struct BinOle
    {

        BinOle() = default;
        BinOle(const BinOle&) = delete;
        BinOle& operator=(const BinOle&) = delete;
        BinOle(BinOle&&) = default;
        BinOle& operator=(BinOle&&) = default;

        oc::AlignedUnVector<oc::block> mMult, mAdd;
        u64 size() const
        {
            return mMult.size() * 128;
        }
    };

    struct ArithTriple {


        //ArithTriple() = default;
        //ArithTriple(const ArithTriple&) = delete;
        //ArithTriple& operator=(const ArithTriple&) = delete;
        //ArithTriple(ArithTriple&&) = default;
        //ArithTriple& operator=(ArithTriple&&) = default;

        u64 mBitCount = 0;
        oc::AlignedUnVector<u32> mA, mB, mC;

        u64 size() { return mA.size(); }
    };


    struct CorRequest
    {

        template< typename T>
        struct Req : T
        {
            std::shared_ptr<macoro::mpsc::channel<std::pair<u64, T>>> mQueue;
        };
        using OtRecvReq = Req<OtRecv>;
        using OtSendReq = Req<OtSend>;
        using BinOleReq = Req<BinOle>;
        using ArithTripleReq = Req<ArithTriple>;

        CorRequest()
            : mOp(BinOleReq{})
        {}
        CorRequest(const CorRequest&) = delete;
        CorRequest(CorRequest&&) = default;
        CorRequest& operator=(CorRequest&&) = default;

        CorRequest(SessionID sid, u64 n, u64 idx, macoro::variant<BinOleReq, OtRecvReq, OtSendReq, ArithTripleReq> op)
            : mSessionID(sid)
            , mN(n)
            , mSequence(idx)
            , mOp(std::move(op))
        {}

        SessionID mSessionID;
        u64 mN = 0;
        u64 mSequence = -1;
        macoro::variant<BinOleReq, OtRecvReq, OtSendReq, ArithTripleReq> mOp;

        u64 sizeBytes()
        {
            return 1 + sizeof(SessionID) + sizeof(u64) + sizeof(u64) + 1;
        }

        void toBytes(span<u8> bytes)
        {
            if (bytes.size() < sizeBytes())
                throw RTE_LOC;
            bytes[0] = mOp.index(); bytes = bytes.subspan(1);
            memcpy(&bytes[0], &mSessionID, sizeof(SessionID)); bytes = bytes.subspan(sizeof(SessionID));
            memcpy(&bytes[0], &mN, sizeof(u64)); bytes = bytes.subspan(sizeof(u64));
            memcpy(&bytes[0], &mSequence, sizeof(u64)); bytes = bytes.subspan(sizeof(u64));
            if (mOp.index() == 3)
                throw RTE_LOC;
                //bytes[0] = mOp.get<3>().mBitCount;
            else
                bytes[0] = 0;
        }


        void fromBytes(span<u8> bytes)
        {
            if (bytes.size() < sizeBytes())
                throw RTE_LOC;

            auto idx = mOp.index(); bytes = bytes.subspan(1);
            memcpy(&mSessionID, &bytes[0], sizeof(SessionID)); bytes = bytes.subspan(sizeof(SessionID));
            memcpy(&mN, &bytes[0], sizeof(u64)); bytes = bytes.subspan(sizeof(u64));
            memcpy(&mSequence, &bytes[0], sizeof(u64)); bytes = bytes.subspan(sizeof(u64));

            switch (idx)
            {
            case 0:
                mOp.emplace<0>();
                break;
            case 1:
                mOp.emplace<1>();
                break;
            case 2:
                mOp.emplace<2>();
                break;
            case 3:
                mOp.emplace<3>();
                throw RTE_LOC;
                //mOp.get<3>().mBitCount = bytes[0];
                break;
            default:
                throw RTE_LOC;
                break;
            }
        }
    };
    template<typename T>
    struct Request
    {
        Request() = default;
        Request(const Request&) = delete;
        Request(Request&& o) noexcept
            : mSessionID(std::exchange(o.mSessionID, SessionID::root()))
            , mSize(std::exchange(o.mSize, 0))
            , mIdx(std::exchange(o.mIdx, 0))
            , mCorrelations(std::move(o.mCorrelations))
            , mRemReq(std::move(o.mRemReq))
            , mGen(std::exchange(o.mGen, nullptr))
            , mChl(std::move(o.mChl))
        {}

        Request(SessionID sessionID, u64 size, u64 numChunks, std::vector<CorRequest>&& rem, OleGenerator* g, std::shared_ptr<macoro::mpsc::channel<std::pair<u64, T>>> chl)
            : mSessionID(sessionID)
            , mSize(size)
            , mCorrelations(numChunks)
            , mRemReq(std::move(rem))
            , mGen(g)
            , mChl(std::move(chl))
        {}

        Request& operator=(Request&& o)
        {
            mSessionID = std::exchange(o.mSessionID, SessionID::root());
            mSize = std::exchange(o.mSize, 0);
            mIdx = std::exchange(o.mIdx, 0);
            mCorrelations = std::move(o.mCorrelations);
            mRemReq = std::move(o.mRemReq);
            mGen = std::exchange(o.mGen, nullptr);
            mChl = std::move(o.mChl);
            return *this;
        }

        using value_type = T;
        SessionID mSessionID = SessionID::root();
        u64 mSize = 0, mIdx = 0;
        std::vector<std::unique_ptr<T>> mCorrelations;
        std::vector<CorRequest> mRemReq;
        OleGenerator* mGen = nullptr;
        std::shared_ptr<macoro::mpsc::channel<std::pair<u64, value_type>>> mChl;

        macoro::task<T> get();
    };

    class OleGenerator : public oc::TimerAdapter
    {
    public:
        enum class Role
        {
            Sender,
            Receiver
        };

        //struct Chunk 
        //{
        //    u64 mN;
        //    u64 mIdx;
        //    oc::block mSessionID;

        //    struct BinOle : secJoin::BinOle, CorRequest::BinOleReq {};
        //    struct OtRecv : secJoin::OtRecv, CorRequest::OtRecvReq {};
        //    struct OtSend : secJoin::OtSend, CorRequest::OtSendReq {};
        //    struct ArithTriple : secJoin::ArithTriple, CorRequest::ArithTripleReq {};
        //    macoro::variant<BinOle, OtRecv, OtSend, ArithTriple> mOp;
        //};


        struct Command
        {
            struct Stop {};
            struct ChunkComplete {
                u64 mWorkerId;
                CorRequest mChunk;
            };
            struct GenStopped
            {
                u64 mIdx = ~0ull;
            };
            macoro::variant<Stop, ChunkComplete, GenStopped, CorRequest> mOp;
        };

        struct Gen
        {
            OleGenerator* mParent;
            coproto::Socket mChl;
            std::unique_ptr<oc::SilentOtExtSender> mSender;
            std::unique_ptr<oc::SilentOtExtReceiver> mRecver;
            macoro::eager_task<> mTask;
            oc::PRNG mPrng;
            u64 mIdx = 0;

            oc::Log mLog;

            void log(std::string m)
            {
                std::stringstream ss;
                ss << "[" << std::this_thread::get_id() << "] ";
                ss << m;
                mLog.push(ss.str());
                mParent->log("gen " + std::to_string(mIdx) + ": " + m);
            }

            std::unique_ptr<macoro::spsc::channel<CorRequest>> mInQueue;


            void compressSender(
                span<oc::block> sendMsg, oc::block delta,
                span<oc::block> add,
                span<oc::block> mult
            );

            void compressRecver(
                span<oc::block> recvMsg,
                span<oc::block> add,
                span<oc::block> mult
            );


            void compressSender(
                span<std::array<oc::block, 2>> sendMsg,
                span<oc::block> add,
                span<oc::block> mult
            );

            void compressRecver(
                oc::BitVector& bv,
                span<oc::block> recvMsg,
                span<oc::block> add,
                span<oc::block> mult
            );

            macoro::task<> start();
        };

        bool mFakeGen = false;
        SessionID mCurSessionID = SessionID::root();
        u64 mChunkSize = 0;
        u64 mNumConcurrent = 0;
        u64 mBaseSize = 0;
        bool mStopRequested = false;
        Role mRole = Role::Sender;
        u64 mNumBinOle = 0;

        std::unique_ptr<macoro::mpsc::channel<Command>> mControlQueue;

        std::vector<std::array<oc::block, 2>> mBaseSendOts;
        std::vector<oc::block> mBaseRecvOts;
        macoro::thread_pool* mThreadPool = nullptr;
        coproto::Socket mChl;

        macoro::eager_task<> mBaseTask;

        macoro::eager_task<> mCtrl;
        oc::PRNG mPrng;
        oc::Log mLog;

        std::vector<Gen> mGens;


        bool isStopRequested()
        {
            return false;
        }

        macoro::task<> stop();

        macoro::task<> control();
        //macoro::task<> baseOtProvider(oc::block seed);


        void init(
            Role role,
            macoro::thread_pool& threadPool,
            coproto::Socket chl,
            oc::PRNG& prng)
        {
            init(role, threadPool, std::move(chl), prng, 1, 1 << 18);
        }

        void init(
            Role role,
            macoro::thread_pool& threadPool,
            coproto::Socket chl,
            oc::PRNG& prng,
            u64 numThreads,
            u64 chunkSize);



        void fakeInit(Role role)
        {
            mFakeGen = true;
            mRole = role;
            mThreadPool = nullptr;
            mPrng.SetSeed(oc::block((int)role, 132423));
            //mCurSize = 0;
            mChunkSize = 1 << 16;
            //mReservoirSize = oc::divCeil(1 << 20, mChunkSize);
            mNumConcurrent = 1;
            //mNumChunks = ~0ull;
        }

        //void getBaseOts(Chunk& chunk, CorRequest& ses);

        void fakeFill(u64 m, BinOle& ole, const BinOle&);

        void fakeFill(u64 m, OtRecv& ole, const OtRecv&)
        {
            //oc::PRNG prng(oc::block(mCurSize++));
            ole.mMsg.resize(m);
            ole.mChoice.resize(m);
            memset(ole.mChoice.data(), 0b10101100, ole.mChoice.sizeBytes());

            for (u32 i = 0; i < ole.mMsg.size(); ++i)
            {
                oc::block m0 = std::array<u32, 4>{i, i, i, i};// prng.get();
                oc::block m1 = std::array<u32, 4>{~i, ~i, ~i, ~i};//prng.get();
                ole.mMsg[i] = ole.mChoice[i] ? m1 : m0;
            }
        }

        void fakeFill(u64 m, OtSend& ole, const OtSend&)
        {
            ole.mMsg.resize(m);
            for (u32 i = 0; i < ole.mMsg.size(); ++i)
            {
                ole.mMsg[i][0] = std::array<u32, 4>{i, i, i, i};// prng.get();
                ole.mMsg[i][1] = std::array<u32, 4>{~i, ~i, ~i, ~i};//prng.get();
            }
        }

        void fakeFill(u64 m, ArithTriple& ole, const ArithTriple& at)
        {
            oc::PRNG prng(oc::block(0, 0));
            ole.mBitCount = at.mBitCount;
            ole.mA.resize(m);
            ole.mB.resize(m);
            ole.mC.resize(m);
            u32 mask = (1 << ole.mBitCount) - 1;
            if (!mask)
                mask = -1;
            for (u32 i = 0; i < m; ++i)
            {
                u32 a[2];
                u32 b[2];
                u32 c[2];

                a[0] = prng.get<u32>() & mask;
                b[0] = prng.get<u32>() & mask;
                c[0] = prng.get<u32>() & mask;
                a[1] = prng.get<u32>() & mask;
                b[1] = prng.get<u32>() & mask;
                c[1] = (a[1] * b[1]) & mask;

                a[1] = (a[1] - a[0]) & mask;
                b[1] = (b[1] - b[0]) & mask;
                c[1] = (c[1] - c[0]) & mask;

                ole.mA[i] = a[(int)mRole];
                ole.mB[i] = b[(int)mRole];
                ole.mC[i] = c[(int)mRole];
            }
        }

        SessionID nextSID()
        {
            auto s = mCurSessionID;
            mCurSessionID = mCurSessionID.derive();
            return s;
        }

        template<typename Cor>
        macoro::task<Request<Cor>> request(u64 numCorrelations, Cor&& cor, u64 reservoirSize, SessionID sessionID)
        {

            MC_BEGIN(macoro::task<Request<Cor>>, this, numCorrelations, reservoirSize, sessionID, cor = std::forward<Cor>(cor),
                c = std::shared_ptr<macoro::mpsc::channel<std::pair<u64, Cor>>>{},
                rem = std::vector<CorRequest>{},
                req = CorRequest{},
                ReqCor = std::move(Request<Cor>{}),
                n = u64{ 0 }, m = u64{}, i = u64{ 0 }
            );

            if (reservoirSize == 0)
                reservoirSize = numCorrelations;

            if (sessionID == SessionID::root())
            {
                sessionID = nextSID();
            }

            if (mFakeGen)
            {
                ReqCor.mSessionID = sessionID;
                ReqCor.mSize = numCorrelations;
                ReqCor.mCorrelations.resize(oc::divCeil(numCorrelations, mChunkSize));

                for (u64 i = 0; i < ReqCor.mCorrelations.size(); ++i)
                {
                    m = std::min<u64>(numCorrelations - n, mChunkSize);
                    m = 1ull << oc::log2ceil(m);
                    n += m;

                    ReqCor.mCorrelations[i] = std::make_unique<Cor>();
                    fakeFill(m, *ReqCor.mCorrelations[i], cor);
                }

                MC_RETURN(std::move(ReqCor));
            }
            else
            {

                c.reset(new macoro::mpsc::channel<std::pair<u64, Cor>>(1ull << oc::log2ceil(oc::divCeil(numCorrelations, mChunkSize))));
                i = 0;
                while (n < numCorrelations)
                {
                    m = std::min<u64>(numCorrelations - n, mChunkSize);
                    m = 1ull << oc::log2ceil(m);

                    req = CorRequest{
                        sessionID,
                        m,
                        i,
                        CorRequest::Req<Cor>{
                            std::move(cor),
                            c
                        }
                    };
                    ++i;
                    sessionID = sessionID.derive();

                    if (n < reservoirSize)
                    {
                        MC_AWAIT(mControlQueue->push(OleGenerator::Command{ std::move(req) }));
                    }
                    else
                    {
                        rem.push_back(std::move(req));
                    }

                    n += m;
                }

                std::reverse(rem.begin(), rem.end());

                MC_RETURN(std::move(Request<Cor>{
                    sessionID,
                        numCorrelations,
                        oc::divCeil(numCorrelations, mChunkSize),
                        std::move(rem),
                        this,
                        std::move(c)
                }));

            }
            MC_END();
        }

        macoro::task<Request<BinOle>> binOleRequest(u64 numCorrelations, u64 reservoirSize = 0, SessionID sessionID = SessionID::root())
        {
            return request<BinOle>(numCorrelations, BinOle{}, reservoirSize, sessionID);
        }
        macoro::task<Request<OtRecv>> otRecvRequest(u64 numCorrelations, u64 reservoirSize = 0, SessionID sessionID = SessionID::root())
        {
            return request<OtRecv>(numCorrelations, OtRecv{}, reservoirSize, sessionID);
        }
        macoro::task<Request<OtSend>> otSendRequest(u64 numCorrelations, u64 reservoirSize = 0, SessionID sessionID = SessionID::root())
        {
            return request<OtSend>(numCorrelations, OtSend{}, reservoirSize, sessionID);
        }
        macoro::task<Request<ArithTriple>> arithTripleRequest(u64 numCorrelations, u64 bitCount, u64 reservoirSize = 0, SessionID sessionID = SessionID::root())
        {
            return request<ArithTriple>(numCorrelations, ArithTriple{ bitCount }, reservoirSize, sessionID);
        }

        void log(std::string m)
        {
            std::stringstream ss;
            ss << "[" << std::this_thread::get_id() << "] ";
            ss << m;
            mLog.push(ss.str());
        }
    };

    template<typename T>
    macoro::task<T>  Request<T>::get()
    {

        if (mIdx >= mCorrelations.size())
            throw std::runtime_error("Out of correlations. " LOCATION);

        MC_BEGIN(macoro::task<T>, this,
            t = std::pair<u64, T>{}
        );

        if (mIdx >= mCorrelations.size())
            throw std::runtime_error("Out of correlations. " LOCATION);

        while (!mCorrelations[mIdx])
        {
            MC_AWAIT_SET(t, mChl->pop());
            mCorrelations[t.first] = std::make_unique<T>(std::move(t.second));
            if (mRemReq.size())
            {
                MC_AWAIT(mGen->mControlQueue->push(OleGenerator::Command{ std::move(mRemReq.back()) }));
                mRemReq.pop_back();
            }
        }
        MC_RETURN(std::move(*mCorrelations[mIdx++]));
        MC_END();
    }
}