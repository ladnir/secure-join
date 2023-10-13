#pragma once
#include "secure-join/Defines.h"
#include "secure-join/CorGenerator/Base.h"
#include "cryptoTools/Common/Aligned.h"
#include "cryptoTools/Common/BitVector.h"

#include <vector>
#include <memory>
#include <numeric>

#include "macoro/task.h"
#include "macoro/channel.h"
#include "macoro/macros.h"
#include "macoro/manual_reset_event.h"
#include "macoro/optional.h"

#include "libOTe/TwoChooseOne/Silent/SilentOtExtSender.h"
#include "libOTe/TwoChooseOne/Silent/SilentOtExtReceiver.h"

namespace secJoin
{
    namespace Corrlation2
    {
        struct RequestState;
        enum class CorType
        {
            SendOt,
            RecvOt,
            Ole
        };

        struct Cor {
            Cor(CorType t)
                :mType(t)
            {}
            Cor(const Cor&) = default;
            Cor(Cor&&) = default;
            Cor& operator=(const Cor&) = default;
            Cor& operator=(Cor&&) = default;

            CorType mType;

            // The request associated with this correlation.
            std::shared_ptr<RequestState> mRequest;
        };

        // A receiver OT correlation.
        struct OtRecv : Cor
        {

            OtRecv() : Cor(CorType::RecvOt) {}
            OtRecv(const OtRecv&) = delete;
            OtRecv& operator=(const OtRecv&) = delete;
            OtRecv(OtRecv&&) = default;
            OtRecv& operator=(OtRecv&&) = default;


            // The choice bits 
            oc::BitVector mChoice;

            // the OT messages
            oc::span<oc::block> mMsg;

            // the number of correlations this chunk has.
            u64 size() const { return mMsg.size(); }

            // The choice bits 
            oc::BitVector& choice() { return mChoice; }

            // the OT messages
            oc::span<oc::block> msg() { return mMsg; }
        };



        // A sender OT correlation.
        struct OtSend : Cor
        {

            OtSend() : Cor(CorType::SendOt) {}
            OtSend(const OtSend&) = delete;
            OtSend& operator=(const OtSend&) = delete;
            OtSend(OtSend&&) = default;
            OtSend& operator=(OtSend&&) = default;

            // the OT messages
            oc::span<std::array<oc::block, 2>> mMsg;

            u64 size() const
            {
                return mMsg.size();
            }

            oc::span<std::array<oc::block, 2>> msg() { return mMsg; }
        };


        // A sender OT correlation.
        struct BinOle : Cor
        {

            BinOle() : Cor(CorType::Ole) {}
            BinOle(const BinOle&) = delete;
            BinOle& operator=(const BinOle&) = delete;
            BinOle(BinOle&&) = default;
            BinOle& operator=(BinOle&&) = default;


            // the ole's
            oc::span<oc::block> mAdd, mMult;

            u64 size() const
            {
                return mMult.size() * 128;
            }

            //oc::span<std::array<oc::block, 2>> msg() { return mMsg; }
        };



        struct GenState;
        struct RequestState;

        struct BaseRequest
        {
            // the choice bits requested for recv base OTs
            oc::BitVector mChoice;

            // the number of send OTs requested
            u64 mSendSize = 0;

            //void operator+=(BaseRequest&& r)
            //{
            //    mChoice.insert(mChoice.end(),  std::make_move_iterator(r.mChoice.begin()),std::make_move_iterator(mChoice.end()));
            //    mSendSize.insert(mSendSize.end(), std::make_move_iterator(r.mSendSize.begin()), std::make_move_iterator(r.mSendSize.end()));
            //}

            BaseRequest() = default;
            BaseRequest(const BaseRequest&) = default;
            BaseRequest(BaseRequest&&) = default;
            BaseRequest& operator=(BaseRequest&&) = default;
            BaseRequest(span<BaseRequest> reqs)
            {
                u64 s = 0;
                for (u64 i = 0; i < reqs.size(); ++i)
                    s += reqs[i].mChoice.size();
                mChoice.reserve(s);
                for (u64 i = 0; i < reqs.size(); ++i)
                    mChoice.append(reqs[i].mChoice);
                mSendSize = std::accumulate(reqs.begin(), reqs.end(), 0ull,
                    [](auto c, auto& v) { return c + v.mSendSize; });
            }
        };

        struct Batch
        {

            // The common state
            //std::shared_ptr<GenState> mState;

            // The size of the batch
            u64 mSize = 0;

            // the index of the batch
            u64 mIndex = 0;

            // true if the correlation is ready to be consumed.
            macoro::async_manual_reset_event mCorReady;


            // true once the base OTs have been set and 
            // ready for the main phase to begin.
            macoro::async_manual_reset_event mHaveBase;

            // The socket that this batch runs on
            coproto::Socket mSock;

            // randomness source.
            oc::PRNG mPrng;

