#include "Table.h"
#include "secure-join/Util/Util.h"
#include "secure-join/Sort/RadixSort.h"



namespace secJoin
{
    // SelectBundle SelectQuery::addInput(SharedTable::ColRef column)
    // {
    //     if (mLeftTable == nullptr)
    //         throw std::runtime_error("call joinOn(...) first. " LOCATION);

    //     mMem.emplace_back();
    //     auto& mem = mMem.back();
    //     mInputs.emplace_back(
    // 		(int)(mMem.size()) - 1,
    // 		column, 
    // 		(int)(mInputs.size()));

    //     mem.mType = column.mCol.mType;
    //     mem.mInputIdx = (mInputs.size()) - 1;
    //     mem.mNextBatchIdx = (mMem.size()) - 1;

    //     if (&column.mTable == mLeftTable)
    //         mLeftInputs.push_back(&mInputs.back());
    //     else if (&column.mTable == mRightTable)
    //         mRightInputs.push_back(&mInputs.back());
    //     else
    //         throw RTE_LOC;


    //     return { *this, mem.mNextBatchIdx };
    // }

    // SelectBundle SelectQuery::joinOn(SharedTable::ColRef left, SharedTable::ColRef right)
    // {
    //     if (mLeftTable)
    //         throw RTE_LOC;

    //     mLeftTable = &left.mTable;
    //     mRightTable = &right.mTable;
    //     mLeftCol = &left.mCol;
    //     mRightCol = &right.mCol;
    //     auto r = addInput(left);
    //     mMem.back().mUsed = true;

    //     addInput(right);

    //     return r;
    // }

    // int SelectQuery::addOp(selectDetails::SelectOp op, int wire1, int wire2)
    // {
    //     if (mLeftTable == nullptr)
    //         throw std::runtime_error("call joinOn(...) first. " LOCATION);

    //     if (wire1 >= (int)mMem.size() ||
    //         wire2 >= (int)mMem.size())
    //         throw RTE_LOC;

    //     selectDetails::Mem mem;

    //     switch (op) {
    //     case selectDetails::BitwiseOr:
    //     case selectDetails::BitwiseAnd:
    //         if (mMem[wire1].mType->getBitCount() != mMem[wire2].mType->getBitCount())
    //             throw RTE_LOC;
    //         mem.mType = mMem[wire1].mType;
    //         break;
    //     case selectDetails::LessThan:
    //         mem.mType = std::make_shared<IntType>(1);
    //         break;
    //     case selectDetails::Multiply:
    //     case selectDetails::Add:
    //         mem.mType =
    //             mMem[wire1].mType->getBitCount() > mMem[wire2].mType->getBitCount() ?
    //             mMem[wire1].mType :
    //             mMem[wire1].mType;
    //         break;
    //     default:
    //         throw RTE_LOC;
    //     }

    //     mMem[wire1].mUsed = true;
    //     mMem[wire2].mUsed = true;

    //     selectDetails::Gate gate;
    //     gate.op = op;
    //     gate.mIn1 = wire1;
    //     gate.mIn2 = wire2;
    //     gate.mOut = (mMem.size());

    //     mGates.push_back(gate);

    //     mem.mGate = &mGates.back();
    //     mem.mNextBatchIdx = (mMem.size());

    //     mMem.push_back(mem);

    //     return mem.mNextBatchIdx;
    // }

    // int SelectQuery::addOp(selectDetails::SelectOp op, int wire1)
    // {
    //     if (mLeftTable == nullptr)
    //         throw std::runtime_error("call joinOn(...) first. " LOCATION);

    //     if (op != selectDetails::Inverse ||
    //         (int)mMem.size() <= wire1)
    //         throw RTE_LOC;

    //     mMem[wire1].mUsed = true;

    //     selectDetails::Gate gate;
    //     gate.op = op;
    //     gate.mIn1 = wire1;
    //     gate.mOut = (mMem.size());
    //     mGates.push_back(gate);


