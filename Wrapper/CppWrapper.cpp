#include "CppWrapper.h"

namespace secJoin
{
    using json = nlohmann::json;
    void testApi(std::string& str)
    {
        std::cout << str << std::endl;
    }

    State* initState(std::string& csvPath, std::string& visaMetaDataPath, std::string& clientMetaDataPath,
        std::string& visaJoinCols, std::string& clientJoinCols, std::string& selectVisaCols,
        std::string& selectClientCols, bool isUnique,
        bool verbose, bool mock, bool debug)
    {
        State* cState = new State;
        oc::u64 lRowCount = 0, rRowCount = 0, 
            lColCount = 0, rColCount = 0;

        // if(debug)
        // {
        //     cState->mProtocol = 
        //         validateCols(visaJoinCols, clientJoinCols, selectVisaCols, selectClientCols, 
        //                     cState->mSock, isUnique) | macoro::make_eager();
        // }


        // Current assumption are that Visa always provides table with unique keys 
        // Which means Visa always has to be left Table
        getFileInfo(visaMetaDataPath, cState->mLColInfo, lRowCount, lColCount);
        getFileInfo(clientMetaDataPath, cState->mRColInfo, rRowCount, lColCount);
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
            oc::u32 number = stoi(word);
            selectCols.emplace_back(cState->mLTable[number]);
        }

        std::stringstream clientStr(std::move(selectClientCols));
        while (getline(clientStr, word, ','))
        {
            oc::u32 number = stoi(word) - lColCount;
            selectCols.emplace_back(cState->mRTable[number]);
        }

        // Initializing the join protocol
        cState->mPrng.SetSeed(oc::sysRandomSeed());
        cState->mJoin.mInsecurePrint = verbose;
        cState->mJoin.mInsecureMockSubroutines = mock;

        // Current assumption are that Visa always provides table with unique keys 
        // Which means Visa always has to be left Table
        if (isUnique)
            cState->mOle.fakeInit(OleGenerator::Role::Sender);
        else
            cState->mOle.fakeInit(OleGenerator::Role::Receiver);


        cState->mProtocol =
            cState->mJoin.join(lJoinCol, rJoinCol, selectCols,
                cState->mJoinTable, cState->mPrng, cState->mOle, cState->mSock) | macoro::make_eager();

        return cState;

    }

    void validateColumns()
    {

    }


    std::vector<oc::u8> runProtocol(State* cState, std::vector<oc::u8>& buff)
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

    void releaseState(State* state)
    {
        delete state;
    }

    bool isProtocolReady(State* cState)
    {
        return cState->mProtocol.is_ready();

    }

    void getOtherShare(State* cState, bool isUnique, bool isAgg)
    {

        Table& table = cState->mJoinTable;
        if(isAgg)
            table = cState->mAggTable;

        // Assuming Visa always receives the client's share
        if (isUnique)
        {
            cState->mProtocol = revealLocal(cState->mJoinTable, cState->mSock, cState->mOutTable)
                | macoro::make_eager();
        }
        else
        {
            cState->mProtocol = revealRemote(cState->mJoinTable, cState->mSock)
                | macoro::make_eager();
        }
    }

    void getJoinTable(State* cState, std::string csvPath, std::string metaDataPath, bool isUnique)
    {
        writeFileInfo(metaDataPath, cState->mOutTable);
        writeFileData(csvPath, cState->mOutTable);
    }


    void aggFunc(State* cState, std::string jsonString)
    {
        nlohmann::json j = json::parse(jsonString);
        
        if(!j[AVERAGE_JSON_LITERAL].empty())
        {
            if(j[GROUP_BY_JSON_LITERAL].empty())
            {
                std::string temp("Group By Table not present in json for Average operation\n");
                throw std::runtime_error(temp + LOCATION);
            }

            std::vector<secJoin::ColRef> avgCols;
            std::string avgColNm;
            std::string temp = j[AVERAGE_JSON_LITERAL];
            std::stringstream avgColNmList(temp);
            while (getline(avgColNmList, avgColNm, ','))
                avgCols.emplace_back(cState->mJoinTable[avgColNm]);

            
            std::string grpByColNm = j[GROUP_BY_JSON_LITERAL];
            secJoin::ColRef grpByCol = cState->mJoinTable[grpByColNm];


            cState->mProtocol =
            cState->mAvg.avg( grpByCol, avgCols, cState->mAggTable,
                cState->mPrng, cState->mOle, cState->mSock) | macoro::make_eager();
    
        }

    }

}
