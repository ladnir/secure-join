#include "OmJoin.h"

namespace secJoin
{

    // output a combined table that has the leftColumn
    // concatenated with the rightColumn (doubling the
    // number of rows). THe left column will have a
    // zero appended as its LSB while the right gets
    // a one appended.
    BinMatrix OmJoin::loadKeys(
        ColRef leftJoinCol,
        ColRef rightJoinCol)
    {
        if (leftJoinCol.mCol.getBitCount() != rightJoinCol.mCol.getBitCount())
            throw RTE_LOC;

        auto bits = leftJoinCol.mCol.getBitCount();
        BinMatrix keys(leftJoinCol.mCol.mData.numEntries() + rightJoinCol.mCol.mData.numEntries(), bits);
        auto size0 = leftJoinCol.mCol.mData.size();
        auto size1 = rightJoinCol.mCol.mData.size();

        u8* d0 = keys.data();
        u8* d1 = keys.data() + size0;
        u8* s0 = leftJoinCol.mCol.mData.data();
        u8* s1 = rightJoinCol.mCol.mData.data();
        memcpy(d0, s0, size0);
        memcpy(d1, s1, size1);

        return keys;
    }

    // this circuit compares two inputs for equality with the exception that
    // the first bit is ignored.
    oc::BetaCircuit OmJoin::getControlBitsCircuit(u64 bitCount)
    {
        oc::BetaLibrary lib;
        return *lib.int_eq(bitCount);
    }

    macoro::task<> OmJoin::getControlBits(
        BinMatrix& sKeys,
        u64 keyOffset,
        coproto::Socket& sock,
        BinMatrix& out,
        OleGenerator& ole)
    {
        MC_BEGIN(macoro::task<>, &sKeys, &sock, &out, &ole,
            cir = oc::BetaCircuit{},
            bin = Gmw{},
            n = u64{}, m = u64{});
        cir = getControlBitsCircuit(sKeys.bitsPerEntry());
        bin.init(sKeys.numEntries(), cir, ole);

        bin.setInput(0, sKeys);

        n = sKeys.numEntries();
        m = sKeys.bytesPerEntry();

        // shift the keys by 1 row
        {
            auto s0 = sKeys[0].data();
            auto s1 = s0 + m;
            memmove(s1, s0, (n - 1) * m);
        }

        bin.setInput(1, sKeys);

        MC_AWAIT(bin.run(sock));

        out.resize(n, 1);
        bin.getOutput(0, out);
        out.mData(0) = 0;

        MC_END();
    }

    void OmJoin::concatColumns(
        BinMatrix& dst,
        span<BinMatrix*> cols)
    {
        auto m = cols.size();
        auto n = cols[0]->rows();
        auto d0 = dst.data();
        auto e0 = dst.data() + dst.size();

        std::vector<u64>
            sizes(m),
            srcSteps(m);
        std::vector<u8*> srcs(m);
        u64 rem = dst.cols();
        for (u64 i = 0; i < m; ++i)
        {
            sizes[i] = oc::divCeil(cols[i]->bitsPerEntry(), 8);
            srcs[i] = cols[i]->data();

            assert(rem >= sizes[i]);
            rem -= sizes[i];
            srcSteps[i] = cols[i]->mData.cols();
        }

        for (u64 i = 0; i < n; ++i)
        {
            for (u64 j = 0; j < m; ++j)
            {
                assert(d0 + sizes[j] <= e0);

                // std::cout << hex({srcs[j], sizes[j]}) << " " << std::flush;
                memcpy(d0, srcs[j], sizes[j]);

                srcs[j] += srcSteps[j];
                d0 += sizes[j];
            }
            // std::cout << std::endl;

            d0 += rem;
        }
    }

    // concatinate all the columns in `select` that are part of the left table.
    // Then append `numDummies` empty rows to the end.
    void OmJoin::concatColumns(
        ColRef leftJoinCol,
        span<ColRef> selects,
        u64 numDummies,
        BinMatrix& keys,
        u64& rowSize,
        BinMatrix& ret)
    {
        u64 m = selects.size();
        u64 n0 = leftJoinCol.mCol.rows();
        rowSize = 0;

        std::vector<BinMatrix*> left;

        for (u64 i = 0; i < m; ++i)
        {
            auto bytes = oc::divCeil(selects[i].mCol.getBitCount(), 8);
            if (&leftJoinCol.mTable == &selects[i].mTable)
            {
                assert(selects[i].mCol.rows() == n0);
                left.emplace_back(&selects[i].mCol.mData);
                rowSize += bytes;
            }
        }
        left.emplace_back(&keys);

        ret.resize(n0 + numDummies, (rowSize + keys.bytesPerEntry()) * 8);
        concatColumns(ret, left);
    }

