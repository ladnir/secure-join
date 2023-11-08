
#include "CWrapper_Test.h"
#include "secure-join/config.h"
#include "Wrapper/state.h"

std::vector<secJoin::ColRef> genSelectCols(secJoin::WrapperState* visaState, secJoin::WrapperState* bankState)
{
    std::vector<secJoin::ColRef> select;
    auto lColCount = visaState->mLTable.cols();
    auto rColCount = visaState->mRTable.cols();

    for(oc::u64 i=0; i < visaState->mSelectCols.size(); i++)
    {
        oc::u64 colNum = visaState->mSelectCols[i];
        if(colNum < lColCount)            
            select.emplace_back(visaState->mLTable[colNum]);
        else if(colNum < (lColCount + rColCount)) 
        {
            oc::u64 index = secJoin::getRColIndex(colNum, lColCount, rColCount);
            select.emplace_back(bankState->mRTable[index]);
        }       
        else
        {
            std::string temp = "Select Col Num = "+ std::to_string(colNum) 
                + " is not present in any table" + "\n" + LOCATION;
            throw std::runtime_error(temp);
        }

    }
    return select;
}   

void OmJoin_wrapper_join_test(const oc::CLP& cmd)
{

    // std::string str("Hello World!");
    // testApi(str);
    std::string rootPath(SEC_JOIN_ROOT_DIRECTORY);
    std::string visaCsvPath = rootPath + "/tests/tables/visa.csv";
    std::string bankCsvPath = rootPath + "/tests/tables/bank.csv";
    std::string visaMetaDataPath = rootPath + "/tests/tables/visa_meta.txt";
    std::string clientMetaDataPath = rootPath + "/tests/tables/bank_meta.txt";
    std::string joinVisaCols("0");
    std::string joinClientCols("3");
    std::string selectVisaCols("0,1");
    std::string selectClientCols("2");
    std::string joinCsvPath = rootPath + "/tests/tables/joindata.csv";
    std::string joinMetaPath = rootPath + "/tests/tables/joindata_meta.txt";
    bool isUnique = true;
    bool isAgg = false;
    bool verbose = cmd.isSet("v");
    bool mock = cmd.getOr("mock", 1);
    bool debug = cmd.isSet("debug");

    // Literals & opInfo are dynamically generated on java side
    std::vector<std::string> literals{"PAN", "Risk_Score", "Date", "PAN", "Balance",
        "Risk_Score", "150", "5000"};
    std::vector<oc::i64> opInfo{2, 0, 3, 4, 1, 0, 4, 5, 1, 0, 2, 1, 4, 4, 5,
         5, 1, 8, 7, 8, 6, 9, 6, 0, 7, 10, 3, 9, 10, 11, -1};

    std::unique_ptr<secJoin::WrapperState> visaState(secJoin::initState(visaCsvPath, visaMetaDataPath, clientMetaDataPath, 
        literals, opInfo, isUnique, verbose, mock, debug));

    std::unique_ptr<secJoin::WrapperState> bankState(secJoin::initState(bankCsvPath, visaMetaDataPath, clientMetaDataPath,
        literals, opInfo, !isUnique, verbose, mock, debug));

    auto lColCount = visaState->mLTable.cols();
    auto rColCount = visaState->mRTable.cols();

    auto select = genSelectCols(visaState.get(), bankState.get());

    oc::u64 rJoinColIndex = secJoin::getRColIndex(bankState->mJoinCols[1], lColCount, rColCount);
    auto exp = secJoin::join(
        visaState->mLTable[visaState->mJoinCols[0]],
        bankState->mRTable[rJoinColIndex],
        select);

    if (verbose)
    {
        std::cout << "visa L table:\n" << visaState->mLTable << std::endl;
        std::cout << "bank R table:\n" << bankState->mRTable << std::endl;
    }


    runProtocol(visaState.get(), bankState.get(), verbose);

    if(verbose)
        std::cout << "Join Protocol Completed" << std::endl;

    secJoin::getOtherShare(visaState.get(), isUnique, isAgg);
    secJoin::getOtherShare(bankState.get(), !isUnique, isAgg);

    runProtocol(visaState.get(), bankState.get(), verbose);

    if (visaState->mOutTable != exp)
    {

        std::cout << "L \n" << visaState->mLTable << std::endl;
        std::cout << "R \n" << bankState->mRTable << std::endl;
        std::cout << "exp \n" << exp << std::endl;
        std::cout << "act \n" << visaState->mOutTable << std::endl;
        secJoin::releaseState(visaState.release());
        secJoin::releaseState(bankState.release());
        throw RTE_LOC;
    }

    secJoin::releaseState(visaState.release());
    secJoin::releaseState(bankState.release());
}


void runProtocol(secJoin::WrapperState* visaState, secJoin::WrapperState* bankState,
    bool verbose)
{
    std::vector<oc::u8> buff;

    bool progress = true;
    bool first = true;
    while (
        !secJoin::isProtocolReady(visaState) ||
        !secJoin::isProtocolReady(bankState)
        )
    {
        if (first || !secJoin::isProtocolReady(visaState))
        {
            if (progress == false && buff.size() == 0)
            {
                std::cout << "visa not ready but has received 0 bytes" << std::endl;
                throw RTE_LOC;
            }

            buff = secJoin::runProtocol(visaState, buff);
            progress = buff.size() || secJoin::isProtocolReady(visaState);

            if (verbose)
            {
                std::cout << "Visa is sending " << buff.size() << " bytes" << std::endl;
            }
        }
        else
        {

            if (buff.size())
                std::cout << "warning, visa is done but bank sent a message" << std::endl;
            buff = {};
        }

        if (first || !secJoin::isProtocolReady(bankState))
        {
            if (first == false && progress == false && buff.size() == 0)
            {
                std::cout << "bank not ready but has received 0 bytes" << std::endl;
                throw RTE_LOC;
            }

            buff = secJoin::runProtocol(bankState, buff);

            progress = buff.size() || secJoin::isProtocolReady(bankState);
            if (verbose)
                std::cout << "Bank is sending " << buff.size() << " bytes" << std::endl;
        }
        else
        {
            if (buff.size())
                std::cout << "warning, bank is done but visa sent a message" << std::endl;
            buff = {};
        }

        first = false;
        //progress = false;
    }

    assert(secJoin::isProtocolReady(visaState));
    assert(secJoin::isProtocolReady(bankState));
}



