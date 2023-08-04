#include "OleGenerator.h"
#include <unordered_map>

#define LOG(X) log(X)

namespace secJoin
{
    const int inplace = 0;
    template<typename T>
    inline std::string str(T&& t)
    {
        std::stringstream ss;
        ss << t;
        return ss.str();
    }
    inline
#define CHECK \
    if (ec.has_error())                                              \
    {                                                                \
        try {                                                        \
            std::rethrow_exception(ec.error());                      \
        }                                                            \
        catch (std::exception& e)                                     \
        {                                                            \
            std::cout << "EC: " << e.what() << " " << LOCATION << std::endl;      \
            throw;                                                   \
        }                                                            \
    } do{}while (0)

    macoro::task<> OleGenerator::Gen::start()
    {
        MC_BEGIN(macoro::task<>, this,
            //slotIdx = u64{},
            //done = false,
            sendMsg = oc::AlignedUnVector<std::array<oc::block, 2>>{},
            recvMsg = oc::AlignedUnVector<oc::block>{},
            bv = oc::BitVector{},
            diff = oc::BitVector{},
            delta = oc::block{},
            rec = CorRequest{},
            ec = macoro::result<void, std::exception_ptr>{},
            buff = std::vector<u8>{}
        );

        LOG("start");

        while (true)
        {
            if (mParent->mRole == Role::Sender)
            {


                MC_AWAIT_SET(rec, mInQueue->pop());
                LOG("pop chunk " + std::to_string(rec.mSequence) +" " + str(rec.mSessionID));
                MC_AWAIT(macoro::transfer_to(*mParent->mThreadPool));
                LOG("transfered " + std::to_string(rec.mSequence) + " " + str(rec.mSessionID));

                buff.resize(rec.sizeBytes());
                rec.toBytes(buff);
                MC_AWAIT_TRY(ec, mChl.send(std::move(buff)));
                CHECK;
            }
            else
            {
                buff.resize(rec.sizeBytes());
                MC_AWAIT_TRY(ec, mChl.recv(buff));                
                CHECK;

                rec.fromBytes(buff);

                LOG("recv chunk " + std::to_string(rec.mSequence) + " " + str(rec.mSessionID));

            }


            //A// std::cout << "pop chunk" << std::endl;
            if (rec.mSequence == ~0ull)
            {
                MC_AWAIT(mParent->mControlQueue->push({ Command::GenStopped{mIdx} }));
                LOG("genStop");

                MC_RETURN_VOID();
            }

            if (mParent->mRole == Role::Sender)
            {
                if (rec.mOp.index() == 0)
                {
                    if (inplace)
                    {
                        delta = mPrng.get();
                        MC_AWAIT(mSender->silentSendInplace(delta, mParent->mChunkSize, mPrng, mChl));
                    }
                    else
                    {
                        sendMsg.resize(mParent->mChunkSize);
                        MC_AWAIT(mSender->silentSend(sendMsg, mPrng, mChl));
                    }
                    LOG("silentSend " + std::to_string(rec.mSequence));
                    //A// std::cout << "send ot " << sendMsg[0][0] << " " << sendMsg[0][1] << std::endl;

                    rec.mOp.get<0>().mAdd.resize(mParent->mChunkSize / 128);
                    rec.mOp.get<0>().mMult.resize(mParent->mChunkSize / 128);
                    if (inplace)
                        compressSender(mSender->mB, delta, rec.mOp.get<0>().mAdd, rec.mOp.get<0>().mMult);
                    else
                        compressSender(sendMsg, rec.mOp.get<0>().mAdd, rec.mOp.get<0>().mMult);

                }
                else if (rec.mOp.index() == 2)
                {
                    rec.mOp.get<2>().mMsg.resize(mParent->mChunkSize);
                    MC_AWAIT(mSender->silentSend(rec.mOp.get<2>().mMsg, mPrng, mChl));
                }
                else
                    std::terminate();
            }
            else
            {

                if (rec.mOp.index() == 0)
                {
                    if (inplace)
                    {
                        MC_AWAIT_TRY(ec, mRecver->silentReceiveInplace(mParent->mChunkSize, mPrng, mChl, oc::ChoiceBitPacking::True));
                        CHECK;

                        LOG("silentReceive " + std::to_string(rec.mSequence));
                        rec.mOp.get<0>().mAdd.resize(mParent->mChunkSize / 128);
                        rec.mOp.get<0>().mMult.resize(mParent->mChunkSize / 128);
                        compressRecver(recvMsg, rec.mOp.get<0>().mAdd, rec.mOp.get<0>().mMult);
                    }
                    else
                    {

                        recvMsg.resize(mParent->mChunkSize);
                        bv.resize(mParent->mChunkSize);
                        LOG("silentReceive begin " + std::to_string(rec.mSequence));
                        MC_AWAIT_TRY(ec, mRecver->silentReceive(bv, recvMsg, mPrng, mChl));
                        CHECK;

                        LOG("silentReceive done  " + std::to_string(rec.mSequence));

                        rec.mOp.get<0>().mAdd.resize(mParent->mChunkSize / 128);
                        rec.mOp.get<0>().mMult.resize(mParent->mChunkSize / 128);
                        compressRecver(bv, recvMsg, rec.mOp.get<0>().mAdd, rec.mOp.get<0>().mMult);
                    }
                }
                else if (rec.mOp.index() == 1)
                {
                    rec.mOp.get<1>().mChoice.resize(mParent->mChunkSize);
                    rec.mOp.get<1>().mMsg.resize(mParent->mChunkSize);
                    MC_AWAIT_TRY(ec, mRecver->silentReceive(bv, recvMsg, mPrng, mChl));
                    CHECK;

                }
                else
                    std::terminate();
            }

            LOG("publish " + std::to_string(rec.mSequence));
            MC_AWAIT(mParent->mControlQueue->push({ Command::ChunkComplete{mIdx, std::move(rec)} }));
            LOG("published " + std::to_string(rec.mSequence));
        }

        MC_END();
    }



