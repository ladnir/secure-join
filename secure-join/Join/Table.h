#pragma once

#include "coproto/Common/span.h"
#include <cryptoTools/Common/MatrixView.h>
#include <fstream>
#include "secure-join/Util/Matrix.h"
#include "secure-join/Defines.h"
#include "coproto/coproto.h"
#include "cryptoTools/Common/BitVector.h"
#include "cryptoTools/Circuit/BetaCircuit.h"
#include <vector>

namespace secJoin
{
    enum class TypeID
    {
        IntID = 0,
        StringID = 1
    };
    using ColumnInfo = std::tuple<std::string, TypeID, u64>;

    class Column
    {
    public:
        Column() = default;
        Column(const Column&) = default;
        Column(Column&&) = default;
        Column& operator=(const Column&) = default;
        Column& operator=(Column&&) = default;

        Column(std::string name, TypeID type, u64 size)
            : mType(type), mBitCount(size), mName(std::move(name))
        {
            if (mType == TypeID::StringID && mBitCount % 8)
                throw std::runtime_error("String type must have a multiple of 8 bits. " LOCATION);
        }

        TypeID mType;
        u64 mBitCount = 0;
        std::string mName;

        u64 getByteCount() const { return (getBitCount() + 7) / 8; }
        u64 getBitCount() const { return mBitCount; }
        TypeID getTypeID() const { return mType; }

        secJoin::BinMatrix mData;

        u8* data() { return mData.data(); }
        auto size() { return mData.size(); }
        const u8* data() const { return mData.data(); }
        u64 rows() { return mData.rows(); }
        u64 cols() { return mData.cols(); }


        ColumnInfo getColumnInfo() const
        {
            return { mName, mType, mBitCount };
        }


        bool operator!=(const Column& o) const
        {
            return !(*this == o);
        }
        bool operator==(const Column& o) const
        {
            if (mBitCount != o.mBitCount)
                return false;
            if (mName != o.mName)
                return false;
            return mData == o.mData;
        }
    };

    class Table;

    struct ColRef
    {
        Table& mTable;
        Column& mCol;

        ColRef(Table& t, Column& c)
            : mTable(t), mCol(c)
        {
        }

        ColRef(const ColRef&) = default;
        ColRef(ColRef&&) = default;
    };
    class Table
    {
    public:
        std::vector<u8> mIsActive;
        std::vector<Column> mColumns;

        Table() = default;
        Table(const Table&) = default;
        Table(Table&&) = default;

        Table& operator=(const Table&) = default;
        Table& operator=(Table&&) = default;

        Table(u64 rows, std::vector<ColumnInfo> columns)
        {
            init(rows, columns);
        }

        void init(u64 rows, std::vector<ColumnInfo> columns)
        {
            mColumns.reserve(columns.size());
            // mColumns.resize(columns.size());
            for (u64 i = 0; i < columns.size(); ++i)
            {
                mColumns.emplace_back(
                    std::get<0>(columns[i]),
                    std::get<1>(columns[i]),
                    std::get<2>(columns[i]));
                // auto size = (std::get<2>(columns[i]) + 7) / 8;
                mColumns.back().mData.resize(rows, std::get<2>(columns[i]));
            }
        }

        std::vector<ColumnInfo> getColumnInfo() const
        {
            std::vector<ColumnInfo> ret(mColumns.size());
            for (u64 i = 0; i < ret.size(); ++i)
            {
                ret[i] = mColumns[i].getColumnInfo();
            }
            return ret;
        }

        void resize(u64 n)
        {
            for (u64 i = 0; i < mColumns.size(); ++i)
                mColumns[i].mData.resize(n, mColumns[i].mBitCount);
        }

        u64 rows() const { return mColumns.size() ? mColumns[0].mData.numEntries() : 0; }

        u64 cols() { return mColumns.size() ? mColumns.size() : 0; }

        ColRef operator[](std::string c)
        {
            for (u64 i = 0; i < mColumns.size(); ++i)
            {
                if (mColumns[i].mName == c)
                    return { *this, mColumns[i] };
            }

            throw std::runtime_error(c + " Col not found " + LOCATION + "\n");
        }

        ColRef operator[](u64 i)
        {
            return { *this, mColumns[i] };
        }

        bool operator!=(const Table& o) const
        {
            return !(*this == o);
        }
        bool operator==(const Table& o) const
        {
            if (getColumnInfo() != o.getColumnInfo())
                return false;
            if (rows() != o.rows())
                return false;

            for (u64 j = 0; j < mColumns.size(); ++j)
            {
                if (mColumns[j] != o.mColumns[j])
                    return false;
            }

            return true;
        }
    };


    std::ostream& operator<<(std::ostream& o, const Table& t);

    macoro::task<> revealLocal(const Table& share, coproto::Socket& sock, Table& out);
    macoro::task<> revealRemote(const Table& share, coproto::Socket& sock);
    void populateTable(Table& tb, std::string& fileName, oc::u64 rowCount);
    void populateTable(Table& tb, std::istream& in, oc::u64 rowCount);
    void share(Table& table, std::array<Table, 2>& shares,
        PRNG& prng);
    Table join(const ColRef& l, const ColRef& r, std::vector<ColRef> select);
    oc::BitVector cirEval(oc::BetaCircuit* cir, std::vector<oc::BitVector>& inputs,
        oc::BitVector& output, u8* data, u64 bits, u64 bytes);
    void populateOutTable(Table& out, std::vector<ColRef> avgCol,
        ColRef groupByCol, u64 nOutRows);
    void copyTableEntry(Table& out, ColRef groupByCol, std::vector<ColRef> avgCol,
        std::vector<oc::BitVector>& inputs, BinMatrix& ones,
        u64 curOutRow, u64 nOutRows, u64 row);
    Table average(ColRef groupByCol, std::vector<ColRef> avgCol);
    // class SharedTable
    // {
    // public:
    //     // shared keys are stored in packed binary format. i.e. XOR shared and trasposed.
    //     std::vector<SharedColumn> mColumns;