    // the aggTree and unpermute setps gives us `data` which looks like
    //     L
    //     L'
    // where L' are the matching rows copied from L for each row of R.
    // That is, L' || R is the result we want.
    // getOutput(...) unpack L' in `data` and write the result to `out`.
    void OmJoin::getOutput(
        BinMatrix& data,
        span<ColRef> selects,
        ColRef& left,
        SharedTable& out)
    {
        u64 nL = left.mCol.rows();
        u64 nR = data.rows() - nL;
        u64 m = selects.size();

        std::vector<u64> sizes; //, dSteps(m);
        std::vector<u8*> srcs, dsts;

        out.mColumns.resize(selects.size());

        // u64 leftOffset = 0;
        u64 rightOffset = 0;
        for (u64 i = 0; i < m; ++i)
        {
            out[i].mCol.mName = selects[i].mCol.mName;
            out[i].mCol.mBitCount = selects[i].mCol.mBitCount;
            out[i].mCol.mType = selects[i].mCol.mType;
            auto& oData = out[i].mCol.mData;

            if (&left.mTable == &selects[i].mTable)
            {
                oData.resize(nR, selects[i].mCol.getBitCount());

                sizes.push_back(oData.bytesPerEntry());
                dsts.push_back(oData.data());
                srcs.push_back(&data(nL, rightOffset));
                rightOffset += sizes.back();
            }
            else
            {
                oData = selects[i].mCol.mData;
            }
        }
        assert(rightOffset == data.bytesPerEntry() - 1);
        out.mIsActive.resize(nR);
        sizes.push_back(1);
        dsts.push_back(out.mIsActive.data());
        srcs.push_back(&data(nL, rightOffset));

        auto srcStep = data.cols();
        for (u64 i = 0; i < nR; ++i)
        {
            //std::cout << "out " << i << " ";
            for (u64 j = 0; j < sizes.size(); ++j)
            {
                //std::cout << hex(span<u8>(srcs[j], sizes[j])) << " ";
                memcpy(dsts[j], srcs[j], sizes[j]);
                dsts[j] += sizes[j];
                srcs[j] += srcStep;
            }
            //std::cout << std::endl;
        }
    }

    AggTree::Operator OmJoin::getDupCircuit()
    {
        return [](
            oc::BetaCircuit& c,
            const oc::BetaBundle& left,
            const oc::BetaBundle& right,
            oc::BetaBundle& out)
            {
                for (u64 i = 0; i < left.size(); ++i)
                    c.addCopy(left[i], out[i]);
            };
    }

    // leftJoinCol should be unique
    macoro::task<> OmJoin::join(
        ColRef leftJoinCol,
        ColRef rightJoinCol,
        std::vector<ColRef> selects,
        SharedTable& out,
        oc::PRNG& prng,
        OleGenerator& ole,
        coproto::Socket& sock)
    {
        MC_BEGIN(macoro::task<>, this, leftJoinCol, rightJoinCol, selects, &out, &prng, &ole, &sock,
            keys = BinMatrix{},
            sPerm = AdditivePerm{},
            controlBits = BinMatrix{},
            data = BinMatrix{},
            temp = BinMatrix{},
            aggTree = AggTree{},
            sort = RadixSort{},
            keyOffset = u64{},
            dup = AggTree::Operator{});

        setTimePoint("start");

        // left keys kL followed by the right keys kR
        keys = loadKeys(leftJoinCol, rightJoinCol);
        setTimePoint("load");

        // get the stable sorting permutation sPerm
        MC_AWAIT(sort.genPerm(keys, sPerm, ole, sock));
        setTimePoint("sort");

        // gather all of the columns from the left table and concatinate them
        // together. Append dummy rows after that. Then add the column of keys
        // to that. So it will look something like:
        //     L | kL
        //     0 | kR
        concatColumns(leftJoinCol, selects, rightJoinCol.mTable.rows(), keys, keyOffset, data);
        setTimePoint("concat");
        keys = {};

        // Apply the sortin permutation. What you end up with are the keys
        // in sorted order and the rows of L also in sorted order.
        temp.resize(data.numEntries(), data.bitsPerEntry() + 8);
        temp.resize(data.numEntries(), data.bitsPerEntry());
        MC_AWAIT(sPerm.apply<u8>(data.mData, temp.mData, prng, sock, ole, true));
        std::swap(data, temp);
        setTimePoint("applyInv-sort");

        // compare adjacent keys. controlBits[i] = 1 if k[i]==k[i-1].
        // put another way, controlBits[i] = 1 if keys[i] is from the
        // right table and has a matching key from the left table.
        MC_AWAIT(getControlBits(data, keyOffset, sock, controlBits, ole));
        setTimePoint("control");

        // reshape data so that the key at then end of each row are discarded.
        data.reshape(keyOffset * 8);
        temp.reshape(keyOffset * 8);

        // duplicate the rows in data that are from L into any matching
        // rows that correspond to R.
        dup = getDupCircuit();
        MC_AWAIT(aggTree.apply(data, controlBits, dup, AggTreeType::Prefix, sock, ole, temp));
        std::swap(data, temp);
        setTimePoint("duplicate");

        data.reshape(data.bitsPerEntry() + 1);
        for (u64 i = 0; i < data.numEntries(); ++i)
            data.mData[i].back() = controlBits(i);

        // unpermute `data`. What we are left with is
        //     L
        //     L'
        // where L' are the matching rows from L for each row of R.
        // That is, L' || R is the result we want.
        temp.resize(data.numEntries(), data.bitsPerEntry());
        MC_AWAIT(sPerm.apply<u8>(data.mData, temp.mData, prng, sock, ole, false));
        std::swap(data, temp);
        setTimePoint("apply-sort");

        // unpack L' in `data` and write the result to `out`.
        getOutput(data, selects, leftJoinCol, out);

        MC_END();
    }
}