    //     selectDetails::Mem mem;
    //     mem.mType = mMem[wire1].mType;
    //     mem.mGate = &mGates.back();
    //     mem.mNextBatchIdx = (mMem.size());
    //     mMem.push_back(mem);

    //     return mem.mNextBatchIdx;
    // }

    // void SelectQuery::addOutput(std::string name, const SelectBundle & column)
    // {
    //     if (mLeftTable == nullptr)
    //         throw std::runtime_error("call joinOn(...) first. " LOCATION);

    //     mOutputs.emplace_back(mMem[column.mMemIdx].mNextBatchIdx, name, -1);
    //     mMem[column.mMemIdx].mOutputIdx = (mOutputs.size()) - 1;


    //     auto maxPos = -1;
    //     if (isLeftPassthrough(mOutputs.back()) == false)
    //     {
    //         for (auto& o : mOutputs)
    //         {
    //             if (isLeftPassthrough(o) == false)
    //                 maxPos = std::max(maxPos, o.mPosition);
    //         }
    //         maxPos = maxPos + 1;
    //     }
    //     mOutputs.back().mPosition = maxPos;
    // }

    // void SelectQuery::apply(BetaCircuit & cir, span<BetaBundle> inputs, span<BetaBundle> outputs) const
    // {
    //     BetaLibrary lib;

    //     std::vector<BetaBundle> mem(mMem.size());
    //     for (u64 i = 0; i < inputs.size(); ++i)
    //         mem[mInputs[i].mMemIdx] = inputs[i];


    //     //int outIdx = 0;
    //     for (auto& gate : mGates)
    //     {
    //         //BetaBundle wires(mMem[gate.mOut].mType->getBitCount());

    //         if (mMem[gate.mOut].isOutput() == false)
    //         {
    //             mem[gate.mOut].mWires.resize(mMem[gate.mOut].mType->getBitCount());
    //             cir.addTempWireBundle(mem[gate.mOut]);
    //         }
    //         else
    //         {
    //             auto out = mOutputs[mMem[gate.mOut].mOutputIdx].mPosition;
    //             mem[gate.mOut] = outputs[out];
    //         }


    //         switch (gate.op)
    //         {
    //         case selectDetails::BitwiseOr:
    //             lib.bitwiseOr_build(cir, mem[gate.mIn1], mem[gate.mIn2], mem[gate.mOut]);
    //             break;
    //         case selectDetails::BitwiseAnd:
    //             lib.bitwiseAnd_build(cir, mem[gate.mIn1], mem[gate.mIn2], mem[gate.mOut]);
    //             break;
    //         case selectDetails::LessThan:
    //             lib.lessThan_build(cir, mem[gate.mIn1], mem[gate.mIn2], mem[gate.mOut],
    //                 oc::BetaLibrary::IntType::TwosComplement, 
    //                 oc::BetaLibrary::Optimized::Size);
    //             break;
    //         case selectDetails::Inverse:
    //             lib.bitwiseInvert_build(cir, mem[gate.mIn1], mem[gate.mOut]);
    //             break;
    //         case selectDetails::Multiply:

    //             lib.mult_build(cir, mem[gate.mIn1], mem[gate.mIn2], mem[gate.mOut], 
    //                 oc::BetaLibrary::Optimized::Depth, 
    //                 oc::BetaLibrary::IntType::TwosComplement);
    //             break;
    //         case selectDetails::Add:
    //         {
    //             oc::BetaBundle temp(mem[gate.mIn1].size());
    //             cir.addTempWireBundle(temp);
    //             lib.add_build(cir, mem[gate.mIn1], mem[gate.mIn2], mem[gate.mOut], temp,
    //                 oc::BetaLibrary::IntType::TwosComplement,
    //                 oc::BetaLibrary::Optimized::Size
    //                 );
    //         }
    //             break;
    //         default:
    //             throw RTE_LOC;
    //         }
    //     }
    // }
    // bool SelectQuery::isLeftPassthrough(selectDetails::Output output) const
    // {
    //     return
    //         mMem[output.mMemIdx].isInput() &&
    //         &mInputs[mMem[output.mMemIdx].mInputIdx].mCol.mTable == mLeftTable;
    // }