    //     struct ColRef
    //     {
    //         SharedTable& mTable;
    //         SharedColumn& mCol;

    //         ColRef(SharedTable& t, SharedColumn& c)
    //             : mTable(t), mCol(c)
    //         {}

    //         ColRef(const ColRef&) = default;
    //         ColRef(ColRef&&) = default;

    //     };

    //     ColRef operator[](std::string c)
    //     {
    //         for (u64 i = 0; i < mColumns.size(); ++i)
    //         {
    //             if (mColumns[i].mName == c)
    //                 return { *this, mColumns[i] };
    //         }

    //         throw RTE_LOC;
    //     }

    //     ColRef operator[](u64 i)
    //     {
    //         return { *this, mColumns[i] };
    //     }

    //     u64 rows();
    // };

    // namespace selectDetails
    // {

    //     enum SelectOp
    //     {
    //         BitwiseOr,
    //         BitwiseAnd,
    //         Multiply,
    //         Add,
    //         LessThan,
    //         Inverse
    //     };
    //     struct Gate
    //     {
    //         SelectOp op;
    //         int mIn1, mIn2, mOut;
    //     };
    //     struct Input;
    //     struct Output;

    //     struct Mem
    //     {
    //         std::shared_ptr<const DataType> mType;
    //         Gate* mGate = nullptr;
    //         //Input* mInputPtr = nullptr;
    //         //Output* mOutputPtr = nullptr;
    //         int mInputIdx = -1;
    //         int mOutputIdx = -1;
    //         int mNextBatchIdx = -1;
    //         bool mUsed = false;

    //         bool isInput() const { return mInputIdx != -1; }
    //         bool isOutput() const { return mOutputIdx != -1; }
    //     };

    //     struct Input
    //     {
    //         Input(int memIdx, SharedTable::ColRef& c, int p)
    //             : mMemIdx(memIdx)
    //             , mCol(c)
    //             , mPosition(p)
    //         {}

    //         int mMemIdx;
    //         SharedTable::ColRef mCol;
    //         int mPosition;
    //     };

    //     struct Output
    //     {

    //         Output(int memIdx, std::string& c, int p)
    //             : mMemIdx(memIdx)
    //             , mName(c)
    //             , mPosition(p)
    //         {}
    //         int mMemIdx;
    //         std::string mName;
    //         int mPosition;
    //     };

    // }

    // class SelectQuery;
    // class SelectBundle
    // {
    // public:
    //     SelectQuery & mSelect;
    //     int mMemIdx;

    // 	SelectBundle(const SelectBundle&) = default;
    // 	SelectBundle(SelectBundle&&) = default;

    //     SelectBundle(SelectQuery& cir, int memIdx)
    //         : mSelect(cir)
    //         , mMemIdx(memIdx)
    //     {}

    //     SelectBundle operator!() const;
    //     SelectBundle operator|(const SelectBundle& r) const;
    //     SelectBundle operator&(const SelectBundle& r) const;
    //     SelectBundle operator<(const SelectBundle& r) const;
    //     SelectBundle operator*(const SelectBundle& r) const;
    //     SelectBundle operator+(const SelectBundle& r) const;
    // };

    // class SelectQuery
    // {
    // public:

    //     std::string mNoRevealName;
    //     bool mIsUnion = false;

    //     SharedTable * mLeftTable = nullptr;
    //     SharedTable * mRightTable = nullptr;
    //     SharedColumn * mLeftCol = nullptr;
    //     SharedColumn * mRightCol = nullptr;

    //     std::vector<selectDetails::Mem> mMem;
    //     std::vector<selectDetails::Input> mInputs;
    //     std::vector<selectDetails::Output> mOutputs;
    //     std::vector<selectDetails::Gate> mGates;

    //     std::vector<selectDetails::Input*>
    //         mLeftInputs,
    //         mRightInputs;

    //     //SelectQuery(std::vector<SharedTable::ColRef> passThrough)
    //     //{
    //     //    for (auto& p : passThrough)
    //     //        addOutput(p.mCol.mName, addInput(p));
    //     //}

    //     SelectQuery() = default;

    //     SelectBundle addInput(SharedTable::ColRef column);

    //     SelectBundle joinOn(SharedTable::ColRef left, SharedTable::ColRef right);

    //     int addOp(selectDetails::SelectOp op, int wire1, int wire2);

    //     int addOp(selectDetails::SelectOp op, int wire1);

    //     void addOutput(std::string name, const SelectBundle& column);

    //     void apply(BetaCircuit& cir, span<BetaBundle> inputs, span<BetaBundle> outputs) const;

    //     void noReveal(std::string columnName)
    //     {
    //         mNoRevealName = std::move(columnName);
    //     }

    //     bool isNoReveal() const
    //     {
    //         return mNoRevealName.size();
    //     }

    //     void isUnion(bool b) { mIsUnion = b; }
    //     bool isUnion() const { return mIsUnion; }

    //     bool isLeftPassthrough(selectDetails::Output output)const;
    //     bool isRightPassthrough(selectDetails::Output output)const;
    //     bool isCircuitInput(selectDetails::Input input)const;
    // };
}