    macoro::task<> OleGenerator::stop()
    {
        MC_BEGIN(macoro::task<>, this);


        if (!mFakeGen)
        {
            MC_AWAIT(mControlQueue->push({ Command::Stop{} }));
            MC_AWAIT(mCtrl);
            MC_AWAIT(mControlQueue->close());

        }

        //assert(mControlQueue->size() == 0);
        

        MC_END();
    }

    macoro::task<> OleGenerator::control()
    {
        MC_BEGIN(macoro::task<>, this,
            i = u64{},
            wid = u64{},
            sid = SessionID{},
            chunk = CorRequest{},
            cmd = Command{},
            pushIdxs = std::vector<u64>{},
            //popIdx = u64{},
            numTasks = u64{},

            baseSender = oc::SoftSpokenMalOtSender{},
            baseRecver = oc::SoftSpokenMalOtReceiver{},
            sessions = std::unordered_map<SessionID, CorRequest>{},
            completed = std::unordered_map<SessionID, CorRequest>{},
            queue = std::list<CorRequest>{},
            idle = std::vector<u64>{},
            curReq = (CorRequest*)nullptr
        );

        //A// std::cout << "control " << std::endl;
        LOG("control: start");

        numTasks = 0;

        {
            oc::SilentOtExtSender s;
            s.configure(mChunkSize);
            mBaseSize = s.silentBaseOtCount();
            //std::cout << "total base " << mNumChunks * mBaseSize << " = n " << mNumChunks << " * s " << mBaseSize << std::endl;
        }

        if (mRole == Role::Sender)
        {
            MC_AWAIT(baseSender.genBaseOts(mPrng, mChl));
        }
        else
        {
            MC_AWAIT(baseRecver.genBaseOts(mPrng, mChl));
        }

        LOG("Base done");

        mGens.resize(mNumConcurrent);
        for (i = 0; i < mNumConcurrent; ++i)
        {

            if (mRole == Role::Sender)
            {
                mGens[i].mSender.reset(new oc::SilentOtExtSender);
                mGens[i].mSender->mOtExtSender = baseSender.splitBase();
            }
            else
            {
                mGens[i].mRecver.reset(new oc::SilentOtExtReceiver);
                mGens[i].mRecver->mOtExtRecver = baseRecver.splitBase();
            }

            ++numTasks;
            mGens[i].mIdx = i;
            mGens[i].mInQueue.reset(new macoro::spsc::channel<CorRequest>(2));
            mGens[i].mParent = this;
            mGens[i].mPrng.SetSeed(mPrng.get());

            mGens[i].mChl = mChl.fork();
            mGens[i].mChl.setExecutor(*mThreadPool);

            mGens[i].mTask = mGens[i].start()
                | macoro::make_eager();

            idle.push_back(i);
        }


        pushIdxs.resize(mNumConcurrent);

        //popIdx = 0;
        while (true)
        {
            MC_AWAIT_SET(cmd, mControlQueue->pop());

            if (cmd.mOp.index() == cmd.mOp.index_of<Command::ChunkComplete>())
            {

                wid = cmd.mOp.get<Command::ChunkComplete>().mWorkerId;
                sid = cmd.mOp.get<Command::ChunkComplete>().mChunk.mSessionID;
                chunk = std::move(cmd.mOp.get<Command::ChunkComplete>().mChunk);
                LOG("control: ChunkComplete " + str(sid));

                if (sessions.find(sid) != sessions.end())
                {
                    curReq = &sessions[sid];
                    assert(chunk.mSequence != ~0ull);
                    //sessions[sid].publish(std::move(cmd.mOp.get<Command::ChunkComplete>().mChunk));
                    if (curReq->mOp.index() == 0)
                    {
                        MC_AWAIT(curReq->mOp.get<0>().mQueue->push({ 
                            chunk.mSequence,
                            std::move(chunk.mOp.get<0>()) }));
                    }
                    else if (curReq->mOp.index() == 1)
                    {
                        MC_AWAIT(curReq->mOp.get<1>().mQueue->push({
                            chunk.mSequence,
                            std::move(chunk.mOp.get<1>()) }));
                    }
                    else if (curReq->mOp.index() == 2)
                    {
                        MC_AWAIT(curReq->mOp.get<2>().mQueue->push({
                            chunk.mSequence,
                            std::move(chunk.mOp.get<2>()) }));
                    }
                    else if (curReq->mOp.index() == 3)
                    {
                        MC_AWAIT(curReq->mOp.get<3>().mQueue->push({
                            chunk.mSequence,
                            std::move(chunk.mOp.get<3>()) }));
                    }
                    else
                        std::terminate();

                    sessions.erase(sid);
                }
                else
                {
                    if (completed.find(sid) != completed.end())
                    {
                        std::cout << "session id already completed. "<< sid << " " << LOCATION << std::endl;
                        std::terminate();
                    }
                    completed[sid] = std::move(cmd.mOp.get<Command::ChunkComplete>().mChunk);
                }

                //cmd.mOp.get<Command::ChunkComplete>().mChunk

                if (mRole == Role::Sender)
                {
                    if (queue.size())
                    {
                        //sid = queue.front();
                        //queue.splice(queue.end(), queue, queue.begin());
                        //getBaseOts(chunk, queue.front());

                        //A//  std::cout << "\n\npush chunk " << i << std::endl;
                        LOG("control: push chunk " + std::to_string(chunk.mSequence) + " " + str(chunk.mSessionID));
                        MC_AWAIT(mGens[wid].mInQueue->push(std::move(chunk)));
                    }
                    else
                    {
                        idle.push_back(wid);
                    }
                }
            }
            else if (cmd.mOp.index() == cmd.mOp.index_of<Command::Stop>())
            {
                LOG("control: stop request");
                mStopRequested = true;
                for (i = 0; i < mNumConcurrent; ++i)
                {
                    chunk.mSequence = -1;
                    MC_AWAIT(mGens[i].mInQueue->push(std::move(chunk)));
                    LOG("control: gen stop sent " + std::to_string(i));

                }

                for (i = 0; i < mNumConcurrent; ++i)
                {
                    MC_AWAIT(mGens[i].mTask);
                    LOG("control: gen stopded " + std::to_string(i));
                }
            }
            else if (cmd.mOp.index() == cmd.mOp.index_of<Command::GenStopped>())
            {
                //A// std::cout << "gen " << cmd.mOp.get<Command::GenStopped>().mIdx << " stopped" << std::endl;
                LOG("control: gen stopped " + std::to_string(cmd.mOp.get<Command::GenStopped>().mIdx));

                MC_AWAIT(mGens[cmd.mOp.get<Command::GenStopped>().mIdx].mTask);

                if (--numTasks == 0)
                {
                    //A// std::cout << "control stopped" << std::endl;
                    LOG("control: stopped ");
                    MC_RETURN_VOID();
                }
            }
            else if (cmd.mOp.index() == cmd.mOp.index_of<CorRequest>())
            {
                curReq = &cmd.mOp.get<3>();
                sid = curReq->mSessionID;
                assert(curReq->mSequence != ~0ull);
                if (mRole == Role::Sender)
                {
                    sessions.emplace(sid, std::move(*curReq));

                    if (idle.size())
                    {
                        wid = idle.back();
                        idle.pop_back();

                        LOG("control: push chunk " + std::to_string(curReq->mSequence) + " " + str(curReq->mSessionID));
                        MC_AWAIT(mGens[wid].mInQueue->push(std::move(*curReq)));
                    }
                    else
                    {
                        queue.push_back(std::move(*curReq));
                    }
                }
                else
                {
                    if (completed.find(sid) != completed.end())
                    {
                        chunk = std::move(completed.find(sid)->second);
                        if (curReq->mOp.index() != chunk.mOp.index())
                        {
                            std::cout << "mixed correlation type for session id. " << LOCATION << std::endl;
                            std::terminate();
                        }
                        if (curReq->mN != chunk.mN)
                        {
                            std::cout << "mixed correlation size for session id. " << LOCATION << std::endl;
                            std::terminate();
                        }
                        assert(chunk.mSequence != ~0ull);

                        if (curReq->mOp.index() == 0)
                        {
                            MC_AWAIT(curReq->mOp.get<0>().mQueue->push({
                                chunk.mSequence,
                                std::move(chunk.mOp.get<0>()) }));
                        }
                        else if (curReq->mOp.index() == 1)
                        {
                            MC_AWAIT(curReq->mOp.get<1>().mQueue->push({
                                chunk.mSequence,
                                std::move(chunk.mOp.get<1>()) }));
                        }
                        else if (curReq->mOp.index() == 2)
                        {
                            MC_AWAIT(curReq->mOp.get<2>().mQueue->push({
                                chunk.mSequence,
                                std::move(chunk.mOp.get<2>()) }));
                        }
                        else if (curReq->mOp.index() == 3)
                        {
                            MC_AWAIT(curReq->mOp.get<3>().mQueue->push({
                                chunk.mSequence,
                                std::move(chunk.mOp.get<3>()) }));
                        }
                        else
                            std::terminate();

                        completed.erase(sid);
                    }
                    else
                    {
                        sessions.emplace(sid, std::move(*curReq));
                    }
                }
            }
            else
            {
                throw RTE_LOC;
            }
        }

        MC_END();
    }

