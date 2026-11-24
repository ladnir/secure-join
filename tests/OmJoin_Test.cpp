

#include "OmJoin_Test.h"
#include "secure-join/Join/OmJoin.h"
#include "secure-join/Util/Util.h" 
#include "cryptoTools/Common/TestCollection.h"
#include "coproto/Socket/BufferingSocket.h"

using namespace secJoin;

void OmJoin_loadKeys_Test()
{

    u64 n0 = 321;
    u64 n1 = 423;
    u64 m = 17;
    Table leftTable, rightTable;
    leftTable.mColumns.emplace_back("name", ColumnType::Int, m);
    rightTable.mColumns.emplace_back("name", ColumnType::Int, m);

    leftTable.mColumns[0].mData.resize(n0, m);
    rightTable.mColumns[0].mData.resize(n1, m);

    auto& ld = leftTable.mColumns[0].mData;
    auto& rd = rightTable.mColumns[0].mData;

    PRNG prng(oc::ZeroBlock);
    prng.get(ld.data(), ld.size());
    prng.get(rd.data(), rd.size());
    ld.trim();
    rd.trim();

    ColRef left(leftTable, leftTable.mColumns[0]);
    ColRef right(rightTable, rightTable.mColumns[0]);

    auto res = OmJoin{}.loadKeys(left, right, oc::ZeroBlock);

    for (u64 i = 0; i < n0; ++i)
    {
        if (memcmp(res[i].data(), ld[i].data(), ld.bytesPerEntry()))
            throw RTE_LOC;
    }
    for (u64 i = 0; i < n1; ++i)
    {
        if (memcmp(res[i + n0].data(), rd[i].data(), rd.bytesPerEntry()))
            throw RTE_LOC;
    }
}

void OmJoin_getControlBits_Test(const oc::CLP& cmd)
{
    u64 n = 342;
    // u64 m = 32;
    u64 offset = 2;
    u64 keyBitCount = 21;

    auto sock = coproto::LocalAsyncSocket::makePair();

    BinMatrix k(n, offset * 8 + keyBitCount), kk[2], cc[2];
    PRNG prng(oc::ZeroBlock);
    prng.get(k.data(), k.size());

    std::vector<u8> exp(n);
    for (u64 i = 1; i < n; ++i)
    {
        exp[i] = prng.getBit();
        if (exp[i])
        {
			osuCrypto::copyBytes(k[i].subspan(offset), k[i - 1].subspan(offset));
            //me mcpy(k.data(i) + offset, k.data(i - 1) + offset, k.cols() - offset);
        }
    }

    // k.trim();

    share(k, kk[0], kk[1], prng);

    CorGenerator ole0, ole1;
    ole0.init(sock[0].fork(), prng, 0, 1, 1 << 18, cmd.getOr("mock", 1));
    ole1.init(sock[1].fork(), prng, 1, 1, 1 << 18, cmd.getOr("mock", 1));

    OmJoin j[2];

    j[0].mControlBitGmw.init(n, OmJoin::getControlBitsCircuit(keyBitCount), ole0);
    j[1].mControlBitGmw.init(n, OmJoin::getControlBitsCircuit(keyBitCount), ole1);


    auto r = macoro::sync_wait(macoro::when_all_ready(
        ole0.start(),
        ole1.start(),
        j[0].getControlBits(kk[0], offset, keyBitCount, sock[0], cc[0]),
        j[1].getControlBits(kk[1], offset, keyBitCount, sock[1], cc[1])));

    std::get<0>(r).result();
    std::get<1>(r).result();
    std::get<2>(r).result();
    std::get<3>(r).result();

    auto c = reveal(cc[0], cc[1]);

    if (c.mData(0))
        throw RTE_LOC;
    for (u64 i = 1; i < n; ++i)
    {
        // auto expi = memcmp(k[i - 1].data() + offset / 8, k[i].data() + offset / 8, k.bytesPerEntry() - offset / 8) == 0;
        auto act = c(i);

        if (exp[i] != act)
            throw RTE_LOC;
    }
}