            // true if the task for this batch has been started.
            // When a task is split between one or more requests,
            // multiple requests might try to start it. This flag 
            // decides who is first.
            std::atomic_bool mStarted;

            virtual BaseRequest getBaseRequest() = 0;

            virtual void setBase(span<oc::block> rMsg, span<std::array<oc::block, 2>> sMsg) = 0;

            // Get the task associated with this batch. If the task
            // has already been retrieved, this will be empty.
            virtual macoro::task<> getTask() = 0;

            virtual void mock(u64 batchIdx) = 0;

            // get the correlation. c must match the type.
            virtual void getCor(Cor* c, u64 begin, u64 size) = 0;
        };


        struct OtBatch : Batch
        {
            OtBatch(bool sender, oc::Socket&& s, oc::PRNG&& p);
            OtBatch(OtBatch&&) { throw RTE_LOC; };

            struct SendOtBatch
            {
                oc::SilentOtExtSender mSender;
                oc::AlignedUnVector<std::array<oc::block, 2>> mMsg2;

                macoro::task<> sendTask(oc::PRNG& prng, oc::Socket& sock);
                void mock(u64 batchIdx);
            };

            struct RecvOtBatch
            {
                oc::SilentOtExtReceiver mReceiver;
                oc::AlignedUnVector<oc::block> mMsg;
                oc::BitVector mChoice;

                macoro::task<> recvTask(oc::PRNG& prng, oc::Socket& sock);
                void mock(u64 batchIdx);
            };

            macoro::variant<SendOtBatch, RecvOtBatch> mSendRecv;

            void getCor(Cor* c, u64 begin, u64 size) override;

            BaseRequest getBaseRequest() override;

            void setBase(span<oc::block> rMsg, span<std::array<oc::block, 2>> sMsg) override;

            // Get the task associated with this batch.
            macoro::task<> getTask() override;

            bool sender() { return mSendRecv.index() == 0; }

            void mock(u64 batchIdx) override;
        };


        struct OleBatch : Batch
        {
            OleBatch(bool sender, oc::Socket&& s, oc::PRNG&& p);

            // The "send" specific state
            struct SendBatch
            {
                // The OT Sender
                oc::SilentOtExtSender mSender;
                // The OT send messages
                oc::AlignedUnVector<std::array<oc::block, 2>> mMsg2;

                // return the task that generate the Sender correlation.
                macoro::task<> sendTask(
                    oc::PRNG& prng,
                    oc::Socket& sock,
                    oc::AlignedUnVector<oc::block>& add,
                    oc::AlignedUnVector<oc::block>& mult,
                    macoro::async_manual_reset_event& corReady);

                // The routine that compresses the sender's OT messages
                // into OLEs. Basically, it just tasks the LSB of the OTs.
                void compressSender(
                    span<std::array<oc::block, 2>> sendMsg,
                    span<oc::block> add,
                    span<oc::block> mult);

                void mock(u64 batchIdx,
                    span<oc::block> add,
                    span<oc::block> mult);

            };


            // The "specific" specific state
            struct RecvBatch
            {
                // The OT receiver
                oc::SilentOtExtReceiver mReceiver;
                // The OT recv choice bit
                oc::BitVector mChoice;
                // The OT recv messages
                oc::AlignedUnVector<oc::block> mMsg;
                // return the task that generate the Sender correlation.
                macoro::task<> recvTask(
                    oc::PRNG& prng,
                    oc::Socket& sock,
                    oc::AlignedUnVector<oc::block>& add,
                    oc::AlignedUnVector<oc::block>& mult,
                    macoro::async_manual_reset_event& corReady);

                // The routine that compresses the sender's OT messages
                // into OLEs. Basically, it just tasks the LSB of the OTs.
                void compressRecver(oc::BitVector& bv, span<oc::block> recvMsg, span<oc::block> add, span<oc::block> mult);

                void mock(u64 batchIdx, span<oc::block> add, span<oc::block> mult);

            };

            macoro::variant<SendBatch, RecvBatch> mSendRecv;

            oc::AlignedUnVector<oc::block> mAdd, mMult;

            void getCor(Cor* c, u64 begin, u64 size) override;

            BaseRequest getBaseRequest() override;

            void setBase(span<oc::block> rMsg, span<std::array<oc::block, 2>> sMsg) override;

            // Get the task associated with this batch.
            macoro::task<> getTask() override;

            void mock(u64 batchIdx) override;
        };

        inline std::shared_ptr<Batch> makeBatch(CorType type, u64 partyIdx, oc::Socket&& sock, oc::PRNG&& p)
        {
            switch (type)
            {
            case CorType::SendOt:
                return std::make_shared<OtBatch>(true, std::move(sock), std::move(p));
                break;
            case CorType::RecvOt:
                return std::make_shared<OtBatch>(false, std::move(sock), std::move(p));
                break;
            case CorType::Ole:
                return std::make_shared<OleBatch>(partyIdx, std::move(sock), std::move(p));
                break;
            default:
            std:
                break;
            }
        }