    //void OleGenerator::getBaseOts(Chunk& chunk, CorRequest& rec)
    //{
    //    if (mRole == Role::Sender)
    //    {
    //        chunk.mBaseSend.resize(mBaseSize);
    //        for (u64 j = 0; j < mBaseSize; ++j)
    //        {
    //            chunk.mBaseSend[j][0] = oc::block(123 * j, 23423 * j);
    //            chunk.mBaseSend[j][1] = oc::block(123 * j, 23423 * j + 324);
    //        }
    //    }
    //    else
    //    {
    //        chunk.mBaseChoice.resize(mBaseSize);
    //        chunk.mBaseRecv.resize(mBaseSize);
    //        for (u64 j = 0; j < mBaseSize; ++j)
    //        {
    //            chunk.mBaseChoice[j] = j & 1;
    //            chunk.mBaseRecv[j] = oc::block(123 * j, 23423 * j + (324 * (j & 1)));
    //        }
    //    }
    //}


    void OleGenerator::init(Role role, macoro::thread_pool& threadPool, coproto::Socket chl, oc::PRNG& prng, u64 numConcurrent, u64 chunkSize)
    {
        if (!numConcurrent)
            throw std::runtime_error("OleGenerator::numConcurrent must be non-zero");

        mRole = role;
        mThreadPool = &threadPool;
        mChl = chl.fork();
        mChl.setExecutor(threadPool);
        mPrng.SetSeed(prng.get());
        mChunkSize = oc::roundUpTo(1ull << oc::log2ceil(chunkSize), 128);
        mNumConcurrent = numConcurrent;
        mControlQueue.reset(new macoro::mpsc::channel<Command>(1024));

        if (!mFakeGen)
        {
            throw std::runtime_error("known issue with OleGenerator, use mock for now. " LOCATION);
            mCtrl = control() | macoro::make_eager();
        }
    }

