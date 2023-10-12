#pragma once
#include "secure-join/Defines.h"
#include "secure-join/CorGenerator/Base.h"

#include "cryptoTools/Common/Aligned.h"
#include "cryptoTools/Common/BitVector.h"

#include <vector>
#include <memory>

#include "macoro/task.h"
#include "macoro/channel.h"
#include "macoro/macros.h"
#include "macoro/manual_reset_event.h"

#include "libOTe/TwoChooseOne/Silent/SilentOtExtSender.h"
#include "libOTe/TwoChooseOne/Silent/SilentOtExtReceiver.h"

namespace secJoin
{




    struct BinOleGenerator
    {
        struct GenState;

        // Used to generate a batch of OLEs.
        // This object owns the underlying memory that the 
        // correlation is stored in. Moreover, correlations for a single
        // batch can be consumed by different requesters. As such, a 
        // Batch might be shared between different OLE requests. The lifetime
        // of a batch is maintained by a shared pointer.
        struct Batch
        {
            Batch() = default;
            Batch(Batch&&) { throw RTE_LOC; };

            // The "send" specific state
            struct SendBatch
            {
                // The OT Sender
                oc::SilentOtExtSender mSender;
                // The OT send messages
                oc::AlignedUnVector<std::array<oc::block, 2>> mMsg2;

                // return the task that generate the Sender correlation.
                macoro::task<> sendTask(Batch& batch);

                // The routine that compresses the sender's OT messages
                // into OLEs. Basically, it just tasks the LSB of the OTs.
                void compressSender(span<std::array<oc::block, 2>> sendMsg, span<oc::block> add, span<oc::block> mult);
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
                macoro::task<> recvTask(Batch& batch);

                // The routine that compresses the sender's OT messages
                // into OLEs. Basically, it just tasks the LSB of the OTs.
                void compressRecver(oc::BitVector& bv, span<oc::block> recvMsg, span<oc::block> add, span<oc::block> mult);
            };

            macoro::variant<SendBatch, RecvBatch> mSendRecv;

            // The total size of the batch.
            u64 mSize = 0;

            // The OLE correlation (mult0  mult1) = (add0+add1).
            // Stored in pacted format, i.e. 128 OLEs per block.
            oc::AlignedUnVector<oc::block> mMult, mAdd;

            // The task once it has started.
            macoro::eager_task<> mTask;

            // The task before it has started.
            macoro::task<> mT;

            // Set once this task is done. Multiple people can wait on this.
            macoro::async_manual_reset_event mDone;

            // The Socket associated with this specific task.
            coproto::Socket mSock;

            // Randomness for this batch
            oc::PRNG mPrng;

            // The stated state that this batch is associated with
            std::shared_ptr<GenState> mState;

            // A flag to decide if a caller should start this task.
            std::atomic_bool mStarted;

            // Init the task. Forks the socket, set the base OTs and initializes mT.
            // Once init, task() to get theunderling task.
            void init(std::shared_ptr<GenState>& state);

            // get the underlying task.
            macoro::task<> task();

            // get and start the underlying task.
            void start()
            {
                auto s = mStarted.exchange(true);
                if (s == false)
                {
                    mTask = task() | macoro::make_eager();
                }
            }
        };


        // A OLE correlation. This represents some or all of the required
        // OLE correlation. Specifically, then the user requests n OLEs,
        // this divided up into chunks of size at most GenState::mBatchSize.
        // The user is given one or more BinOle's such that overall
        // the are given at least n OLEs.
        struct BinOle
        {
            BinOle() = default;
            BinOle(const BinOle&) = delete;
            BinOle& operator=(const BinOle&) = delete;
            BinOle(BinOle&&) = default;
            BinOle& operator=(BinOle&&) = default;

            // A shared pointer to manage the lifetime the lifetime associated with
            // mMult, mAdd.
            std::shared_ptr<Batch> mBatch;

            // The OLE correlation (mult0  mult1) = (add0+add1).
            // Stored in pacted format, i.e. 128 OLEs per block.
            oc::span<oc::block> mMult, mAdd;

            // The number of individual binary OLEs that this BinOLE has.
            // The number of block's is this divided by 128.
            u64 size() const
            {
                return mMult.size() * 128;
            }

            // release this instance.
            void clear()
            {
                mBatch = {};
                mMult = {};
                mAdd = {};
            }
        };

        // object representing a request for OLE correlation.
        // This in turn has one or more OLE batch's.
        struct Request
        {
            Request() = default;
            Request(const Request&) = delete;
            Request(Request&&) = default;
            Request& operator=(Request&&) = default;

            struct Offset
            {
                // the underlying batch
                std::shared_ptr<Batch> mBatch;

                // the index that this batch starts at and how big the batch is.
                u64 mOffset = 0, mSize = 0;
            };

            // The shared state used to general all OLE.
            std::shared_ptr<GenState> mState;

            // The index of the next batch to be consumed.
            u64 mNextBatchIdx = 0;

            // The batches.
            std::vector<Offset> mBatches;

            // Returns the task to start this request. When first created 
            // the batches are allocated but not started.
            macoro::task<> start();

            // returns a task that when awaited populates d with the OLE correlation.
            // All or a subset of the requested OLEs will be in d.
            macoro::task<> get(BinOle& d);


            // The number of batches that the request has. It will be at most
            // size / batchsize + 1
            u64 batchCount()
            {
                return mState->mBatches.size();
            }

            // return the total size of the request.
            u64 size() const
            {
                u64 s = 0;
                for (auto b : mBatches)
                    s += b.mSize;
                return s;
            }

            // deallocates and cancels and pending batches.
            void clear();
        };

        // The common state that is used to initialize the batches.
        // This state tracks the requests and the batches.
        struct GenState
        {
            GenState() = default;
            GenState(const GenState&) = delete;
            GenState(GenState&&) = delete;

            // The batches.
            std::vector<std::shared_ptr<Batch>> mBatches;

            // the requests that the user makes. Each will be assigned 
            // to one or more batches
            std::vector<Request> mRequests;

            // Randomness source.
            oc::PRNG mPrng;

            // The socked that individual batches will be forked from.
            coproto::Socket mSock;

            //
            //u64 mIdx = 0;
            //u64 mSize = 0;

            // zero or one depending on the party index.
            u64 mRole = 0;

            // The max size of a batch
            u64 mBatchSize = 0;

            // True of the preprocessing have been started.
            bool mStarted = false;

            // true if we should fake the preprocessing.
            bool mMock = false;

            // The base OTs send messages. Used initialize each batch.
            SendBase mSendBase;

            // The base OTs send messages. Used initialize each batch.
            RecvBase mRecvBase;

            void set(SendBase& b) { mSendBase = b.fork(); }
            void set(RecvBase& b) { mRecvBase = b.fork(); }

        };

        // The common state that tracks the batches and requests.
        std::shared_ptr<GenState> mState;


        // intialise the generator with the given base ots.
        // batches will use batchSize OLEs per batch.
        template<typename Base>
        void init(
            u64 batchSize,
            coproto::Socket& sock,
            oc::PRNG& prng,
            Base& base,
            bool mock = false);


        // request n OLEs. These will not be started until 
        // Request::start() is called.
        Request request(u64 n);

        // Return true if some request has been true.
        bool started()
        {
            return mState && mState->mStarted;
        }

        // returns true if init has been called.
        bool initialized()
        {
            return mState.get();
        }

    };

    using BinOleRequest = BinOleGenerator::Request;
    using BinOle = BinOleGenerator::BinOle;
}