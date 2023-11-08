#pragma once
#include <cmath>
#include "cryptoTools/Crypto/PRNG.h"
#include "secure-join/Join/Table.h"
#include <fstream>
#include "secure-join/Defines.h"

namespace secJoin
{

    u64 getCount(std::string& line, const std::string& type);
    ColumnInfo getColumnInfo(std::string& line);
    void getFileInfo(std::string& fileName, std::vector<ColumnInfo>& columnInfo, 
                    u64& rowCount, u64 &colCount);
    void getFileInfo(std::string& fileName, std::istream& in, std::vector<ColumnInfo>& columnInfo, 
                    u64& rowCount, u64& colCount);
    void writeFileInfo(std::string &filePath, Table& tb);
    void writeColumnInfo(std::ofstream &file, Table &tb);
    void writeFileData(std::string &filePath, Table& tb);
    void writeFileHeader(std::ofstream &file, Table &tb);

}