void OmJoin_concatColumns_Test()
{
    u64 n0 = 234;
    u64 n1 = 333;
    Table t0, t1;
    u64 m = 13;
    BinMatrix keys(n0 + n1, m);
    t0.mColumns.emplace_back("c0", ColumnType::Int, 11);
    t0.mColumns.emplace_back("c1", ColumnType::Int, 31);
    t0.mColumns.emplace_back("c2", ColumnType::Int, 1);
    t1.mColumns.emplace_back("r0", ColumnType::Int, 11);
    t1.mColumns.emplace_back("r1", ColumnType::Int, 31);
    t1.mColumns.emplace_back("r2", ColumnType::Int, 1);

    t0.resize(n0);
    t1.resize(n1);

    PRNG prng(oc::ZeroBlock);
    for (u64 i = 0; i < t0.mColumns.size(); ++i)
    {
        prng.get(t0[i].mCol.mData.data(), t0[i].mCol.mData.size());
        t0[i].mCol.mData.trim();
    }
    prng.get(keys.data(), keys.size());
    keys.trim();

    std::vector<ColRef> select;
    select.emplace_back(t0[0]);
    select.emplace_back(t0[1]);
    select.emplace_back(t0[2]);
    select.emplace_back(t1[0]);
    select.emplace_back(t1[1]);
    select.emplace_back(t1[2]);

    BinMatrix y;
    std::vector<OmJoin::Offset> offsets;

    OmJoin j{};
    CorGenerator ole;
    auto sock = coproto::LocalAsyncSocket::makePair();
    ole.init(std::move(sock[0]), prng, 0, 1, 1 << 16, true);

    JoinQuery query{ t0[0], t1[0], select };

    j.init(query, ole);
    j.concatColumns(t0[0], select, keys, y, 1);

    for (u64 i = 0; i < n0; ++i)
    {
        // std::cout << "y" << i << " " << hex(y[i]) << " ~ " << std::flush;
        auto iter = oc::BitIterator(y.mData[i].data());
        for (u64 j = 0; j < t0.mColumns.size(); ++j)
        {
            // std::cout << hex(t0[j].mCol.mData[i]) << " " << std::flush;
            auto expIter = oc::BitIterator(t0[j].mCol.mData[i].data());
            for (u64 k = 0; k < t0[j].mCol.getBitCount(); ++k)
            {
                u8 exp = *expIter++;
                u8 act = *iter++;
                if (exp != act)
                    throw RTE_LOC;
            }

            auto rem = t0[j].mCol.getBitCount() % 8;
            if (rem)
            {
                iter = iter + (8 - rem);
            }
        }
        iter = iter + 8;
        auto expIter = oc::BitIterator(keys.mData[i].data());
        for (u64 k = 0; k < keys.bitsPerEntry(); ++k)
        {
            u8 exp = *expIter++;
            u8 act = *iter++;
            if (exp != act)
                throw RTE_LOC;
        }

        //auto rem = keys.bitsPerEntry() % 8;
        //if (rem)
        //{
        //    iter = iter + (8 - rem);
        //}
        // std::cout << std::endl;
    }
}

