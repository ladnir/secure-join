#include "CSVParser.h"

namespace secJoin
{
    // Reading files starts here
    u64 getCount(std::string& line, const std::string& type)
    {
        std::stringstream str(line);
        std::string word;

        u64 count = 0;
        u8 colInfoCount = 0;
        while (getline(str, word, CSV_COL_DELIM))
        {
            if (colInfoCount == 0)
            {
                if (word.compare(type) != 0)
                    throw RTE_LOC;
            }
            else if (colInfoCount == 1)
            {
                count = std::stol(word);
            }

            colInfoCount++;
        }

        if (colInfoCount != 2)
        {
            std::string temp = line + " -> Not enough Information in the Meta File\n" + LOCATION;
            throw std::runtime_error(temp);
        }
        return count;
    }

    ColumnInfo getColumnInfo(std::string& line)
    {
        std::stringstream str(line);
        std::string word;

        u64 colInfoCount = 0;
        std::string name;
        TypeID type;
        u64 size;
        while (getline(str, word, CSV_COL_DELIM))
        {
            if (colInfoCount == 0)
                name = word;
            else if (colInfoCount == 1)
            {
                if (word.compare(STRING_META_TYPE) == 0)
                    type = TypeID::StringID;
                else
                    type = TypeID::IntID;
            }
            else if (colInfoCount == 2)
            {
                size = std::stol(word) * 8;
            }

            colInfoCount++;
        }

        if (colInfoCount != 3)
        {
            std::cout << line
                      << " -> Not enough Information in the Meta File"
                      << std::endl;
            throw RTE_LOC;
        }
        return {name, type, size};
    }

    void getFileInfo(std::string &fileName,
                     std::istream &in,
                     std::vector<ColumnInfo> &columnInfo,
                     u64 &rowCount,
                     u64 &colCount)
    {
        std::string line, word;
        bool readRowCount = false;
        bool readColCount = false;

        while (getline(in, line))
        {
            if (!readRowCount)
            {
                rowCount = getCount(line, ROWS_META_TYPE);
                readRowCount = true;
            }
            else if (!readColCount)
            {
                colCount = getCount(line, COLS_META_TYPE);
                readColCount = true;
            }
            else
                columnInfo.push_back(getColumnInfo(line));

        }

        if(columnInfo.size() != colCount)
        {
            throw std::runtime_error("Col Count doesn't match the total \
                number of column info " + fileName + "\n" LOCATION);
        }

        if (SECJOIN_ENABLE_LOGGING)
        {
            std::cout << "Printing " << fileName << " Meta Data" << std::endl;
            for (u64 i = 0; i < columnInfo.size(); i++)
            {
                if (std::get<1>(columnInfo[i]) == TypeID::IntID)
                {
                    std::cout
                        << std::get<0>(columnInfo[i]) << " "
                        << "Integer "
                        << std::get<2>(columnInfo[i]) << " "
                        << std::endl;
                }
                else
                {
                    std::cout
                        << std::get<0>(columnInfo[i]) << " "
                        << "String "
                        << std::get<2>(columnInfo[i]) << " "
                        << std::endl;
                }
            }
        }
    }

    void getFileInfo(std::string &fileName, std::vector<ColumnInfo> &columnInfo, 
        u64 &rowCount, u64& colCount)
    {

        std::fstream file(fileName, std::ios::in);
        std::istream in(file.rdbuf());

        if (!file.is_open())
            throw std::runtime_error("Could not open the file " + fileName + "\n" LOCATION);

        getFileInfo(fileName, in, columnInfo, rowCount, colCount);
        file.close();
    }


    // Writing to files starts here
    void writeFileInfo(std::string &filePath, Table& tb)
    {

        std::ofstream file;
        file.open(filePath);

        if (!file.is_open())
            throw std::runtime_error("Could not open the file " + filePath + "\n" LOCATION);
        

        // Adding the Row Count to the file
        file << ROWS_META_TYPE << CSV_COL_DELIM << tb.rows() << "\n";

        // Adding the Column info to the file
        writeColumnInfo(file, tb);
        
        file.close();
    }

    void writeColumnInfo(std::ofstream &file, Table &tb)
    {
        for(u64 i=0; i<tb.mColumns.size(); i++)
        {
            if( tb.mColumns[i].getTypeID() == TypeID::IntID)
            {
                file << tb.mColumns[i].mName << CSV_COL_DELIM
                     << "INT" << CSV_COL_DELIM
                     << tb.mColumns[i].getByteCount()
                     << "\n";
            }
            else
            {
                file << tb.mColumns[i].mName << CSV_COL_DELIM
                << "STRING" << CSV_COL_DELIM
                << tb.mColumns[i].getByteCount()
                << "\n";
            }
        }
    }

    void writeFileHeader(std::ofstream &file, Table &tb)
    {
        for(u64 i=0; i < tb.cols(); i++)
        {
            file << tb.mColumns[i].mName;

            if( i == tb.cols() - 1 )
                file << "\n";
            else
                file << CSV_COL_DELIM;
        }
    }

    void writeFileData(std::string &filePath, Table& tb)
    {
        std::ofstream file;
        file.open(filePath);

        if (!file.is_open())
            throw std::runtime_error("Could not open the file " + filePath + "\n" LOCATION);
    
        // Adding the Columns names to the file
        writeFileHeader(file, tb);
        
        for(u64 rowNum=0; rowNum<tb.rows(); rowNum++)
        {
            for(u64 colNum=0; colNum<tb.cols(); colNum++)
            {
                if( tb.mColumns[colNum].getTypeID() == TypeID::IntID)
                {
                    if(tb.mColumns[colNum].getByteCount() <= 4)
                    {
                        u8* ptr = tb.mColumns[colNum].mData[rowNum].data();
                        i32 number = *(i32*)ptr;

                        file << number;
                    }
                    else if(tb.mColumns[colNum].getByteCount() <= 8)
                    {
                        u8* ptr = tb.mColumns[colNum].mData[rowNum].data();
                        i64 number = *(i64*)ptr;
                        
                        file << number;
                    }
                    else
                    {
                        std::string temp = tb.mColumns[colNum].mName  
                            + " can't be stored as int type\n"
                            + LOCATION;
                        throw std::runtime_error(temp);
                    }
                }
                else
                {
                    std::string temp(tb.mColumns[colNum].getByteCount(), '\0');

                    // Is this safe?
                    memcpy(temp.data(), tb.mColumns[colNum].mData[rowNum].begin(),  
                                tb.mColumns[colNum].getByteCount() );

                    temp.erase(temp.find('\0'));
                    file << temp;
                }

                if( colNum == tb.cols() - 1 )
                    file << "\n";
                else
                    file << CSV_COL_DELIM;

            }
        }

        file.close();
    }

}