void OmJoin_wrapper_avg_test(const oc::CLP& cmd)
{
    std::string rootPath(SEC_JOIN_ROOT_DIRECTORY);
    std::string visaCsvPath = rootPath + "/tests/tables/visa.csv";
    std::string bankCsvPath = rootPath + "/tests/tables/bank.csv";
    std::string visaMetaDataPath = rootPath + "/tests/tables/visa_meta.txt";
    std::string clientMetaDataPath = rootPath + "/tests/tables/bank_meta.txt";
    std::string joinVisaCols("0");
    std::string joinClientCols("3");
    std::string selectVisaCols("0,1");
    std::string selectClientCols("2");
    std::string joinCsvPath = rootPath + "/tests/tables/joindata.csv";
    std::string joinMetaPath = rootPath + "/tests/tables/joindata_meta.txt";
    std::string jsonString = "{ \"Average\": \"Risk_Score,Balance\", \"Group by\": \"PAN\" }";
    bool isUnique = true;
    bool isAgg = true;
    bool verbose = cmd.isSet("v");
    bool mock = cmd.isSet("mock");
    bool debug = cmd.isSet("debug");
    
    // Literals & opInfo are dynamically generated on java side
    std::vector<std::string> literals{"PAN", "Risk_Score", "Date", "PAN", "Balance",
        "Risk_Score", "150", "5000"};
    std::vector<oc::i64> opInfo{2, 0, 3, 4, 1, 0, 4, 5, 1, 0, 2, 1, 4, 4, 5,
         5, 1, 8, 7, 8, 6, 9, 6, 0, 7, 10, 3, 9, 10, 11, -1};

    std::unique_ptr<secJoin::WrapperState> visaState(secJoin::initState(visaCsvPath, visaMetaDataPath, clientMetaDataPath,
        literals, opInfo, isUnique, verbose, mock, debug));

    std::unique_ptr<secJoin::WrapperState> bankState(secJoin::initState(bankCsvPath, visaMetaDataPath, clientMetaDataPath,
        literals, opInfo, !isUnique, verbose, mock, debug));

    auto select = genSelectCols(visaState.get(), bankState.get());
    auto lColCount = visaState->mLTable.cols();
    auto rColCount = visaState->mRTable.cols();

    oc::u64 rJoinColIndex = secJoin::getRColIndex(bankState->mJoinCols[1], lColCount, rColCount);
    auto joinResult = secJoin::join(
        visaState->mLTable[visaState->mJoinCols[0]],
        bankState->mRTable[rJoinColIndex],
        select);
    

    if (verbose)
    {
        std::cout << "visa L table:\n" << visaState->mLTable << std::endl;
        std::cout << "bank R table:\n" << bankState->mRTable << std::endl;
    }

    runProtocol(visaState.get(), bankState.get(), verbose);

    if(verbose)
        std::cout << "Join Protocol Completed" << std::endl;
    
    secJoin::aggFunc(visaState.get());
    secJoin::aggFunc(bankState.get());

    runProtocol(visaState.get(), bankState.get(), verbose);

    if(verbose)
        std::cout << "Average Protocol Completed" << std::endl;

    secJoin::getOtherShare(visaState.get(), isUnique, isAgg);
    secJoin::getOtherShare(bankState.get(), !isUnique, isAgg);

    runProtocol(visaState.get(), bankState.get(), verbose);
    
    // Calculating average in plain text
    if(visaState->mAvgCols.size() == 0)
    {
        std::string temp("Average column not present\n");
        throw std::runtime_error(temp + LOCATION);
    }
    if(visaState->mGroupByCols.size() == 0)
    {
        std::string temp("Group By Table not present for Average operation\n");
        throw std::runtime_error(temp + LOCATION);
    }
    std::vector<oc::u64> colList = visaState->mAvgCols;
    std::vector<secJoin::ColRef> avgCols;
    for(oc::u64 i =0; i< colList.size(); i++)
    {
        oc::u64 avgColIndex = visaState->mMap[visaState->mAvgCols[i]];
        avgCols.emplace_back(joinResult[avgColIndex]);
    }

    // Current Assumption we only have 
    oc::u64 groupByColIndex = visaState->mMap[visaState->mGroupByCols[0]];
    secJoin::ColRef grpByCol = joinResult[groupByColIndex];

    auto exp = secJoin::average(grpByCol, avgCols);

    if (visaState->mOutTable != exp)
    {
        // std::cout << "L \n" << visaState->mLTable << std::endl;
        // std::cout << "R \n" << bankState->mRTable << std::endl;
        std::cout << "exp \n" << exp << std::endl;
        std::cout << "act \n" << visaState->mOutTable << std::endl;
        secJoin::releaseState(visaState.release());
        secJoin::releaseState(bankState.release());
        throw RTE_LOC;
    }

    secJoin::releaseState(visaState.release());
    secJoin::releaseState(bankState.release());
}