void OmJoin_getOutput_Test()
{

    u64 nL = 234;
    u64 nR = 125;
    u64 mL = 3, mR = 1;


    Table L, R;
    L.init(nL + nR, { {
        ColumnInfo{"l1", ColumnType::Int, 33},
        ColumnInfo{"l2", ColumnType::Int, 1},
        ColumnInfo{"l3", ColumnType::Int, 5}
        } });
    R.init(nR, { {
            ColumnInfo{"r1", ColumnType::Int, 8},
        } }
        );

    PRNG prng(oc::ZeroBlock);
    for (u64 i = 0; i < mL; ++i)
        prng.get(L.mColumns[i].data(), L.mColumns[i].size());

    for (u64 i = 0; i < mR; ++i)
        prng.get(R.mColumns[i].data(), R.mColumns[i].size());

    u64 bitCount =
        oc::roundUpTo(L[0].mCol.getBitCount(), 8) +
        oc::roundUpTo(L[1].mCol.getBitCount(), 8) +
        oc::roundUpTo(L[2].mCol.getBitCount(), 8) + 8;
    BinMatrix data(nL + nR, bitCount);

    BinMatrix isActive(nL + nR, 8);
    prng.get(isActive.data(), isActive.size());
    isActive.trim();

    std::vector<BinMatrix*> cat{ &L.mColumns[0].mData,&L.mColumns[1].mData,&L.mColumns[2].mData, &isActive };
    std::vector<OmJoin::Offset> offsets;
    for (u64 i = 0, p = 0; i < cat.size(); ++i)
    {
        offsets.emplace_back(OmJoin::Offset{ p,cat[i]->bitsPerEntry(), "L" + std::to_string(i) });
        p += oc::roundUpTo(offsets.back().mSize, 8);
    }

    OmJoin::concatColumns(data, cat, offsets);

    Table LL = L;
    for (u64 i = 0; i < LL.mColumns.size(); ++i)
        LL.mColumns[i].mData.resize(nL, LL.mColumns[i].mBitCount);

    std::vector<ColRef> select{ LL[0], LL[1], LL[2], R[0] };


    Table out;
    OmJoin::getOutput(data, select, select[0], out, offsets);


    for (u64 i = 0; i < mR; ++i)
    {
        for (u64 j = 0; j < select.size(); ++j)
        {
            auto exp = &select[j].mTable == &LL ?
                L.mColumns[j].mData.mData[i + nL] :
                R.mColumns[0].mData[i];

            auto act = out.mColumns[j].mData.mData[i];
            if (exp.size() != act.size())
                throw RTE_LOC;
            if (memcmp(exp.data(), act.data(), act.size()))
            {
                std::cout << "exp " << hex(exp) << std::endl;
                std::cout << "act " << hex(act) << std::endl;
                throw RTE_LOC;
            }
        }

        if (out.mIsActive[i] != isActive(nL + i))
            throw RTE_LOC;
    }
}


void OmJoin_join_Test(const oc::CLP& cmd)
{
    u64 nL = cmd.getOr("L", 11),
        nR = cmd.getOr("R", 8);

    u64 keySize = cmd.getOr("keySize", 9);
    bool printSteps = cmd.isSet("print");
    bool mock = cmd.getOr("mock", 1);

    auto mod = 1ull << keySize;
    Table L, R;

    L.init(nL, { {
        {"L1", ColumnType::Int, keySize},
        {"L2", ColumnType::Int, 16}
    } });
    R.init(nR, { {
        {"R1", ColumnType::Int, keySize},
        {"R2", ColumnType::Int, 7}
    } });

    std::unordered_set<u64> keys, k2;
    for (u64 i = 0; i < nL; ++i)
    {
        // u64 k 
        auto ii = (i * 3) % mod;
        if (keys.insert(ii).second == false)
            throw RTE_LOC;
        //m emcpy(&L.mColumns[0].mData.mData(i, 0), &ii, L.mColumns[0].mData.bytesPerEntry());
		osuCrypto::copyBytesMin(L.mColumns[0].mData.mData[i], ii);
        L.mColumns[1].mData.mData(i, 0) = i % 4;
        L.mColumns[1].mData.mData(i, 1) = i % 3;
    }

    for (u64 i = 0; i < nR; ++i)
    {
        auto ii = i / 2 * 4 % mod;
        //if (k2.insert(ii).second == false)
        //    throw RTE_LOC;
        //m emcpy(&R.mColumns[0].mData.mData(i, 0), &ii, R.mColumns[0].mData.bytesPerEntry());
		osuCrypto::copyBytesMin(R.mColumns[0].mData.mData[i], ii);
        // R.mColumns[0].mData.mData(i, 0) = i * 2;
        R.mColumns[1].mData.mData(i) = i % 3;
    }

    if (printSteps)
    {
        std::cout << "L\n" << L << std::endl;
        std::cout << "R\n" << R << std::endl;
    }

    PRNG prng(oc::ZeroBlock);
    std::array<Table, 2> Ls, Rs;
    L.share(Ls, prng);
    R.share(Rs, prng);

    for (auto remDummies : { false, true })
    {
        OmJoin join0, join1;

        join0.mInsecurePrint = printSteps;
        join1.mInsecurePrint = printSteps;

        join0.mInsecureMockSubroutines = mock;
        join1.mInsecureMockSubroutines = mock;

        auto sock = coproto::LocalAsyncSocket::makePair();
        CorGenerator ole0, ole1;
        ole0.init(sock[0].fork(), prng, 0, 1, 1 << 18, mock);
        ole1.init(sock[1].fork(), prng, 1, 1, 1 << 18, mock);

        PRNG prng0(oc::ZeroBlock);
        PRNG prng1(oc::OneBlock);

        Table out[2];

        oc::Timer timer;
        join0.setTimer(timer);
        join1.setTimer(timer);

        JoinQuery query0{ Ls[0][0], Rs[0][0], { Ls[0][0], Rs[0][1], Ls[0][1] } };
        JoinQuery query1{ Ls[1][0], Rs[1][0], { Ls[1][0], Rs[1][1], Ls[1][1] } };

        join0.init(query0, ole0, remDummies);
        join1.init(query1, ole1, remDummies);

        auto r = macoro::sync_wait(macoro::when_all_ready(
            ole0.start(),
            ole1.start(),
            join0.join(query0, out[0], prng0, sock[0]),
            join1.join(query1, out[1], prng1, sock[1])
        ));

        std::get<0>(r).result();
        std::get<1>(r).result();
        std::get<2>(r).result();
        std::get<3>(r).result();

        auto exp = join(L[0], R[0], { L[0], R[1], L[1] });

        auto res = reveal(out[0], out[1], remDummies);

        exp.extractActive();
        exp.sort();
        if (remDummies == false)
            res.extractActive();
        res.sort();

        if (res != exp)
        {
            {
                std::cout << "exp \n" << exp << std::endl;
                std::cout << "act \n" << res << std::endl;
                std::cout << "ful \n" << reveal(out[0], out[1], false) << std::endl;
            }
            throw RTE_LOC;
        }

        if (cmd.isSet("timing"))
            std::cout << timer << std::endl;
    }
}