    void OleGenerator::fakeFill(u64 m, BinOle& ole, const BinOle&)
    {
        mNumBinOle += m;
        assert(m % 128 == 0);
        m = m / 128;

        //oc::PRNG prng(oc::block(mCurSize++));
        ole.mAdd.resize(m);
        ole.mMult.resize(m);
        auto add = ole.mAdd.data();
        auto mult = ole.mMult.data();

        auto m8 = m / 8 * 8;
        oc::block mm8(4532453452, 43254534);
        oc::block mm(2342314, 213423);

        if (mRole == Role::Sender)
        {
            oc::block aa8(0, 43254534);
            oc::block aa(0, 213423);
            u64 i = 0;
            while (i < m8)
            {
                mult[i + 0] = mm;
                mult[i + 1] = mm;
                mult[i + 2] = mm;
                mult[i + 3] = mm;
                mult[i + 4] = mm;
                mult[i + 5] = mm;
                mult[i + 6] = mm;
                mult[i + 7] = mm;
                add[i + 0] = aa;
                add[i + 1] = aa;
                add[i + 2] = aa;
                add[i + 3] = aa;
                add[i + 4] = aa;
                add[i + 5] = aa;
                add[i + 6] = aa;
                add[i + 7] = aa;
                mm += mm8;
                aa += aa8;
                i += 8;
            }
            for (; i < m; ++i)
            {
                //oc::block m0 = std::array<u32, 4>{i, i, i, i};// prng.get();
                //oc::block m1 = std::array<u32, 4>{i, i, i, i};//prng.get();
                //oc::block a0 = std::array<u32, 4>{0, i, 0, i};//prng.get();
                //auto a1 = std::array<u32, 4>{i, 0, i, 0};;// m0& m1^ a0;
                mult[i] = oc::block(i, i);
                add[i] = oc::block(0, i);
            }
        }
        else
        {

            oc::block aa8(4532453452, 0);
            oc::block aa(2342314, 0);
            u64 i = 0;
            while (i < m8)
            {
                //oc::block mm(i, i);
                //oc::block aa(i, 0);
                mult[i + 0] = mm;
                mult[i + 1] = mm;
                mult[i + 2] = mm;
                mult[i + 3] = mm;
                mult[i + 4] = mm;
                mult[i + 5] = mm;
                mult[i + 6] = mm;
                mult[i + 7] = mm;
                add[i + 0] = aa;
                add[i + 1] = aa;
                add[i + 2] = aa;
                add[i + 3] = aa;
                add[i + 4] = aa;
                add[i + 5] = aa;
                add[i + 6] = aa;
                add[i + 7] = aa;
                mm += mm8;
                aa += aa8;
                i += 8;
            }
            for (; i < m; ++i)
            {
                mult[i] = oc::block(i, i);
                add[i] = oc::block(i, 0);
            }
        }
    }

