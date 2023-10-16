#include "CppWrapper.h"
#include "state.h"
#include "secure-join/Util/CSVParser.h"

namespace secJoin
{

    void testApi(std::string& str)
    {
        std::cout << str << std::endl;
    }

    WrapperState* initState(std::string& csvPath, std::string& visaMetaDataPath, std::string& clientMetaDataPath,
        std::string& visaJoinCols, std::string& clientJoinCols, std::string& selectVisaCols,
        std::string& selectClientCols, bool isUnique,
        bool verbose, bool mock)
    {
        WrapperState* cState = new WrapperState;
        oc::u64 lRowCount = 0, rRowCount = 0;


        // Current assumption are that Visa always provides table with unique keys 
        // Which means Visa always has to be left Table
        getFileInfo(visaMetaDataPath, cState->mLColInfo, lRowCount);
        getFileInfo(clientMetaDataPath, cState->mRColInfo, rRowCount);
        cState->mLTable.init(lRowCount, cState->mLColInfo);
        cState->mRTable.init(rRowCount, cState->mRColInfo);
        if (isUnique)
            populateTable(cState->mLTable, csvPath, lRowCount);
        else
            populateTable(cState->mRTable, csvPath, rRowCount);


        // Current Assumptions is that there is only one Join Columns
        auto lJoinCol = cState->mLTable[visaJoinCols];
        auto rJoinCol = cState->mRTable[clientJoinCols];

        // Constructing Select Cols
        std::vector<secJoin::ColRef>& selectCols = cState->selectCols;
        std::string word;
        std::stringstream visaStr(std::move(selectVisaCols));
        while (getline(visaStr, word, ','))
        {
            selectCols.emplace_back(cState->mLTable[word]);
        }

        std::stringstream clientStr(std::move(selectClientCols));
        while (getline(clientStr, word, ','))
        {
            selectCols.emplace_back(cState->mRTable[word]);
        }

        // Initializing the join protocol
        cState->mPrng.SetSeed(oc::ZeroBlock); // Make Change
        cState->mJoin.mInsecurePrint = verbose;
        cState->mJoin.mInsecureMockSubroutines = mock;

        // Current assumption are that Visa always provides table with unique keys 
        // Which means Visa always has to be left Table
        if (isUnique)
            cState->mOle.init(cState->mSock.fork(), cState->mPrng, 0, 1 << 14, mock);
            //cState->mOle.mock(CorGenerator::Role::Sender);
        else
            cState->mOle.init(cState->mSock.fork(), cState->mPrng, 1, 1 << 14, mock);
            //cState->mOle.mock(CorGenerator::Role::Receiver);


        cState->mProtocol =
            cState->mJoin.join(lJoinCol, rJoinCol, selectCols,
                cState->mShareTable, cState->mPrng, cState->mOle, cState->mSock) | macoro::make_eager();

        return cState;

    }


    std::vector<oc::u8> runJoin(WrapperState* cState, std::vector<oc::u8>& buff)
    {
        cState->mSock.processInbound(buff);

        auto b = cState->mSock.getOutbound();

        if (!b.has_value())
        {
            macoro::sync_wait(cState->mProtocol);
            throw RTE_LOC;
        }

        return *b;
        // return b.value();

    }

    void releaseState(WrapperState* state)
    {
        delete state;
    }

    bool isProtocolReady(WrapperState* cState)
    {
        return cState->mProtocol.is_ready();

    }

    void getOtherShare(WrapperState* cState, bool isUnique)
    {
        // Assuming Visa always receives the client's share
        if (isUnique)
        {
            cState->mProtocol = revealLocal(cState->mShareTable, cState->mSock, cState->mOutTable)
                | macoro::make_eager();
        }
        else
        {
            cState->mProtocol = revealRemote(cState->mShareTable, cState->mSock)
                | macoro::make_eager();
        }
    }

    void getJoinTable(WrapperState* cState, std::string csvPath, std::string metaDataPath, bool isUnique)
    {
        writeFileInfo(metaDataPath, cState->mOutTable);
        writeFileData(csvPath, cState->mOutTable);
    }

}
