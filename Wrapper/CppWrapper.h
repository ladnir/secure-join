#pragma once
#include <string>
#include <vector>
#include "secure-join/Defines.h"

namespace secJoin
{
    struct GenState;

    void testApi(std::string& str);
    GenState* initState(std::string& csvPath, std::string& visaMetaDataPath, std::string& clientMetaDataPath,
        std::string& visaJoinCols, std::string& clientJoinCols, std::string& selectVisaCols,
        std::string& selectClientCols, bool isUnique,
        bool verbose, bool mock);

    std::vector<u8> runJoin(GenState* stateAddress, std::vector<u8>& buff);
    void releaseState(GenState* memoryAddress);
    bool isProtocolReady(GenState* stateAddress);
    void getOtherShare(GenState* stateAddress, bool isUnique);
    void getJoinTable(GenState* stateAddress, std::string csvPath, std::string metaDataPath, bool isUnique);

}