    void OleGenerator::Gen::compressSender(span<oc::block> sendMsg, oc::block delta, span<oc::block> add, span<oc::block> mult)
    {

        auto bIter8 = (u8*)add.data();
        auto aIter8 = (u8*)mult.data();

        if (add.size() * 128 != sendMsg.size())
            throw RTE_LOC;
        if (mult.size() * 128 != sendMsg.size())
            throw RTE_LOC;
        using block = oc::block;

        auto shuffle = std::array<block, 16>{};
        memset(shuffle.data(), 1 << 7, sizeof(*shuffle.data()) * shuffle.size());
        for (u64 i = 0; i < 16; ++i)
            shuffle[i].set<u8>(i, 0);

        oc::AlignedArray<block, 16> m;
        auto m0 = m.data();
        auto m1 = m.data() + 8;
        oc::block mask = ~oc::OneBlock;
        for (u64 i = 0; i < sendMsg.size(); i += 8)
        {
            for (u64 j = 0; j < 8; ++j)
            {
                m0[j] = sendMsg[i + j] & mask;
                m1[j] = m0[j] ^ delta;
            }

            oc::mAesFixedKey.hashBlocks<16>(m.data(), m.data());

            auto a0 = m0[0].testc(oc::OneBlock);
            auto a1 = m0[1].testc(oc::OneBlock);
            auto a2 = m0[2].testc(oc::OneBlock);
            auto a3 = m0[3].testc(oc::OneBlock);
            auto a4 = m0[4].testc(oc::OneBlock);
            auto a5 = m0[5].testc(oc::OneBlock);
            auto a6 = m0[6].testc(oc::OneBlock);
            auto a7 = m0[7].testc(oc::OneBlock);

            auto ap =
                a0 ^
                (a1 << 1) ^
                (a2 << 2) ^
                (a3 << 3) ^
                (a4 << 4) ^
                (a5 << 5) ^
                (a6 << 6) ^
                (a7 << 7);

            auto b0 = m1[0].testc(oc::OneBlock);
            auto b1 = m1[1].testc(oc::OneBlock);
            auto b2 = m1[2].testc(oc::OneBlock);
            auto b3 = m1[3].testc(oc::OneBlock);
            auto b4 = m1[4].testc(oc::OneBlock);
            auto b5 = m1[5].testc(oc::OneBlock);
            auto b6 = m1[6].testc(oc::OneBlock);
            auto b7 = m1[7].testc(oc::OneBlock);

            auto bp =
                b0 ^
                (b1 << 1) ^
                (b2 << 2) ^
                (b3 << 3) ^
                (b4 << 4) ^
                (b5 << 5) ^
                (b6 << 6) ^
                (b7 << 7);

            *aIter8++ = ap ^ bp;
            *bIter8++ = ap;
        }
    }