        struct BatchOffset {

            // The batch being referenced.
            std::shared_ptr<Batch> mBatch;

            // the begin index of this correlation within the referenced batch.
            u64 mBegin = 0;

            // the size of the correlation with respect to the referenced batch.
            u64 mSize = 0;
        };

        struct RequestState : std::enable_shared_from_this<RequestState>
        {
            RequestState(CorType t, u64 size, std::shared_ptr<GenState>&, u64 idx);

            // the type of the correlation
            CorType mType;

            // the total size of the request
            u64 mSize = 0;

            // the index of this request.
            u64 mReqIndex = 0;

            // the index of the next batch in the get() call.
            u64 mNextBatchIdx = 0;

            // a flag encoding if the request has been started.
            std::atomic_bool mStarted = false;

            // Where in the i'th batch should we take the correlations.
            std::vector<BatchOffset> mBatches;

            // The core state.
            std::shared_ptr<GenState> mGenState;

            // set by the batch when it completes.
            //macoro::async_manual_reset_event mDone;

            // Return a task that starts the preprocessing.
            macoro::task<> startReq();

            // returns the number of batches this request has.
            u64 batchCount();

            // returns the total number of correlations requested.
            u64 size();

            // clears the state associated with the request.
            void clear();
        };



        struct GenState : std::enable_shared_from_this<GenState>
        {
            GenState() = default;
            GenState(const GenState&) = delete;
            GenState(GenState&&) = delete;

            oc::SoftSpokenShOtSender<> mSendBase;
            oc::SoftSpokenShOtReceiver<> mRecvBase;

            // all of the batches. 
            //std::vector<std::shared_ptr<Batch>> mBatches;

            // all of the requests. These are broken down into batches.
            std::vector<std::shared_ptr<RequestState>> mRequests;

            // randomness source
            oc::PRNG mPrng;

            // the base socket that each subprotocol is forked from.
            coproto::Socket mSock;

            // the size that a batch of OT/OLEs should be generated in.
            u64 mBatchSize = 0;

            // true if the base OTs has been started. This 
            // will guard starting them to ensure only one 
            // thread does it.
            std::atomic<bool> mBaseOtsStarted = false;

            // true if we should just fake the correlation generation
            bool mMock = false;

            // used to determine which party should go first when its ambiguous.
            u64 mPartyIdx = 0;

            // returns a task that constructs the base OTs and assigns them to batches.
            macoro::task<> startBaseOts();

            void set(SendBase& b);
            void set(RecvBase& b);
        };

        template<typename Cor>
        struct Request
        {
            std::shared_ptr<RequestState> mReqState;

            macoro::task<> get(Cor& d)
            {
                MC_BEGIN(macoro::task<>, this, &d,
                    batch = (BatchOffset*)nullptr
                );

                if (mReqState->mNextBatchIdx >= mReqState->mBatches.size())
                    throw RTE_LOC;

                if (mReqState->mStarted == false)
                    std::terminate();

                batch = &mReqState->mBatches[mReqState->mNextBatchIdx++];
                MC_AWAIT(batch->mBatch->mCorReady);

                d.mRequest = mReqState;
                batch->mBatch->getCor(&d, batch->mBegin, batch->mSize);

                MC_END();
            }

            macoro::task<> start() {
                if (mReqState->mStarted.exchange(true) == false)
                    return mReqState->startReq();
                else
                {
                    MC_BEGIN(macoro::task<>);
                    MC_END();
                }
            }

            u64 batchCount() { return mReqState->batchCount(); }

            u64 size() { return mReqState->mSize; }

            void clear() { mReqState->clear(); }
        };

        struct CorGenerator2
        {
            std::shared_ptr<GenState> mGenState;


            template<typename Base>
            void init(
                u64 batchSize,
                coproto::Socket& sock,
                oc::PRNG& prng,
                Base& base,
                u64 partyIdx,
                bool mock)
            {
                mGenState = std::make_shared<GenState>();
                mGenState->mBatchSize = batchSize;
                mGenState->mSock = sock;
                mGenState->mPrng = prng.fork();
                mGenState->set(base);
                mGenState->mPartyIdx = partyIdx;
                mGenState->mMock = mock;
            }

            Request<OtRecv> requestRecvOt(u64 n) { return Request<OtRecv>{request(CorType::RecvOt, oc::roundUpTo(n, 128))}; }
            Request<OtSend> requestSendOt(u64 n) { return Request<OtSend>{request(CorType::SendOt, oc::roundUpTo(n, 128))}; }
            Request<BinOle> requestBinOle(u64 n) { return Request<BinOle>{request(CorType::Ole, oc::roundUpTo(n, 128))}; }

            bool started()
            {
                return mGenState && mGenState->mBaseOtsStarted;
            }

            bool initialized()
            {
                return mGenState.get();
            }

        private:

            std::shared_ptr<RequestState> request(CorType, u64);

        };


    }


}