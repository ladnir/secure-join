#include "CppWrapper.h"

namespace secJoin
{
    //using json = nlohmann::json;
    void testApi(std::string& str)
    {
        std::cout << str << std::endl;
    }


    oc::u64 getRColIndex(oc::u64 relativeIndex, oc::u64 lColCount, oc::u64 rColCount)
    {
        oc::u64 index = relativeIndex - lColCount;

        if( index < 0 || index >= rColCount)
        {
            std::string temp = "Right Column relative index = "+ std::to_string(relativeIndex) 
                + " is not present in the right table" + "\n" + LOCATION;
            throw std::runtime_error(temp);
        }

        return index;
    }

    WrapperState* initState(std::string& csvPath, std::string& visaMetaDataPath, std::string& clientMetaDataPath,
        std::vector<std::string>& literals, std::vector<oc::i64>& opInfo, bool isUnique,
        bool verbose, bool mock, bool debug)
    {
        auto cState = std::make_unique<WrapperState>();
        cState->mLiterals = literals; //  This will fail, need to make a copy of literals
        oc::u64 lRowCount = 0, rRowCount = 0, 
            lColCount = 0, rColCount = 0;

        parseColsArray(cState.get(), opInfo, verbose);
        updateSelectCols(cState.get(), verbose);
        
        // Current assumption are that Visa always provides table with unique keys 
        // Which means Visa always has to be left Table
        getFileInfo(visaMetaDataPath, cState->mLColInfo, lRowCount, lColCount);
        getFileInfo(clientMetaDataPath, cState->mRColInfo, rRowCount, rColCount);
        cState->mLTable.init(lRowCount, cState->mLColInfo);
        cState->mRTable.init(rRowCount, cState->mRColInfo);
        if (isUnique)
            populateTable(cState->mLTable, csvPath, lRowCount);
        else
            populateTable(cState->mRTable, csvPath, rRowCount);

        // Current Assumptions is that there is only one Join Columns
        auto lJoinCol = cState->mLTable[cState->mJoinCols[0]];
        oc::u64 index = getRColIndex(cState->mJoinCols[1], lColCount, rColCount);
        auto rJoinCol = cState->mRTable[index];

        // Constructing Select Cols
        std::vector<secJoin::ColRef> selectCols;
        selectCols.reserve(cState->mSelectCols.size());

        for(u64 i=0; i < cState->mSelectCols.size(); i++)
        {
            u64 colNum = cState->mSelectCols[i];
            if(colNum < lColCount)            
                selectCols.emplace_back(cState->mLTable[colNum]);
            else if(colNum < (lColCount + rColCount) ) 
            {
                oc::u64 index = getRColIndex(colNum, lColCount, rColCount);
                selectCols.emplace_back(cState->mRTable[index]);
            }       
            else
            {
                std::string temp = "Select Col Num = "+ std::to_string(colNum) 
                    + " is not present in any table" + "\n" + LOCATION;
                throw std::runtime_error(temp);
            }

        }
        // Create a new mapping and store the new mapping in the cState
        createNewMapping(cState->mMap, cState->mSelectCols);


        // Initializing the join protocol
        cState->mPrng.SetSeed(oc::sysRandomSeed());
        cState->mJoin.mInsecurePrint = verbose;
        cState->mJoin.mInsecureMockSubroutines = mock;

        // Current assumption are that Visa always provides table with unique keys 
        // Which means Visa always has to be left Table
        if (isUnique)
            cState->mOle.init(cState->mSock.fork(), cState->mPrng, 0, 1<<18, mock);
            //cState->mOle.mock(CorGenerator::Role::Sender);
        else
            cState->mOle.init(cState->mSock.fork(), cState->mPrng, 1, 1<<18, mock);
            //cState->mOle.mock(CorGenerator::Role::Receiver);

        cState->mProtocol =
            cState->mJoin.join(lJoinCol, rJoinCol, selectCols,
                cState->mJoinTable, cState->mPrng, cState->mOle, cState->mSock) | macoro::make_eager();

        
        return cState.release();
    }


    std::vector<oc::u8> runProtocol(WrapperState* cState, std::vector<oc::u8>& buff)
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

    void getOtherShare(WrapperState* cState, bool isUnique, bool isAgg)
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

    void getJoinTable(WrapperState* cState, std::string csvPath, std::string metaDataPath, bool isUnique)
    {
        writeFileInfo(metaDataPath, cState->mOutTable);
        writeFileData(csvPath, cState->mOutTable);
    }


    void aggFunc(WrapperState* cState)
    {
        if(cState->mAvgCols.size() > 0)
        {
            if(cState->mGroupByCols.size() == 0)
            {
                std::string temp("Group By Table not present in json for Average operation\n");
                throw std::runtime_error(temp + LOCATION);
            }

            std::vector<secJoin::ColRef> avgCols;
            std::vector<oc::u64> colList = cState->mAvgCols;
            
            for(oc::u64 i =0; i< colList.size(); i++)
            {
                oc::u64 avgColIndex = cState->mMap[cState->mAvgCols[i]];
                avgCols.emplace_back(cState->mJoinTable[avgColIndex]);
            }

            // Current Assumption we only have 
            oc::u64 groupByColIndex = cState->mMap[cState->mGroupByCols[0]];
            secJoin::ColRef grpByCol = cState->mJoinTable[groupByColIndex];


            cState->mProtocol =
                cState->mAvg.avg( grpByCol, avgCols, cState->mAggTable,
                    cState->mPrng, cState->mOle, cState->mSock) | macoro::make_eager();
    
        }

    }

}