    // bool SelectQuery::isRightPassthrough(selectDetails::Output output) const
    // {
    //     return
    //         mMem[output.mMemIdx].isInput() &&
    //         &mInputs[mMem[output.mMemIdx].mInputIdx].mCol.mTable == mRightTable;
    // }

    // bool SelectQuery::isCircuitInput(selectDetails::Input input) const
    // {
    //     return
    //         &input.mCol.mTable == mRightTable ||
    //         mMem[input.mMemIdx].mUsed;
    // }

    // SelectBundle SelectBundle::operator|(const SelectBundle& r) const
    // {
    //     return SelectBundle{
    //         mSelect,
    //         mSelect.addOp(selectDetails::BitwiseOr, mMemIdx, r.mMemIdx) };
    // }
    // SelectBundle SelectBundle::operator&(const SelectBundle& r) const
    // {
    //     return SelectBundle{
    //         mSelect,
    //         mSelect.addOp(selectDetails::BitwiseAnd, mMemIdx, r.mMemIdx) };
    // }
    // SelectBundle SelectBundle::operator<(const SelectBundle&r) const

    // {
    //     return SelectBundle{
    //         mSelect,
    //         mSelect.addOp(selectDetails::LessThan, mMemIdx, r.mMemIdx) };
    // }
    // SelectBundle SelectBundle::operator!() const
    // {
    //     return SelectBundle{
    //         mSelect,
    //         mSelect.addOp(selectDetails::Inverse, mMemIdx) };
    // }
    // SelectBundle SelectBundle::operator*(const SelectBundle& r) const
    // {
    //     return SelectBundle{
    //         mSelect,
    //         mSelect.addOp(selectDetails::Multiply, mMemIdx, r.mMemIdx) };
    // }
    // SelectBundle SelectBundle::operator+(const SelectBundle& r) const
    // {
    //     return SelectBundle{
    //         mSelect,
    //         mSelect.addOp(selectDetails::Add, mMemIdx, r.mMemIdx) };
    // }


    macoro::task<> revealLocal(const Table& share, coproto::Socket& sock, Table& out)
    {
        MC_BEGIN(macoro::task<>, &share, &sock, &out,
            remoteShare = Table(),
            i = u64()
        );

        remoteShare.init(share.rows(), share.getColumnInfo());
        for (i = 0; i < remoteShare.mColumns.size(); i++)
        {
            MC_AWAIT(sock.recv(remoteShare.mColumns[i].mData.mData));
        }
        if (share.mIsActive.size() > 0)
        {
            remoteShare.mIsActive.resize(share.mIsActive.size());
            MC_AWAIT(sock.recv(remoteShare.mIsActive));
        }
        out = reveal(share, remoteShare);

        MC_END();
    }


    macoro::task<> revealRemote(const Table& share, coproto::Socket& sock)
    {
        MC_BEGIN(macoro::task<>, &share, &sock, i = u64());

        for (i = 0; i < share.mColumns.size(); i++)
        {
            MC_AWAIT(sock.send(coproto::copy(share.mColumns[i].mData.mData)));
        }

        // std::move() will the delete the local share
        if (share.mIsActive.size() > 0)
        {
            MC_AWAIT(sock.send(coproto::copy(share.mIsActive)));
        }
        MC_END();
    }