    void OleGenerator::Gen::compressRecver(span<oc::block> recvMsg, span<oc::block> add, span<oc::block> mult)
    {
        auto aIter8 = (u8*)add.data();
        auto bIter8 = (u8*)mult.data();

        //if (bv.size() != recvMsg.size())
        //    throw RTE_LOC;
        if (add.size() * 128 != recvMsg.size())
            throw RTE_LOC;
        if (mult.size() * 128 != recvMsg.size())
            throw RTE_LOC;
        using block = oc::block;

        auto shuffle = std::array<block, 16>{};
        memset(shuffle.data(), 1 << 7, sizeof(*shuffle.data()) * shuffle.size());
        for (u64 i = 0; i < 16; ++i)
            shuffle[i].set<u8>(i, 0);

        block mask = oc::OneBlock ^ oc::AllOneBlock;

        oc::AlignedArray<oc::block, 8> m;
        for (u64 i = 0; i < recvMsg.size(); i += 8)
        {
            auto r = &recvMsg[i];
            // extract the choice bit from the LSB of r
            u32 b0 = r[0].testc(oc::OneBlock);
            u32 b1 = r[1].testc(oc::OneBlock);
            u32 b2 = r[2].testc(oc::OneBlock);
            u32 b3 = r[3].testc(oc::OneBlock);
            u32 b4 = r[4].testc(oc::OneBlock);
            u32 b5 = r[5].testc(oc::OneBlock);
            u32 b6 = r[6].testc(oc::OneBlock);
            u32 b7 = r[7].testc(oc::OneBlock);

            // pack the choice bits.
            *bIter8++ =
                b0 ^
                (b1 << 1) ^
                (b2 << 2) ^
                (b3 << 3) ^
                (b4 << 4) ^
                (b5 << 5) ^
                (b6 << 6) ^
                (b7 << 7);

            // mask of the choice bit which is stored in the LSB
            m[0] = r[0] & mask;
            m[1] = r[1] & mask;
            m[2] = r[2] & mask;
            m[3] = r[3] & mask;
            m[4] = r[4] & mask;
            m[5] = r[5] & mask;
            m[6] = r[6] & mask;
            m[7] = r[7] & mask;

            oc::mAesFixedKey.hashBlocks<8>(m.data(), m.data());

            auto a0 = m[0].testc(oc::OneBlock);
            auto a1 = m[1].testc(oc::OneBlock);
            auto a2 = m[2].testc(oc::OneBlock);
            auto a3 = m[3].testc(oc::OneBlock);
            auto a4 = m[4].testc(oc::OneBlock);
            auto a5 = m[5].testc(oc::OneBlock);
            auto a6 = m[6].testc(oc::OneBlock);
            auto a7 = m[7].testc(oc::OneBlock);

            // pack the choice bits.
            *aIter8++ =
                a0 ^
                (a1 << 1) ^
                (a2 << 2) ^
                (a3 << 3) ^
                (a4 << 4) ^
                (a5 << 5) ^
                (a6 << 6) ^
                (a7 << 7);


        }
    }