void OmJoin_join_BigKey_Test(const oc::CLP& cmd)
{
    u64 nL = cmd.getOr("L", 5),
        nR = cmd.getOr("R", 8);

    bool printSteps = cmd.isSet("print");
    bool mock = cmd.getOr("mock", 1);
    u64 keySize = 100;
    Table L, R;

    L.init(nL, { {
        {"L1", ColumnType::Int, keySize},
        {"L2", ColumnType::Int, 8}
    } });
    R.init(nR, { {
        {"R1", ColumnType::Int, keySize},
        {"R2", ColumnType::Int, 8}
    } });

    std::vector<u8> buff(L[0].mCol.getByteCount());
    for (u64 i = 0; i < nL; ++i)
    {
        // u64 k 
        auto ii = i * 3;
        assert(sizeof(ii) <= buff.size());
        osuCrypto::copyBytesMin(buff, ii);
        //m emcpy(buff.data(), &ii, sizeof(ii));
        //m emcpy(&L.mColumns[0].mData.mData(i, 0), buff.data(), L.mColumns[0].mData.bytesPerEntry());
		osuCrypto::copyBytes(L.mColumns[0].mData.mData[i], buff);
        L.mColumns[1].mData.mData(i, 0) = i % 4;
        //L.mColumns[1].mData.mData(i, 1) = i % 3;
    }

    for (u64 i = 0; i < nR; ++i)
    {
        auto ii = i / 2 * 4;
        assert(sizeof(ii) <= buff.size());
		osuCrypto::copyBytesMin(buff, ii);

        //m emcpy(buff.data(), &ii, sizeof(ii));
        //m emcpy(&R.mColumns[0].mData.mData(i, 0), buff.data(), R.mColumns[0].mData.bytesPerEntry());
		osuCrypto::copyBytesMin(R.mColumns[0].mData[i], buff);
        // R.mColumns[0].mData.mData(i, 0) = i * 2;
        R.mColumns[1].mData.mData(i) = i % 3;
    }

    if (printSteps)
    {
        std::cout << "L\n" << L << std::endl;
        std::cout << "R\n" << R << std::endl;
    }

    PRNG prng(oc::ZeroBlock);
    std::array<Table, 2> Ls, Rs;
    L.share(Ls, prng);
    R.share(Rs, prng);

    for (auto remDummies : { true, false })
    {
        OmJoin join0, join1;

        join0.mInsecurePrint = printSteps;
        join1.mInsecurePrint = printSteps;

        join0.mInsecureMockSubroutines = mock;
        join1.mInsecureMockSubroutines = mock;

        CorGenerator ole0, ole1;
        auto sock = coproto::LocalAsyncSocket::makePair();
        ole0.init(sock[0].fork(), prng, 0, 1, 1 << 18, mock);
        ole1.init(sock[1].fork(), prng, 1, 1, 1 << 18, mock);



        PRNG prng0(oc::ZeroBlock);
        PRNG prng1(oc::OneBlock);

        Table out[2];

        oc::Timer timer;
        join0.setTimer(timer);
        // join1.setTimer(timer);

        // make ssp small to the test is fast.
        join0.mStatSecParam = 4;
        join1.mStatSecParam = 4;
        JoinQuery query0{ Ls[0][0], Rs[0][0], { Ls[0][0], Rs[0][1], Ls[0][1] } };
        JoinQuery query1{ Ls[1][0], Rs[1][0], { Ls[1][0], Rs[1][1], Ls[1][1] } };
        join0.init(query0, ole0, remDummies);
        join1.init(query1, ole1, remDummies);

        auto r = macoro::sync_wait(macoro::when_all_ready(
            ole0.start(),
            ole1.start(),
            join0.join(query0, out[0], prng0, sock[0]),
            join1.join(query1, out[1], prng1, sock[1])
        ));
        std::get<0>(r).result();
        std::get<1>(r).result();
        std::get<2>(r).result();
        std::get<3>(r).result();

        auto exp = join(L[0], R[0], { L[0], R[1], L[1] });

        auto res = reveal(out[0], out[1], remDummies);

        exp.extractActive();
        exp.sort();
        if (remDummies == false)
            res.extractActive();
        res.sort();

        if (res != exp)
        {
            if (printSteps)
            {
                std::cout << "exp \n" << exp << std::endl;
                std::cout << "act \n" << res << std::endl;
                std::cout << "ful \n" << reveal(out[0], out[1], false) << std::endl;
            }
            throw RTE_LOC;
        }

        if (cmd.isSet("timing"))
            std::cout << timer << std::endl;
    }


}


