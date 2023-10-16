#pragma once
#include <string>
#include <vector>
#include "secure-join/Defines.h"

namespace secJoin
{
    struct WrapperState;

    void testApi(std::string& str);
    WrapperState* initState(std::string& csvPath, std::string& visaMetaDataPath, std::string& clientMetaDataPath,
        std::string& visaJoinCols, std::string& clientJoinCols, std::string& selectVisaCols,
        std::string& selectClientCols, bool isUnique,
        bool verbose, bool mock);

    std::vector<u8> runJoin(WrapperState* stateAddress, std::vector<u8>& buff);
    void releaseState(WrapperState* memoryAddress);
    bool isProtocolReady(WrapperState* stateAddress);
    void getOtherShare(WrapperState* stateAddress, bool isUnique);
    void getJoinTable(WrapperState* stateAddress, std::string csvPath, std::string metaDataPath, bool isUnique);

}


