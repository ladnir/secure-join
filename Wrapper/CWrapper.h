#include "com_visa_secureml_wrapper_SecJoinWrapper.h"
#include "secure-join/Util/CSVParser.h"
#include "coproto/Socket/BufferingSocket.h"
#include "secure-join/Join/Table.h"
#include "secure-join/Join/OmJoin.h"

extern "C"
{
    void testApi(std::string& str);
    long initState(std::string& csvPath, std::string& visaMetaDataPath, std::string& clientMetaDataPath, 
            std::string& visaJoinCols, std::string& clientJoinCols, std::string& selectVisaCols,
            std::string& selectClientCols, bool isUnique);
    std::vector<oc::u8> runJoin(long stateAddress, std::vector<oc::u8>& buff);
    void releaseState(long memoryAddress);
    bool isProtocolReady(long stateAddress);
    void getOtherShare(long stateAddress, bool isUnique);
    void getJoinTable(long stateAddress, std::string csvPath, std::string metaDataPath, bool isUnique);
}