    void OleGenerator::Gen::compressSender(span<std::array<oc::block, 2>> sendMsg, span<oc::block> add, span<oc::block> mult)
    {

        auto bIter16 = (u16*)add.data();
        auto aIter16 = (u16*)mult.data();

        if (add.size() * 128 != sendMsg.size())
            throw RTE_LOC;
        if (mult.size() * 128 != sendMsg.size())
            throw RTE_LOC;
        using block = oc::block;

        auto shuffle = std::array<block, 16>{};
        memset(shuffle.data(), 1 << 7, sizeof(*shuffle.data()) * shuffle.size());
        for (u64 i = 0; i < 16; ++i)
            shuffle[i].set<u8>(i, 0);

        for (u64 i = 0; i < sendMsg.size(); i += 16)
        {
            block a00 = _mm_shuffle_epi8(sendMsg[i + 0][0], shuffle[0]);
            block a01 = _mm_shuffle_epi8(sendMsg[i + 1][0], shuffle[1]);
            block a02 = _mm_shuffle_epi8(sendMsg[i + 2][0], shuffle[2]);
            block a03 = _mm_shuffle_epi8(sendMsg[i + 3][0], shuffle[3]);
            block a04 = _mm_shuffle_epi8(sendMsg[i + 4][0], shuffle[4]);
            block a05 = _mm_shuffle_epi8(sendMsg[i + 5][0], shuffle[5]);
            block a06 = _mm_shuffle_epi8(sendMsg[i + 6][0], shuffle[6]);
            block a07 = _mm_shuffle_epi8(sendMsg[i + 7][0], shuffle[7]);
            block a08 = _mm_shuffle_epi8(sendMsg[i + 8][0], shuffle[8]);
            block a09 = _mm_shuffle_epi8(sendMsg[i + 9][0], shuffle[9]);
            block a10 = _mm_shuffle_epi8(sendMsg[i + 10][0], shuffle[10]);
            block a11 = _mm_shuffle_epi8(sendMsg[i + 11][0], shuffle[11]);
            block a12 = _mm_shuffle_epi8(sendMsg[i + 12][0], shuffle[12]);
            block a13 = _mm_shuffle_epi8(sendMsg[i + 13][0], shuffle[13]);
            block a14 = _mm_shuffle_epi8(sendMsg[i + 14][0], shuffle[14]);
            block a15 = _mm_shuffle_epi8(sendMsg[i + 15][0], shuffle[15]);

            block b00 = _mm_shuffle_epi8(sendMsg[i + 0][1], shuffle[0]);
            block b01 = _mm_shuffle_epi8(sendMsg[i + 1][1], shuffle[1]);
            block b02 = _mm_shuffle_epi8(sendMsg[i + 2][1], shuffle[2]);
            block b03 = _mm_shuffle_epi8(sendMsg[i + 3][1], shuffle[3]);
            block b04 = _mm_shuffle_epi8(sendMsg[i + 4][1], shuffle[4]);
            block b05 = _mm_shuffle_epi8(sendMsg[i + 5][1], shuffle[5]);
            block b06 = _mm_shuffle_epi8(sendMsg[i + 6][1], shuffle[6]);
            block b07 = _mm_shuffle_epi8(sendMsg[i + 7][1], shuffle[7]);
            block b08 = _mm_shuffle_epi8(sendMsg[i + 8][1], shuffle[8]);
            block b09 = _mm_shuffle_epi8(sendMsg[i + 9][1], shuffle[9]);
            block b10 = _mm_shuffle_epi8(sendMsg[i + 10][1], shuffle[10]);
            block b11 = _mm_shuffle_epi8(sendMsg[i + 11][1], shuffle[11]);
            block b12 = _mm_shuffle_epi8(sendMsg[i + 12][1], shuffle[12]);
            block b13 = _mm_shuffle_epi8(sendMsg[i + 13][1], shuffle[13]);
            block b14 = _mm_shuffle_epi8(sendMsg[i + 14][1], shuffle[14]);
            block b15 = _mm_shuffle_epi8(sendMsg[i + 15][1], shuffle[15]);

            a00 = a00 ^ a08;
            a01 = a01 ^ a09;
            a02 = a02 ^ a10;
            a03 = a03 ^ a11;
            a04 = a04 ^ a12;
            a05 = a05 ^ a13;
            a06 = a06 ^ a14;
            a07 = a07 ^ a15;

            b00 = b00 ^ b08;
            b01 = b01 ^ b09;
            b02 = b02 ^ b10;
            b03 = b03 ^ b11;
            b04 = b04 ^ b12;
            b05 = b05 ^ b13;
            b06 = b06 ^ b14;
            b07 = b07 ^ b15;

            a00 = a00 ^ a04;
            a01 = a01 ^ a05;
            a02 = a02 ^ a06;
            a03 = a03 ^ a07;

            b00 = b00 ^ b04;
            b01 = b01 ^ b05;
            b02 = b02 ^ b06;
            b03 = b03 ^ b07;

            a00 = a00 ^ a02;
            a01 = a01 ^ a03;

            b00 = b00 ^ b02;
            b01 = b01 ^ b03;

            a00 = a00 ^ a01;
            b00 = b00 ^ b01;

            a00 = _mm_slli_epi16(a00, 7);
            b00 = _mm_slli_epi16(b00, 7);

            u16 ap = _mm_movemask_epi8(a00);
            u16 bp = _mm_movemask_epi8(b00);

            assert(aIter16 < (u16*)(mult.data() + mult.size()));
            assert(bIter16 < (u16*)(add.data() + add.size()));

            *aIter16++ = ap ^ bp;
            *bIter16++ = ap;
        }
    }