//void OmJoin_join_Reveal_Test(const oc::CLP& cmd)
//{
//
//    u64 nL = 4,
//        nR = 4;
//    bool printSteps = cmd.isSet("print");
//    bool mock = cmd.getOr("mock", 1);
//
//    Table L, R, LP, RP;
//
//    L.init(nL, { {
//        {"L1", ColumnType::Int, 6},
//        {"L2", ColumnType::Int, 16}
//    } });
//    R.init(nR, { {
//        {"R1", ColumnType::Int, 6},
//        {"R2", ColumnType::Int, 7}
//    } });
//
//    LP.init(nL, { {
//        {"L1", ColumnType::Int, 6},
//        {"L2", ColumnType::Int, 16}
//    } });
//    RP.init(nR, { {
//        {"R1", ColumnType::Int, 6},
//        {"R2", ColumnType::Int, 7}
//    } });
//
//    for (u64 i = 0; i < nL; ++i)
//    {
//        L.mColumns[0].mData.mData(i, 0) = i;
//        L.mColumns[1].mData.mData(i, 0) = i % 4;
//        L.mColumns[1].mData.mData(i, 1) = i % 3;
//    }
//
//    for (u64 i = 0; i < nR; ++i)
//    {
//        R.mColumns[0].mData.mData(i, 0) = i * 2;
//        R.mColumns[1].mData.mData(i) = i % 3;
//    }
//
//    PRNG prng(oc::ZeroBlock);
//    // std::array<Table, 2> Ls, Rs;
//    // share(L, Ls, prng);
//    // share(R, Rs, prng);
//
//    OmJoin join0, join1;
//
//    join0.mInsecurePrint = printSteps;
//    join1.mInsecurePrint = printSteps;
//
//    join0.mInsecureMockSubroutines = mock;
//    join1.mInsecureMockSubroutines = mock;
//
//    CorGenerator ole0, ole1;
//    auto sock = coproto::LocalAsyncSocket::makePair();
//    ole0.init(sock[0].fork(), prng, 0, 1, 1 << 18, mock);
//    ole1.init(sock[1].fork(), prng, 1, 1, 1 << 18, mock);
//
//
//    PRNG prng0(oc::ZeroBlock);
//    PRNG prng1(oc::OneBlock);
//
//    Table out[2];
//
//    auto exp = join(L[0], R[0], { L[0], R[1], L[1] });
//    oc::Timer timer;
//    join0.setTimer(timer);
//    // join1.setTimer(timer);
//
//    JoinQuery query0{ L[0], RP[0], { L[0], RP[1], L[1] } };
//    JoinQuery query1{ LP[0], R[0], { LP[0], R[1], LP[1] } };
//    join0.init(query0, ole0);
//    join1.init(query1, ole1);
//
//    auto r = macoro::sync_wait(macoro::when_all_ready(
//        ole0.start(),
//        ole1.start(),
//        join0.join(query0, out[0], prng0, sock[0]),
//        join1.join(query1, out[1], prng1, sock[1])
//    ));
//    std::get<0>(r).result();
//    std::get<1>(r).result();
//    // auto res = reveal(out[0], out[1]);
//
//    Table outTable;
//    auto res = macoro::sync_wait(macoro::when_all_ready(
//        out[0].revealLocal(sock[0], outTable),
//        out[1].revealRemote(sock[1])));
//
//    std::get<1>(res).result();
//    std::get<0>(res).result();
//
//    if (outTable != exp)
//    {
//        std::cout << "exp \n" << exp << std::endl;
//        std::cout << "act \n" << outTable << std::endl;
//        throw RTE_LOC;
//    }
//
//    if (cmd.isSet("timing"))
//        std::cout << timer << std::endl;
//
////}



