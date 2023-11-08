#pragma once

namespace secJoin{

    // checking end of the array
    inline oc::u64 checkEOA(oc::i64 val)
    {
        if(val == -1)
            throw RTE_LOC;
        
        return val;
    }

    inline void printCols(std::vector<oc::u64>& vec, std::string colType)
    {
        std::cout << "Printing " << colType << " Cols" << std::endl;
        for(oc::u64 i = 0; i < vec.size(); i++)
            std::cout << vec[i] << " ";
        
        std::cout << std::endl;
    }

    inline void printGates(std::vector<ArrGate>& gates)
    {
        std::cout << "Printing Gates" << std::endl;

        for(oc::u64 i = 0; i < gates.size(); i++)
            std::cout << gates[i];
        
        std::cout << std::endl;
    }

    inline void initCols(std::vector<oc::u64>& vec, oc::u64& startIndex, std::vector<oc::i64>& opInfo)
    {
        oc::u64 size = checkEOA(opInfo[startIndex++]);
        
        vec.reserve(size);
        
        for(u64 i=0; i<size; i++)
            vec.emplace_back(checkEOA(opInfo[startIndex++]));
    }

    inline void initGates(std::vector<ArrGate>& gates, oc::u64& startIndex, std::vector<oc::i64>& opInfo)
    {
        oc::u64 size = checkEOA(opInfo[startIndex++]);
        gates.reserve(size);

        for(u64 i=0; i<size; i++)
        {
            gates.emplace_back(
                checkEOA(opInfo[startIndex++]),
                checkEOA(opInfo[startIndex++]),
                checkEOA(opInfo[startIndex++]),
                checkEOA(opInfo[startIndex++])
            );
        }
    }


    /*
    opInfo array contains object in the following way
    1) Total number of the Join Columns
    2) Series of the Join Columns
    3) Total number of the Select Columns
    4) Series of the Select Columns
    5) Total number of the Groupby Columns
    6) Series of the Groupby Columns
    7) Total number of the Average Columns
    8) Series of the Average Columns
    9) Total number of Gates
    10) Four Entries for Gate 1
    11) Four Entries for Gate 2
    12) .
    13) .
    14) .
    15) -1
    */
    inline void parseColsArray(WrapperState* cState, std::vector<oc::i64>& opInfo, bool print)
    {
        oc::u64 startIndex = 0;
        
        // Getting Join Cols
        initCols(cState->mJoinCols, startIndex, opInfo);

        // Getting Select Cols
        initCols(cState->mSelectCols, startIndex, opInfo); 
        
        // Getting Groupby Cols
        initCols(cState->mGroupByCols, startIndex, opInfo);

        // Getting Average Cols
        initCols(cState->mAvgCols, startIndex, opInfo);

        // Getting the where Gates
        initGates(cState->mGates, startIndex, opInfo);

        if(print)
        {
            printCols(cState->mJoinCols, "Join");
            printCols(cState->mSelectCols, "Select");
            printCols(cState->mGroupByCols, "GroupBy");
            printCols(cState->mAvgCols, "Average");
            printGates(cState->mGates);
        }
    }

    inline void appendVecIfNec(std::vector<oc::u64>& select, oc::u64 element)
    {
        std::vector<oc::u64>::iterator it = std::find(select.begin(), select.end(), element) ;
        if( it == select.end())
            select.emplace_back(element);

    }

    inline void updateSelectCols(std::vector<oc::u64>& select, std::vector<ArrGate>& gates, 
        oc::u64 totalColCount)
    {
        for(u64 i=0; i < gates.size(); i++)
        {

            for( u64 j=0; j< gates[i].mInput.size(); j++)
            {
                u64 element = gates[i].mInput[j];
                // Check whether input to the gate is a table column or not
                if(element < totalColCount)
                    appendVecIfNec(select, element);
            }
        }        
    }


    inline void updateSelectCols(std::vector<oc::u64>& select, std::vector<oc::u64>& colList)
    {
        for(u64 i=0; i < colList.size(); i++)
        {
            u64 element = colList[i];
            // If the col is not present then add it to the select
            appendVecIfNec(select, element);

        }        
    }

    /*
    This method method updates the Select Cols if any of the where, 
    groupby & Average Columns don't exists in the Select Cols.
    */
    inline void updateSelectCols(WrapperState* cState, bool print)
    {  

        // Checking GroupBy Cols
        updateSelectCols(cState->mSelectCols, cState->mGroupByCols);

        // Checking Average Cols
        updateSelectCols(cState->mSelectCols, cState->mAvgCols);

        // Checking where Cols
        updateSelectCols(cState->mSelectCols, cState->mGates, 
            cState->mLTable.cols() + cState->mRTable.cols());

        if(print)
            printCols(cState->mSelectCols, "Select");
    }

    inline void createNewMapping(std::unordered_map<oc::u64, oc::u64> &mMap,
        std::vector<oc::u64>& mSelectCols)
    {
        for(oc::u64 i = 0; i < mSelectCols.size(); i++)
        {
            oc::u64 colIndex = mSelectCols[i];
            mMap[colIndex] = i;
        }
    }



}