    void OleGenerator::Gen::compressRecver(oc::BitVector& bv, span<oc::block> recvMsg, span<oc::block> add, span<oc::block> mult)
    {
        auto aIter16 = (u16*)add.data();
        auto bIter16 = (u16*)mult.data();

        if (bv.size() != recvMsg.size())
            throw RTE_LOC;
        if (add.size() * 128 != recvMsg.size())
            throw RTE_LOC;
        if (mult.size() * 128 != recvMsg.size())
            throw RTE_LOC;
        using block = oc::block;

        auto shuffle = std::array<block, 16>{};
        memset(shuffle.data(), 1 << 7, sizeof(*shuffle.data()) * shuffle.size());
        for (u64 i = 0; i < 16; ++i)
            shuffle[i].set<u8>(i, 0);

        memcpy(bIter16, bv.data(), bv.sizeBytes());

        for (u64 i = 0; i < recvMsg.size(); i += 16)
        {
            // _mm_shuffle_epi8(a, b): 
            //     FOR j := 0 to 15
            //         i: = j * 8
            //         IF b[i + 7] == 1
            //             dst[i + 7:i] : = 0
            //         ELSE
            //             index[3:0] : = b[i + 3:i]
            //             dst[i + 7:i] : = a[index * 8 + 7:index * 8]
            //         FI
            //     ENDFOR

            // _mm_sll_epi16 : shifts 16 bit works left
            // _mm_movemask_epi8: packs together the MSG

            block a00 = _mm_shuffle_epi8(recvMsg[i + 0], shuffle[0]);
            block a01 = _mm_shuffle_epi8(recvMsg[i + 1], shuffle[1]);
            block a02 = _mm_shuffle_epi8(recvMsg[i + 2], shuffle[2]);
            block a03 = _mm_shuffle_epi8(recvMsg[i + 3], shuffle[3]);
            block a04 = _mm_shuffle_epi8(recvMsg[i + 4], shuffle[4]);
            block a05 = _mm_shuffle_epi8(recvMsg[i + 5], shuffle[5]);
            block a06 = _mm_shuffle_epi8(recvMsg[i + 6], shuffle[6]);
            block a07 = _mm_shuffle_epi8(recvMsg[i + 7], shuffle[7]);
            block a08 = _mm_shuffle_epi8(recvMsg[i + 8], shuffle[8]);
            block a09 = _mm_shuffle_epi8(recvMsg[i + 9], shuffle[9]);
            block a10 = _mm_shuffle_epi8(recvMsg[i + 10], shuffle[10]);
            block a11 = _mm_shuffle_epi8(recvMsg[i + 11], shuffle[11]);
            block a12 = _mm_shuffle_epi8(recvMsg[i + 12], shuffle[12]);
            block a13 = _mm_shuffle_epi8(recvMsg[i + 13], shuffle[13]);
            block a14 = _mm_shuffle_epi8(recvMsg[i + 14], shuffle[14]);
            block a15 = _mm_shuffle_epi8(recvMsg[i + 15], shuffle[15]);

            a00 = a00 ^ a08;
            a01 = a01 ^ a09;
            a02 = a02 ^ a10;
            a03 = a03 ^ a11;
            a04 = a04 ^ a12;
            a05 = a05 ^ a13;
            a06 = a06 ^ a14;
            a07 = a07 ^ a15;

            a00 = a00 ^ a04;
            a01 = a01 ^ a05;
            a02 = a02 ^ a06;
            a03 = a03 ^ a07;

            a00 = a00 ^ a02;
            a01 = a01 ^ a03;

            a00 = a00 ^ a01;

            a00 = _mm_slli_epi16(a00, 7);

            u16 ap = _mm_movemask_epi8(a00);

            *aIter16++ = ap;
        }
    }
}