// This example demonstates how one can get and manually send the protocol messages
// that are generated. This communicate method is one possible way of doing this.
// It takes a protocol that has been started and coproto buffering socket as input.
// It alternates between "sending" and "receiving" protocol messages. Instead of
// sending the messages on a socket, this program writes them to a file and the other
// party reads that file to get the message. In a real program the communication could 
// handled in any way the user decides.
auto communicate(
    macoro::eager_task<>& protocol,
    bool sender,
    coproto::BufferingSocket& sock,
    bool verbose)
{

    int s = 0, r = 0;
    std::string me = sender ? "sender" : "recver";
    std::string them = !sender ? "sender" : "recver";

    // write any outgoing data to a file me_i.bin where i in the message index.
    auto write = [&]()
        {
            // the the outbound messages that the protocol has generated.
            // This will consist of all the outbound messages that can be 
            // generated without receiving the next inbound message.
            auto b = sock.getOutbound();

            // If we do have outbound messages, then lets write them to a file.
            if (b && b->size())
            {
                std::ofstream message;
                auto temp = me + ".tmp";
                auto file = me + "_" + std::to_string(s) + ".bin";
                message.open(temp, std::ios::binary | std::ios::trunc);
                message.write((char*)b->data(), b->size());
                message.close();

                if (verbose)
                {
                    // optional for debug purposes.
                    oc::RandomOracle hash(16);
                    hash.Update(b->data(), b->size());
                    oc::block h; hash.Final(h);

                    std::cout << me << " write " << std::to_string(s) << ", " << b->size() << " bytes " << h << "\n";
                }

                if (rename(temp.c_str(), file.c_str()) != 0)
                    std::cout << me << " file renamed failed\n";
                else if (verbose)
                    std::cout << me << " file renamed successfully\n";

                ++s;
            }

        };

    // write incoming data from a file them_i.bin where i in the message index.
    auto read = [&]() {

        std::ifstream message;
        auto file = them + "_" + std::to_string(r) + ".bin";
        while (message.is_open() == false)
        {
            message.open(file, std::ios::binary);
            if ((message.is_open() == false))
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        auto fsize = message.tellg();
        message.seekg(0, std::ios::end);
        fsize = message.tellg() - fsize;
        message.seekg(0, std::ios::beg);
        std::vector<oc::u8> buff(fsize);
        message.read((char*)buff.data(), fsize);
        message.close();
        std::remove(file.c_str());

        if (verbose)
        {
            oc::RandomOracle hash(16);
            hash.Update(buff.data(), buff.size());
            oc::block h; hash.Final(h);

            std::cout << me << " read " << std::to_string(r) << ", " << buff.size() << " bytes " << h << "\n";
        }
        ++r;

        // This gives this socket the message which forwards it to the protocol and
        // run the protocol forward, possibly generating more outbound protocol
        // messages.
        sock.processInbound(buff);
        };

    // The sender we generate the first message.
    if (sender)
        write();

    // While the protocol is not done we alternate between reading and writing messages.
    while (protocol.is_ready() == false)
    {
        read();
        write();
    }
}


void OmJoin_join_round_Test(const oc::CLP& cmd)
{
    u64 nL = cmd.getOr("L", 20),
        nR = cmd.getOr("R", 29);
    bool printSteps = cmd.isSet("print");
    bool mock = cmd.getOr("mock", 1);

    Table L, R;

    L.init(nL, { {
        {"L1", ColumnType::Int, 9},
        {"L2", ColumnType::Int, 8}
    } });
    R.init(nR, { {
        {"R1", ColumnType::Int, 9},
        {"R2", ColumnType::Int, 7}
    } });

    for (u64 i = 0; i < nL; ++i)
    {
        // u64 k 
        osuCrypto::copyBytesMin(L.mColumns[0].mData.mData[i], i);
        L.mColumns[1].mData.mData(i, 0) = i % 4;
        //L.mColumns[1].mData.mData(i, 1) = i % 3;
    }

    for (u64 i = 0; i < nR; ++i)
    {
        auto ii = i * 2;
        osuCrypto::copyBytesMin(R.mColumns[0].mData.mData[i], ii);
        // R.mColumns[0].mData.mData(i, 0) = i * 2;
        R.mColumns[1].mData.mData(i) = i % 3;
    }

    PRNG prng(oc::ZeroBlock);
    std::array<Table, 2> Ls, Rs;
    L.share(Ls, prng);
    R.share(Rs, prng);

    OmJoin join0, join1;

    join0.mInsecurePrint = printSteps;
    join1.mInsecurePrint = printSteps;

    join0.mInsecureMockSubroutines = mock;
    join1.mInsecureMockSubroutines = mock;

    CorGenerator ole0, ole1;
    std::array<coproto::BufferingSocket, 2> sock;

    auto ss = coproto::LocalAsyncSocket::makePair();
    ole0.init(ss[0].fork(), prng, 0, 1, 1 << 18, mock);
    ole1.init(ss[1].fork(), prng, 1, 1, 1 << 18, mock);


    PRNG prng0(oc::ZeroBlock);
    PRNG prng1(oc::OneBlock);

    Table out[2];

    auto exp = join(L[0], R[0], { L[0], R[1], L[1] });

    oc::Timer timer;
    join0.setTimer(timer);


    JoinQuery query0{ Ls[0][0], Rs[0][0], { Ls[0][0], Rs[0][1], Ls[0][1] } };
    JoinQuery query1{ Ls[1][0], Rs[1][0], { Ls[1][0], Rs[1][1], Ls[1][1] } };
    join0.init(query0, ole0);
    join1.init(query1, ole1);

    auto proto0 = join0.join(query0, out[0], prng0, sock[0])
        | macoro::make_eager();
    auto proto1 = join1.join(query1, out[1], prng1, sock[1])
        | macoro::make_eager();


    auto t0 = ole0.start() | macoro::make_eager();
    auto t1 = ole1.start() | macoro::make_eager();

    coproto::BufferingSocket::exchangeMessages(sock[0], sock[1]);

    macoro::sync_wait(macoro::when_all_ready(
        std::move(t0),
        std::move(t1),
        std::move(proto0),
        std::move(proto1)
    ));

    auto res = reveal(out[0], out[1],true);

    res.extractActive();
    res.sort();
	exp.extractActive();
    exp.sort();

    if (res != exp)
    {
        std::cout << "exp \n" << exp << std::endl;
        std::cout << "act \n" << res << std::endl;
        throw RTE_LOC;
    }

    if (cmd.isSet("timing"))
        std::cout << timer << std::endl;
}



void OmJoin_join_csv_Test(const oc::CLP& cmd)
{

    //std::string rootPath(SEC_JOIN_ROOT_DIRECTORY);
    //std::string primaryCsvPath = rootPath + "/tests/tables/primary.csv";
    //std::string secondaryCsvPath = rootPath + "/tests/tables/secondary.csv";
    //std::string primaryMetaDataPath = rootPath + "/tests/tables/primary_meta.txt";
    //std::string clientMetaDataPath = rootPath + "/tests/tables/secondary_meta.txt";
    //std::string joinVisaCols("ID");
    //std::string joinClientCols("ID");
    //std::string selectVisaCols("Score,ID");
    //std::string selectClientCols("Balance");
    //std::string joinCsvPath = rootPath + "/tests/tables/joindata.csv";
    //std::string joinMetaPath = rootPath + "/tests/tables/joindata_meta.txt";
    //// bool isUnique = true;
    //// bool isAgg = false;
    //// bool verbose = cmd.isSet("v");
    //bool printSteps = cmd.isSet("print");
    //bool mock = !cmd.isSet("noMock");

    //oc::u64 lRowCount = 0, rRowCount = 0, lColCount = 0, rColCount = 0;
    //bool isBin;

    //std::vector<ColumnInfo> lColInfo, rColInfo;
    //getFileInfo(primaryMetaDataPath, lColInfo, lRowCount, lColCount, isBin);
    //getFileInfo(clientMetaDataPath, rColInfo, rRowCount, rColCount, isBin);

    //Table L, R;

    //L.init(lRowCount, lColInfo);
    //R.init(rRowCount, rColInfo);

    //populateTable(L, primaryCsvPath, lRowCount, isBin);
    //populateTable(R, secondaryCsvPath, rRowCount, isBin);

    //if (printSteps)
    //{
    //    std::cout << "L\n" << L << std::endl;
    //    std::cout << "R\n" << R << std::endl;
    //}

    //PRNG prng(oc::ZeroBlock);
    //std::array<Table, 2> Ls, Rs;
    //share(L, Ls, prng);
    //share(R, Rs, prng);

    //for (auto remDummies : { false })
    //{
    //    OmJoin join0, join1;

    //    join0.mInsecurePrint = printSteps;
    //    join1.mInsecurePrint = printSteps;

    //    join0.mInsecureMockSubroutines = mock;
    //    join1.mInsecureMockSubroutines = mock;

    //    PRNG prng0(oc::ZeroBlock);
    //    PRNG prng1(oc::OneBlock);
    //    auto sock = coproto::LocalAsyncSocket::makePair();
    //    CorGenerator ole0, ole1;

    //    ole0.init(sock[0].fork(), prng0, 0, 1, 1 << 20, mock);
    //    ole1.init(sock[1].fork(), prng1, 1, 1, 1 << 20, mock);

    //    Table out[2];

    //    auto exp = join(L[joinVisaCols], R[joinClientCols], { L[0], R[2], L[1] });
    //    oc::Timer timer;
    //    join0.setTimer(timer);
    //    join1.setTimer(timer);
    //    JoinQuery query0{ Ls[0][joinVisaCols], Rs[0][joinClientCols], { Ls[0][0], Rs[0][2], Ls[0][1] } };
    //    JoinQuery query1{ Ls[1][joinVisaCols], Rs[1][joinClientCols], { Ls[1][0], Rs[1][2], Ls[1][1] } };
    //    join0.init(query0, ole0, remDummies);
    //    join1.init(query1, ole1, remDummies);

    //    auto r = macoro::sync_wait(macoro::when_all_ready(
    //        ole0.start(),
    //        ole1.start(),
    //        join0.join(query0, out[0], prng0, sock[0]),
    //        join1.join(query1, out[1], prng1, sock[1])
    //    ));
    //    std::get<0>(r).result();
    //    std::get<1>(r).result();
    //    std::get<2>(r).result();
    //    std::get<3>(r).result();

    //    auto res = reveal(out[0], out[1]);

    //    if (res != exp)
    //    {
    //        std::cout << "exp \n" << exp << std::endl;
    //        std::cout << "act \n" << res << std::endl;
    //        // std::cout << "ful \n" << reveal(out[0], out[1], false) << std::endl;
    //        throw RTE_LOC;
    //    }

        //if (cmd.isSet("timing"))
        //    std::cout << timer << std::endl;
    //}

}