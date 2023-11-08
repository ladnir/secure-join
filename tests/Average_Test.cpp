#include "Average_Test.h"

//#include "nlohmann/json.hpp"

using namespace secJoin;

void Average_concatColumns_Test()
{
    u64 n0 = 234;
    Table t0;
    t0.mColumns.emplace_back("c0", TypeID::IntID, 11);
    t0.mColumns.emplace_back("c1", TypeID::IntID, 31);
    t0.mColumns.emplace_back("c2", TypeID::IntID, 5);
    t0.resize(n0);

    // Average avg;

    PRNG prng(oc::ZeroBlock);
    for (u64 i = 0; i < t0.mColumns.size(); ++i)
    {
        prng.get(t0[i].mCol.mData.data(), t0[i].mCol.mData.size());
        t0[i].mCol.mData.trim();
    }
    BinMatrix y;
    std::vector<OmJoin::Offset> offsets;

    std::vector<ColRef> averageCols;
    averageCols.emplace_back(t0[0]);
    averageCols.emplace_back(t0[1]);
    averageCols.emplace_back(t0[2]);


    CorGenerator ole;
    ole.init(coproto::Socket{}, prng, 0);
     
    Average::concatColumns( t0[0], averageCols, y, offsets, ole);
    BinMatrix ones(n0, sizeof(oc::u64) * 8);
    for(oc::u64 i = 0; i < n0; i++)
            ones(i,0) = 1;


    for (u64 i = 0; i < n0; ++i)
    {
        auto iter = oc::BitIterator(y.mData[i].data());
        
        for (u64 j = 0; j < t0.mColumns.size(); ++j)
        {
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


        auto expIter = oc::BitIterator(ones.mData[i].data());

        for (u64 k = 0; k < ones.mBitCount; ++k)
        {
            u8 exp = *expIter++;
            u8 act = *iter++;
            if (exp != act)
                throw RTE_LOC;
        }
    }

    
}



void Average_getControlBits_Test(const oc::CLP& cmd)
{
    u64 n = 342;
    u64 keyBitCount = 21;
    auto mock = cmd.getOr("mock", 1);

    auto sock = coproto::LocalAsyncSocket::makePair();

    BinMatrix keys(n, keyBitCount), kk[2], cc[2];
    PRNG prng(oc::ZeroBlock);
    prng.get(keys.data(), keys.size());

    std::vector<u8> exp(n);
    for (u64 i = 1; i < n; ++i)
    {
        exp[i] = prng.getBit();
        if (exp[i])
        {
            memcpy(keys.data(i), keys.data(i - 1), keys.cols());
        }
    }

    share(keys, kk[0], kk[1], prng);

    CorGenerator ole0, ole1;
    ole0.init(sock[0].fork(), prng, 0, 1 << 16, mock);
    ole1.init(sock[1].fork(), prng, 1, 1 << 16, mock);

    auto r = macoro::sync_wait(macoro::when_all_ready(
        Average::getControlBits( kk[0], sock[0] ,cc[0], ole0),
        Average::getControlBits( kk[1], sock[1] ,cc[1], ole1)));

    std::get<0>(r).result();
    std::get<1>(r).result();

    auto c = reveal(cc[0], cc[1]);

    if (c.mData(0))
        throw RTE_LOC;
    for (u64 i = 1; i < n; ++i)
    {
        auto act = c(i);
        if (exp[i] != act)
            throw RTE_LOC;
    }
}

void Average_avg_Test(const oc::CLP& cmd)
{
    u64 nTb = 333;
    Table tb, tbShare;

    bool printSteps = cmd.isSet("print");
    bool mock = cmd.getOr("mock", 1);

    tb.init(nTb, { {
        {"L1", TypeID::IntID, 12},
        {"L2", TypeID::IntID, 16},
        {"L3", TypeID::IntID, 16}
    } });

    tbShare.init(nTb, { {
        {"L1", TypeID::IntID, 12},
        {"L2", TypeID::IntID, 16},
        {"L3", TypeID::IntID, 16}
    } });

    for (u64 i = 0; i < nTb; ++i)
    {
        tb.mColumns[0].mData.mData(i, 0) = i % 5 ;
        tb.mColumns[1].mData.mData(i, 0) = i % 4;
        tb.mColumns[1].mData.mData(i, 1) = i % 4;
        tb.mColumns[2].mData.mData(i, 0) = i % 4;
        tb.mColumns[2].mData.mData(i, 1) = i % 4;
    }

    Average avg1, avg2;

    avg1.mInsecurePrint = printSteps;
    avg2.mInsecurePrint = printSteps;

    avg1.mInsecureMockSubroutines = mock;
    avg2.mInsecureMockSubroutines = mock;

    auto sock = coproto::LocalAsyncSocket::makePair();

    PRNG prng0(oc::ZeroBlock);
    PRNG prng1(oc::OneBlock);
    CorGenerator ole0, ole1;
    ole0.init(sock[0].fork(), prng0, 0, 1 << 16, mock);
    ole1.init(sock[1].fork(), prng1, 1, 1 << 16, mock);

    Table out[2];

    // crashes.
    throw RTE_LOC;
    auto r = macoro::sync_wait(macoro::when_all_ready(
        avg1.avg(tb[0], { tb[1], tb[2] }, out[0], prng0, ole0, sock[0]),
        avg2.avg(tbShare[0], { tbShare[1], tbShare[2] }, out[1], prng1, ole1, sock[1])
    ));
    std::get<1>(r).result();
    std::get<0>(r).result();

    auto res = reveal(out[0], out[1]);
    
    auto exp = average(tb[0], { tb[1], tb[2] });

    if (res != exp)
    {
        std::cout << "exp \n" << exp << std::endl;
        std::cout << "act \n" << res << std::endl;
        std::cout << "ful \n" << reveal(out[0], out[1], false) << std::endl;
        throw RTE_LOC;
    }
}

void Average_avg_csv_Test(const oc::CLP& cmd)
{
    std::string rootPath(SEC_JOIN_ROOT_DIRECTORY);
    std::string visaCsvPath = rootPath + "/tests/tables/visa.csv";
    std::string bankCsvPath = rootPath + "/tests/tables/bank.csv";
    std::string visaMetaDataPath = rootPath + "/tests/tables/visa_meta.txt";
    std::string clientMetaDataPath = rootPath + "/tests/tables/bank_meta.txt";
    std::string joinVisaCols("PAN");
    std::string joinClientCols("PAN");
    std::string selectVisaCols("Risk_Score,PAN");
    std::string selectClientCols("Balance");
    std::string joinCsvPath = rootPath + "/tests/tables/joindata.csv";
    std::string joinMetaPath = rootPath + "/tests/tables/joindata_meta.txt";
    std::string jsonString = "{ \"Average\": \"Risk_Score,Balance\", \"Group by\": \"PAN\" }";
    // bool isUnique = true;
    // bool isAgg = false;
    // bool verbose = cmd.isSet("v");
    bool printSteps = cmd.isSet("print");
    bool mock = cmd.getOr("mock", 1);


    oc::u64 lRowCount = 0, rRowCount = 0, 
        lColCount = 0, rColCount = 0;

    std::vector<ColumnInfo> lColInfo, rColInfo;
    getFileInfo(visaMetaDataPath, lColInfo, lRowCount, lColCount);
    getFileInfo(clientMetaDataPath, rColInfo, rRowCount, rColCount);

    Table L, R;

    L.init( lRowCount, lColInfo);
    R.init( rRowCount, rColInfo);

    populateTable(L, visaCsvPath, lRowCount);
    populateTable(R, bankCsvPath, rRowCount);

    // if (printSteps)
    // {
    //     std::cout << "L\n" << L << std::endl;
    //     std::cout << "R\n" << R << std::endl;
    // }

    PRNG prng(oc::ZeroBlock);
    std::array<Table, 2> Ls, Rs;
    share(L, Ls, prng);
    share(R, Rs, prng);

    OmJoin join0, join1;

    join0.mInsecurePrint = printSteps;
    join1.mInsecurePrint = printSteps;

    join0.mInsecureMockSubroutines = mock;
    join1.mInsecureMockSubroutines = mock;

    CorGenerator ole0, ole1;
    PRNG prng0(oc::ZeroBlock);
    PRNG prng1(oc::OneBlock);
    auto sock = coproto::LocalAsyncSocket::makePair();
    ole0.init(sock[0].fork(), prng0, 0, 1 << 16, mock);
    ole1.init(sock[1].fork(), prng1, 1, 1 << 16, mock);


    Table tempOut[2], out[2];

    auto joinExp = join(L[0], R[1], { L[0], R[2], L[1] });
    oc::Timer timer;
    // join0.setTimer(timer);
    // join1.setTimer(timer);


    auto r = macoro::sync_wait(macoro::when_all_ready(
        join0.join(Ls[0][0], Rs[0][1], { Ls[0][0], Rs[0][2], Ls[0][1] }, tempOut[0], prng0, ole0, sock[0]),
        join1.join(Ls[1][0], Rs[1][1], { Ls[1][0], Rs[1][2], Ls[1][1] }, tempOut[1], prng1, ole1, sock[1])
    ));
    std::get<0>(r).result();
    std::get<1>(r).result();

    auto res = reveal(tempOut[0], tempOut[1]);

    if (res != joinExp)
    {
        std::cout << "exp \n" << joinExp << std::endl;
        std::cout << "act \n" << res << std::endl;
        // std::cout << "ful \n" << reveal(out[0], out[1], false) << std::endl;
        throw RTE_LOC;
    }

    Average avg1, avg2;

    avg1.mInsecurePrint = printSteps;
    avg2.mInsecurePrint = printSteps;

    avg1.mInsecureMockSubroutines = mock;
    avg2.mInsecureMockSubroutines = mock;

    //nlohmann::json j = nlohmann::json::parse(jsonString);

    //std::array<std::vector<secJoin::ColRef>,3> avgCols;
    //std::string avgColNm;
    //// std::string temp = j[secJoin::AVERAGE_JSON_LITERAL];
    //std::string temp = j[AVERAGE_JSON_LITERAL];
    //std::stringstream avgColNmList(temp);
    //while (getline(avgColNmList, avgColNm, ','))
    //{
    //    avgCols[0].emplace_back(tempOut[0][avgColNm]);
    //    avgCols[1].emplace_back(tempOut[1][avgColNm]);
    //    avgCols[2].emplace_back(joinExp[avgColNm]);
    //}

    //// std::string grpByColNm = j[secJoin::GROUP_BY_JSON_LITERAL];
    //std::string grpByColNm = j[GROUP_BY_JSON_LITERAL];

    //auto r1 = macoro::sync_wait(macoro::when_all_ready(
    //    avg1.avg(tempOut[0][grpByColNm], avgCols[0], out[0], prng0, ole0, sock[0]),
    //    avg2.avg(tempOut[1][grpByColNm], avgCols[1], out[1], prng1, ole1, sock[1])
    //));
    //std::get<1>(r1).result();
    //std::get<0>(r1).result();

    //auto res1 = reveal(out[0], out[1]);
    //auto avgExp = average(joinExp[grpByColNm], avgCols[2]);

    //if (res1 != avgExp)
    //{
    //    std::cout << "exp \n" << avgExp << std::endl;
    //    std::cout << "act \n" << res1 << std::endl;
    //    // std::cout << "ful \n" << reveal(out[0], out[1], false) << std::endl;
    //    throw RTE_LOC;
    //}
    
    // if (cmd.isSet("timing"))
    //     std::cout << timer << std::endl;

}