    void populateTable(Table& tb, std::istream& in, oc::u64 rowCount)
    {
        bool isheader = true;
        std::string line, word;

        for (oc::u64 rowNum = 0; rowNum < rowCount; rowNum++)
        {
            getline(in, line);

            // Skipping the header
            if (isheader)
            {
                isheader = false;
                rowNum--;
                continue;
            }
            oc::u64 colNum = 0;
            std::stringstream str(line);
            while (getline(str, word, CSV_COL_DELIM))
            {
                if (tb.mColumns[colNum].getTypeID() == TypeID::IntID)
                {
                    if (tb.mColumns[colNum].getByteCount() <= 4)
                    {
                        oc::i32 number = stoi(word);
                        memcpy(tb.mColumns[colNum].mData.data(rowNum), &number, sizeof(i32));
                    }
                    else if (tb.mColumns[colNum].getByteCount() <= 8)
                    {
                        oc::i64 number = stoll(word);
                        memcpy(tb.mColumns[colNum].mData.data(rowNum), &number, sizeof(i64));
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
                    oc::u64 minSize = tb.mColumns[colNum].getByteCount() > word.size() ?
                        word.size() : tb.mColumns[colNum].getByteCount();

                    memcpy(tb.mColumns[colNum].mData.data(rowNum), word.data(), minSize);
                }
                colNum++;
            }
            isheader = false;
        }

    }



    void populateTable(Table& tb, std::string& fileName, oc::u64 rowCount)
    {
        std::fstream file(fileName, std::ios::in);
        std::istream in(file.rdbuf());
        if (!file.is_open())
        {
            std::cout << "Could not open the file" << std::endl;
            throw RTE_LOC;
        }
        populateTable(tb, in, rowCount);
        file.close();
    }

    void share(Table& table,
        std::array<Table, 2>& shares,
        PRNG& prng)
    {
        shares[0].mColumns.resize(table.mColumns.size());
        shares[1].mColumns.resize(table.mColumns.size());
        for (oc::u64 i = 0; i < table.mColumns.size(); i++)
        {
            std::array<BinMatrix, 2> temp;
            share(table.mColumns[i].mData, temp[0], temp[1], prng);

            for (u64 k = 0;k < 2; ++k)
            {
                shares[k].mColumns[i].mBitCount = table.mColumns[i].mBitCount;
                shares[k].mColumns[i].mName = table.mColumns[i].mName;
                shares[k].mColumns[i].mType = table.mColumns[i].mType;
                shares[k].mColumns[i].mData = temp[k];
            }
        }

    }

    bool eq(span<const u8> l, span<const u8> r)
    {
        assert(l.size() == r.size());
        for (u64 i = 0; i < l.size(); ++i)
            if (l[i] != r[i])
                return false;
        return true;
    }

    // bool lessThan(span<const u8> l, span<const u8> r)
    // {
    //     assert(l.size() == r.size());
    //     for (u64 i = l.size() - 1; i < l.size(); --i)
    //         if (l[i] < r[i])
    //             return true;
    //     return false;
    // }
    Perm sort(const ColRef& x)
    {
        return sort(x.mCol.mData);
        // Perm res(x.mCol.rows());

        // std::stable_sort(res.begin(), res.end(),
        //     [&](const auto& a, const auto& b) {
        //         return lessThan(x.mCol.mData[a], x.mCol.mData[b]);
        //         // return (k64[a] < k64[b]);
        //         // for (u64 i = x.mCol.cols() - 1; i < x.mCol.cols(); --i)
        //         //     if (x.mCol.mData(a, i) < x.mCol.mData(b, i))
        //         //         return true;
        //         // return false;
        //     });
        // return res;
    }

    Table average(ColRef groupByCol,
                std::vector<ColRef> avgCol)
    {
        u64 m = avgCol.size();
        u64 n0 = groupByCol.mCol.rows();

        // Generating the permutation
        auto groupByPerm = sort(groupByCol);


        auto invPerm = PermOp::Regular;
        BinMatrix temp; 
        temp.resize(groupByCol.mCol.mData.numEntries(), groupByCol.mCol.mData.bytesPerEntry() * 8);
        groupByPerm.apply<u8>(groupByCol.mCol.mData, temp, invPerm);
        std::swap(groupByCol.mCol.mData, temp);


        // Applying permutation to all the average cols
        for(u64 i=0; i<m; i++)
        {
            temp.resize(avgCol[i].mCol.mData.numEntries(), 
                avgCol[i].mCol.mData.bytesPerEntry() * 8);
            groupByPerm.apply<u8>(avgCol[i].mCol.mData, temp, invPerm);
            std::swap(avgCol[i].mCol.mData, temp);
        }


        // Adding a Columns of 1's for calculating average
        BinMatrix ones(n0, sizeof(oc::u64) * 8);
        for(oc::u64 i = 0; i < n0; i++)
            ones(i,0) = 1;

        Table out;

        u64 nOutRows=0;

        // Maybe implement a ControlBits logic
        if(n0 > 0)
        {
            nOutRows=1; // First region 
            for(u64 row=1; row<n0; row++)
            {
                if( !eq(groupByCol.mCol.mData[row], groupByCol.mCol.mData[row-1]))
                    nOutRows++;
            }
        }

        populateOutTable(out, avgCol, groupByCol, nOutRows);

        // Base case where there are no rows
        if(n0 == 0)
            return out;
        

    
        // Creating a vector of inputs for Beta Curcuit evaluation
        std::vector<oc::BetaCircuit*> cir;
        cir.resize(m + 1);
        std::vector<oc::BitVector> inputs(2 * cir.size()), outputs(cir.size());

        oc::BetaLibrary lib;
        for(u64 i=0; i<m; i++)
        {
            u64 size = avgCol[i].mCol.getByteCount() * 8;
            cir[i] = lib.int_int_add(size, size, size, oc::BetaLibrary::Optimized::Depth);
            
            // Placing the first entry of the table for each column
            u64 rem = size - avgCol[i].mCol.getBitCount();
            inputs[2*i].reset(rem);
            inputs[2*i].append(avgCol[i].mCol.mData.data(0) , avgCol[i].mCol.getBitCount());

            // Placing 0s as the first entry
            inputs[2*i+1].reset(size);
            outputs[i].reset(size);
        }

        // Adding the ciruit for the BinMatrix of ones
        u64 size = ones.bytesPerEntry() * 8;
        cir[m] = lib.int_int_add(size, size, size, oc::BetaLibrary::Optimized::Depth);
        inputs[2*m].append(ones.mData.data(0) , size);
        inputs[2*m+1].reset(size);
        outputs[m].reset(size);


        u64 curOutRow = 0;
        // We don't have to check the first entry
        for(u64 row=1; row<n0; row++)
        {
            // Checking groupby row with the previous entry
            if( eq(groupByCol.mCol.mData[row], groupByCol.mCol.mData[row-1]))
            {

                for(u64 col=0; col<m; col++)
                {
                    std::vector<oc::BitVector> tempInputs = 
                                    {inputs[2*col], inputs[2*col+1]};

                    inputs[2*col] = cirEval(cir[col], tempInputs,
                        outputs[col], avgCol[col].mCol.mData.data(row), 
                        avgCol[col].mCol.getBitCount(),
                        avgCol[col].mCol.getByteCount()
                    );
                }

                // Run the circuit of ones:
                std::vector<oc::BitVector> tempInputs = {inputs[2*m], inputs[2*m+1]};
                inputs[2*m] = cirEval(cir[m], tempInputs,
                        outputs[m], ones.mData.data(row), 
                        ones.bitsPerEntry(),
                        ones.bytesPerEntry()
                    );
            }
            else
            {
                copyTableEntry(out, groupByCol, avgCol, inputs, ones,
                    curOutRow, nOutRows, row);

                // Putting the current row value in the first input
                for(u64 i=0; i<m; i++)
                {
                    auto size = avgCol[i].mCol.getByteCount() * 8;
                    auto bits = avgCol[i].mCol.getBitCount();
                    // Filling extra bits with zero
                    u64 rem = size - bits;
                    inputs[2*i].reset(rem);
                    inputs[2*i].append(avgCol[i].mCol.mData.data(row) , bits);
                }
                inputs[2*m].reset(0);
                inputs[2*m].append(ones.mData.data(row) , ones.bitsPerEntry());

                curOutRow++;
            }

            if( row == n0 - 1)
            {
                copyTableEntry(out, groupByCol, avgCol, inputs, ones,
                    curOutRow, nOutRows, row);
            }

        }


        return out;
    }

    void copyTableEntry(Table& out, ColRef groupByCol, std::vector<ColRef> avgCol,
                std::vector<oc::BitVector>& inputs, BinMatrix& ones,
                u64 curOutRow, u64 nOutRows, u64 row)
    {
        u64 m = avgCol.size();
        assert(curOutRow <= nOutRows);
        // Copying the groupby column
        assert(out.mColumns[0].mData.cols() == groupByCol.mCol.mData.cols());
        memcpy(out.mColumns[0].mData.data(curOutRow),  
                groupByCol.mCol.mData.data(row-1), 
                groupByCol.mCol.mData.cols());

        // Copying the average column
        for(u64 col=0; col<m; col++)
        {
            assert(out.mColumns[col+1].mData.bytesPerEntry() == inputs[2*col].sizeBytes());
            memcpy(out.mColumns[col+1].mData.data(curOutRow),
                    inputs[2*col].data(), inputs[2*col].sizeBytes());
        }

        // Copying the ones column 
        memcpy(out.mColumns[m+1].mData.data(curOutRow),
                    inputs[2*m].data(), inputs[2*m].sizeBytes());

        // Making the first input zero
        for(u64 i=0; i<m; i++)
        {
            u64 size = avgCol[i].mCol.getByteCount() * 8;
            inputs[2*i+1].reset(size);
        }
        u64 size = ones.bytesPerEntry() * 8;
        inputs[2*m].reset(size);
    }

    void populateOutTable(
        Table& out,
        std::vector<ColRef> avgCol,
        ColRef groupByCol,
        u64 nOutRows)
    {
        u64 m = avgCol.size();
        // u64 n0 = groupByCol.mCol.rows();

        out.mColumns.resize(m + 2); // Average Cols + Group By Cols + Count Col

        // Adding the group by column info
        out.mColumns[0].mName = groupByCol.mCol.mName;
        auto bits = groupByCol.mCol.getByteCount() * 8;
        out.mColumns[0].mBitCount = bits;
        out.mColumns[0].mType = groupByCol.mCol.mType;
        out.mColumns[0].mData.resize(nOutRows, bits);

        // Adding the average cols
        for(u64 i=0; i < m; i++)
        {
            out.mColumns[i+1].mName = avgCol[i].mCol.mName;
            auto bits = avgCol[i].mCol.getByteCount() * 8;
            out.mColumns[i+1].mBitCount = bits;
            out.mColumns[i+1].mType = avgCol[i].mCol.mType;
            out.mColumns[i+1].mData.resize(nOutRows, bits);
        }

        // Adding the count col
        out.mColumns[m+1].mName = "Count";
        out.mColumns[m+1].mBitCount = sizeof(oc::u64) * 8;
        out.mColumns[m+1].mType = TypeID::IntID;
        out.mColumns[m+1].mData.resize(nOutRows, sizeof(oc::u64) * 8);

    }

    oc::BitVector cirEval(oc::BetaCircuit* cir, std::vector<oc::BitVector>& inputs,
             oc::BitVector& output, u8* data, u64 bits, u64 bytes)
    {

        auto size = bytes * 8;
        // Filling extra bits with zero
        u64 rem = size - bits;
        inputs[1].reset(rem);
        inputs[1].append(data , bits);

        // i64 cc = 0;
        // memcpy(&cc, inputs[0].data(), inputs[0].sizeBytes());  
        // std::cout << " input1: " << cc;                  

        std::vector<oc::BitVector> tempOutputs = {output};
        cir->evaluate( inputs, tempOutputs );

        return tempOutputs[0];
    }

    Table join(const ColRef& l, const ColRef& r, std::vector<ColRef> select)
    {
        // std::unordered_map<oc::block, u64>
        auto LPerm = sort(l);
        auto RPerm = sort(r);

        // std::cout << " L " << std::endl;
        // for (u64 i = 0; i < LPerm.size(); ++i)
        // {
        //     std::cout << i << ": " << hex(l.mCol.mData[LPerm[i]]) << std::endl;
        // }

        std::vector<std::array<u64, 2>> I;
        std::vector<u64> rIdx(r.mCol.rows());
        I.reserve(r.mCol.rows());

        u64 lIdx = 0;
        for (u64 i = 0; i < r.mCol.rows(); ++i)
        {
            while (
                lIdx < l.mCol.rows() &&
                lessThan(l.mCol.mData[LPerm[lIdx]], r.mCol.mData[RPerm[i]]))
            {
                if (lIdx)
                {
                    if (eq(l.mCol.mData[LPerm[lIdx - 1]], l.mCol.mData[LPerm[lIdx]]))
                        throw RTE_LOC;// L duplicate key
                }
                ++lIdx;
            }

            if (lIdx < l.mCol.rows())
            {
                if (eq(l.mCol.mData[LPerm[lIdx]], r.mCol.mData[RPerm[i]]))
                {
                    I.push_back({ LPerm[lIdx], RPerm[i] });
                    rIdx[RPerm[i]] = 1;
                }
            }
        }

        for (u64 i = 1; i < rIdx.size(); ++i)
        {
            rIdx[i] += rIdx[i - 1];
        }
        if (rIdx.back() != I.size())
            throw RTE_LOC;

        std::vector<ColumnInfo> colInfo(select.size());
        for (u64 i = 0; i < colInfo.size(); ++i)
        {
            if (&select[i].mTable != &l.mTable &&
                &select[i].mTable != &r.mTable)
                throw std::runtime_error("select statement doesnt match Left and Right table.");

            colInfo[i] = select[i].mCol.getColumnInfo();
        }
        Table ret(I.size(), colInfo);

        for (u64 i = 0; i < I.size(); ++i)
        {

            for (u64 j = 0; j < colInfo.size(); ++j)
            {
                auto d = rIdx[I[i][1]] - 1;
                auto lr = (&select[j].mTable == &r.mTable) ? 1 : 0;
                auto src = select[j].mCol.mData.data(I[i][lr]);
                auto dst = ret.mColumns[j].mData.data(d);
                auto size = ret.mColumns[j].mData.cols();

                memcpy(dst, src, size);
            }
        }

        return ret;
    }

    std::ostream& operator<<(std::ostream& o, const Table& t)
    {
        auto width = 8;
        auto separator = ' ';
        auto printElem = [&](auto&& t)
            {
                o << std::left << std::setw(width) << std::setfill(separator) << t << " ";
            };

        o << "      ";
        for (u64 i = 0; i < t.mColumns.size(); ++i)
            printElem(t.mColumns[i].mName);

        std::cout << "\n-------------------------------" << std::endl;
        for (u64 i = 0; i < t.rows(); ++i)
        {
            o << std::setw(2) << std::setfill(' ') << i << " ";
            if (t.mIsActive.size())
                o << (int)t.mIsActive[i];
            else
                o << 1;

            o << ": ";
            for (u64 j = 0; j < t.mColumns.size(); ++j)
            {
                if (t.mColumns[j].mType == TypeID::StringID)
                    printElem(std::string((const char*)t.mColumns[j].mData.data(i), t.mColumns[j].getByteCount()));
                else
                    printElem(hex(t.mColumns[j].mData.data(i), t.mColumns[j].getByteCount()));
            }
            o << "\n";

        }

        return o;

    }


}
