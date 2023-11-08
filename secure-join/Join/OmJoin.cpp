#include "OmJoin.h"
#include "libOTe/Tools/LinearCode.h"

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
        auto rows0 = leftJoinCol.mCol.mData.rows();
        auto rows1 = rightJoinCol.mCol.mData.rows();
        auto compressesSize = mStatSecParam + log2(rows0) + log2(rows1);
        auto bits = leftJoinCol.mCol.getBitCount();

        if (leftJoinCol.mCol.getBitCount() != rightJoinCol.mCol.getBitCount())
            throw RTE_LOC;

        if (leftJoinCol.mCol.getBitCount() <= compressesSize)
        {

            auto size0 = leftJoinCol.mCol.mData.size();
            auto size1 = rightJoinCol.mCol.mData.size();
            BinMatrix keys(rows0 + rows1, bits);

            u8* d0 = keys.data();
            u8* d1 = keys.data() + size0;
            u8* s0 = leftJoinCol.mCol.mData.data();
            u8* s1 = rightJoinCol.mCol.mData.data();
            memcpy(d0, s0, size0);
            memcpy(d1, s1, size1);
            return keys;

        }
        else
        {
            PRNG prng(oc::ZeroBlock);
            oc::LinearCode code;
            code.random(prng, bits, compressesSize);

            BinMatrix keys(rows0 + rows1, bits);

            for (u64 i = 0; i < rows0; ++i)
                code.encode(
                    leftJoinCol.mCol.mData.data(i),
                    keys.data(i));

            for (u64 i = 0, j = rows0; i < rows1; ++i, ++j)
                code.encode(
                    rightJoinCol.mCol.mData.data(i), 
                    keys.data(j));

            return keys;
        }
    }

    // this circuit compares two inputs for equality with the exception that
    // the first bit is ignored.
    oc::BetaCircuit OmJoin::getControlBitsCircuit(u64 bitCount)
    {
        //oc::BetaLibrary lib;
        //return *lib.int_eq(bitCount);

        // oc::BetaLibrary lib;
        oc::BetaCircuit cd;
        oc::BetaBundle a1(bitCount);
        oc::BetaBundle a2(bitCount);
        oc::BetaBundle out(1);
        auto bits = a1.mWires.size();
        BetaBundle temp(bits);

        cd.addInputBundle(a1);
        cd.addInputBundle(a2);
        cd.addOutputBundle(out);

        if (bits == 1)
            temp[0] = out[0];
        else
            cd.addTempWireBundle(temp);

        for (u64 i = 0; i < bits; ++i)
        {
            cd.addGate(a1.mWires[i], a2.mWires[i],
                oc::GateType::Nxor, temp.mWires[i]);
        }

        auto levels = oc::log2ceil(bits);
        for (u64 i = 0; i < levels; ++i)
        {
            auto step = 1ull << i;
            auto size = bits / 2 / step;
            BetaBundle temp2(size);
            if (size == 1)
                temp2[0] = out[0];
            else
                cd.addTempWireBundle(temp2);

            for (u64 j = 0; j < size; ++j)
            {
                cd.addGate(
                    temp[2 * j + 0],
                    temp[2 * j + 1],
                    oc::GateType::And,
                    temp2[j]
                );
            }

            temp = std::move(temp2);
        }
        return cd;
    }

    macoro::task<> OmJoin::getControlBits(
        BinMatrix& data,
        u64 keyByteOffset,
        u64 keyBitCount,
        coproto::Socket& sock,
        BinMatrix& out,
        CorGenerator& ole,
        PRNG&prng)
    {
        MC_BEGIN(macoro::task<>, &data, &sock, &out, &ole, keyByteOffset, keyBitCount, &prng,
            cir = oc::BetaCircuit{},
            sKeys = BinMatrix{},
            bin = Gmw{},
            n = u64{},
            keyByteSize = u64{});

        n = data.numEntries();
        keyByteSize = oc::divCeil(keyBitCount, 8);
        cir = getControlBitsCircuit(keyBitCount);
        sKeys.resize(data.rows() + 1, keyBitCount);
        for (u64 i = 0; i < n; ++i)
        {
            memcpy(sKeys.data(i + 1), data.data(i) + keyByteOffset, keyByteSize);
        }
        bin.init(n, cir);

        bin.setInput(0, sKeys.subMatrix(0, n));
        bin.setInput(1, sKeys.subMatrix(1, n));

        MC_AWAIT(bin.run(ole, sock, prng));

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
        //auto n = cols[0]->rows();
        //auto d0 = dst.data();
        auto e0 = dst.data() + dst.size();

        std::vector<u64>
            offsets(m),
            sizes(m),
            srcSteps(m);
        std::vector<u8*> srcs(m);
        u64 rem = dst.cols();
        for (u64 i = 0; i < m; ++i)
        {
            sizes[i] = oc::divCeil(cols[i]->bitsPerEntry(), 8);
            if (i)
                offsets[i] = offsets[i - 1] + sizes[i - 1];

            srcs[i] = cols[i]->data();
            srcSteps[i] = cols[i]->mData.cols();
        }

        for (u64 j = 0; j < m; ++j)
        {
            auto n = cols[j]->rows();
            assert(n <= dst.rows());
            auto d0 = dst.data() + offsets[j];

            auto src = srcs[j];
            auto size = sizes[j];
            auto step = srcSteps[j];
            for (u64 i = 0; i < n; ++i)
            {
                assert(d0 + size <= e0);
                memcpy(d0, src, size);

                src += step;
                d0 += rem;
            }
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
        BinMatrix& ret,
        u8 role,
        std::vector<Offset>& offsets
    )
    {
        u64 m = selects.size();
        u64 n0 = leftJoinCol.mCol.rows();
        rowSize = 0;

        std::vector<BinMatrix*> left;

        offsets.clear();
        offsets.reserve(m + 1);
        for (u64 i = 0; i < m; ++i)
        {
            if (&leftJoinCol.mTable == &selects[i].mTable)
            {
                auto bytes = oc::divCeil(selects[i].mCol.getBitCount(), 8);
                assert(bytes == selects[i].mCol.getByteCount());
                assert(selects[i].mCol.rows() == n0);

                left.emplace_back(&selects[i].mCol.mData);
                offsets.emplace_back(Offset{ rowSize * 8, selects[i].mCol.mData.bitsPerEntry(), selects[i].mCol.mName });
                rowSize += bytes;
            }
        }

        offsets.emplace_back(Offset{ rowSize * 8, keys.bitsPerEntry(), "key*" });
        left.emplace_back(&keys);

        ret.resize(n0 + numDummies, rowSize * 8 + keys.bitsPerEntry() + 1);
        concatColumns(ret, left);

        auto flagBit = rowSize * 8 + keys.bitsPerEntry();
        offsets.emplace_back(Offset{ flagBit, 1, "LFlag" });

        if (role)
        {
            for (u64 i = 0; i < n0; ++i)
            {
                *oc::BitIterator((u8*)ret.data(i), flagBit) = 1;
            }
        }
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
        SharedTable& out,
        std::vector<Offset>& offsets)
    {
        u64 nL = left.mCol.rows();
        u64 nR = data.rows() - nL;
        u64 m = selects.size();

        std::vector<u64> sizes; //, dSteps(m);
        std::vector<u8*> srcs, dsts;

        out.mColumns.resize(selects.size());
        // std::cout << "start row " << nL << std::endl;
        // u64 leftOffset = 0;
        u64 rightOffset = 0, k = 0;
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

                assert(rightOffset == oc::divCeil(offsets[k].mStart + offsets[k].mSize, 8));
                ++k;
            }
            else
            {
                oData = selects[i].mCol.mData;
            }
        }
        assert(rightOffset == data.bytesPerEntry() - 1);
        assert(rightOffset * 8 == offsets[k].mStart);
        assert(1 == offsets[k].mSize);
        out.mIsActive.resize(nR);
        sizes.push_back(1);
        dsts.push_back(out.mIsActive.data());
        srcs.push_back(&data(nL, rightOffset));

        auto srcStep = data.cols();
        for (u64 i = 0; i < nR; ++i)
        {
            // std::cout << "out " << i << " ";
            for (u64 j = 0; j < sizes.size(); ++j)
            {
                // std::cout << hex(span<u8>(srcs[j], sizes[j])) << " ";
                if (j != sizes.size() - 1)
                {
                    assert(srcs[j] == &data(nL + i, offsets[j].mStart / 8));
                    assert(sizes[j] == oc::divCeil(offsets[j].mSize, 8));
                    // assert(dsts[j] == )
                }


                memcpy(dsts[j], srcs[j], sizes[j]);
                dsts[j] += sizes[j];
                srcs[j] += srcStep;
            }
            // std::cout << std::endl;
        }
        // std::cout << out << std::endl;
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

    macoro::task<> OmJoin::print(
        const BinMatrix& data,
        const BinMatrix& control,
        coproto::Socket& sock,
        int role,
        std::string name,
        std::vector<OmJoin::Offset>& offsets)
    {
        MC_BEGIN(macoro::task<>, &data, &control, &sock, role, name, &offsets,
            D = BinMatrix{},
            C = BinMatrix{});

        if (role)
        {
            MC_AWAIT(sock.send(data));

            if (control.size())
                MC_AWAIT(sock.send(control));
        }
        else
        {
            D.resize(data.numEntries(), data.bytesPerEntry() * 8);
            MC_AWAIT(sock.recv(D));
            if (control.size())
                C.resize(control.numEntries(), control.bitsPerEntry());
            if (control.size())
                MC_AWAIT(sock.recv(C));


            for (u64 i = 0; i < D.size(); ++i)
                D(i) ^= data(i);
            for (u64 i = 0; i < C.size(); ++i)
                C(i) ^= control(i);

            std::cout << name << std::endl << "        ";
            for (auto o : offsets)
                std::cout << o.mName << "  ";
            std::cout << std::endl;
            oc::BitVector bv;
            for (u64 i = 0; i < D.numEntries(); ++i)
            {
                std::cout << i << ": " << (C.size() ? (int)C(i) : -1) << " ~ ";// << hex(D[i]) << std::endl;
                for (auto o : offsets)
                {
                    assert(D.bitsPerEntry() >= o.mSize + o.mStart);
                    bv.resize(0);
                    bv.append(D[i].data(), o.mSize, o.mStart);
                    trimSpan(bv.getSpan<u8>(), bv.size());
                    std::cout << bv.hex() << " ";
                    ///bv.resize(o.mSize);
                    //b/v.getSpan<u8>().back() = 0;
                }
                std::cout << std::endl;
            }
        }
        MC_END();
    }

    // Active Flag = LFlag & Controlbits
    macoro::task<> OmJoin::updateActiveFlag(
        BinMatrix& data,
        BinMatrix& choice,
        BinMatrix& out,
        CorGenerator& ole,
        PRNG& prng,
        coproto::Socket& sock)
    {
        MC_BEGIN(macoro::task<>, &data, &choice, &out, &ole, &sock, &prng,
            gmw = Gmw{},
            cir = oc::BetaCircuit{},
            temp = BinMatrix{},
            offsets = std::vector<Offset>{},
            offset = u64{});

        cir = *oc::BetaLibrary{}.int_int_bitwiseAnd(1, 1, 1);
        gmw.init(data.rows(), cir);

        if (data.bitsPerEntry() % 8 != 1)
        {
            std::cout << "logic error, need to fix. " << LOCATION << std::endl;
            throw RTE_LOC;
        }
        offset = data.bitsPerEntry() / 8;

        temp.resize(data.rows(), 1);
        for (u64 i = 0; i < data.rows(); ++i)
            temp(i) = data(i, offset);//*oc::BitIterator(data[i].data(), data.bitsPerEntry() - 1);

        gmw.setInput(0, choice);
        gmw.setInput(1, temp);

        offsets.emplace_back(Offset{ 0,1 });
        //MC_AWAIT(print(temp, choice, sock, ole.partyIdx(), "active", offsets));

        MC_AWAIT(gmw.run(ole, sock, prng));

        gmw.getOutput(0, temp);

        //MC_AWAIT(print(temp, choice, sock, ole.partyIdx(), "active out", offsets));

        out.resize(data.rows(), data.bitsPerEntry());
        for (u64 i = 0; i < data.rows(); ++i)
        {
            memcpy(out[i].subspan(0, offset), data[i].subspan(0, offset));
            out(i, offset) = temp(i);
            //*oc::BitIterator(data[i].data(), data.bitsPerEntry() - 1) = temp(i);
        }
        MC_END();
    }
    void appendChoiceBit(BinMatrix& data, BinMatrix& choice, BinMatrix& out)
    {
        auto n = data.rows();
        auto m = data.bitsPerEntry();
        auto m8 = oc::divCeil(m, 8);
        out.resize(n, m + 8);
        for (u64 i = 0; i < n; ++i)
        {
            memcpy(out.data(i), data.data(i), m8);
            out(i, m8) = choice(i);
        }
    }



    // leftJoinCol should be unique
    macoro::task<> OmJoin::join(
        ColRef leftJoinCol,
        ColRef rightJoinCol,
        std::vector<ColRef> selects,
        SharedTable& out,
        PRNG& prng,
        CorGenerator& ole,
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
            dup = AggTree::Operator{},
            offsets = std::vector<Offset>{},
            bytesPermuted0 = u64{},
            bytesPermuted1 = u64{}, 
            prepro = macoro::eager_task<>{});

        setTimePoint("start");

        // left keys kL followed by the right keys kR
        keys = loadKeys(leftJoinCol, rightJoinCol);
        setTimePoint("load");

        if (mInsecurePrint)
        {
            offsets = { Offset{0,keys.bitsPerEntry(), "key"} };
            MC_AWAIT(print(keys, controlBits, sock, ole.partyIdx(), "keys", offsets));
        }

        sort.mInsecureMock = mInsecureMockSubroutines;
        sPerm.mInsecureMock = mInsecureMockSubroutines;


        // if the forward direction we will permute the keys, a flag, 
        // and all of the select columns of the left table. In the 
        // backwards direction, we will unpermute the left table select
        // columns. Therefore, in total we will permute:
        bytesPermuted0 = oc::divCeil(keys.bitsPerEntry() + 1, 8);
        bytesPermuted1 = 1;
        for (u64 i = 0; i < selects.size(); ++i)
        {
            auto isLeft = &selects[i].mTable == &leftJoinCol.mTable;
            //auto notKey = &leftJoinCol.mCol != &selects[i].mCol;
            if (isLeft)// && notKey)
            {
                bytesPermuted0 += selects[i].mCol.getByteCount();
                bytesPermuted1 += selects[i].mCol.getByteCount();
            }
        }

        sort.init(ole.partyIdx(), keys.rows(), keys.bitsPerEntry(), bytesPermuted0 + bytesPermuted1);
        sort.request(ole);
        prepro = sort.preprocess(sock, prng) |macoro::make_eager();

        // get the stable sorting permutation sPerm
        MC_AWAIT(sort.genPerm(keys, sPerm, sock,prng));
        setTimePoint("sort");

        if (mInsecurePrint)
            MC_AWAIT(print(data, controlBits, sock, ole.partyIdx(), "preSort", offsets));

        // gather all of the columns from the left table and concatinate them
        // together. Append dummy rows after that. Then add the column of keys
        // to that. So it will look something like:
        //     L | kL | 1
        //     0 | kR | 0
        concatColumns(leftJoinCol, selects, rightJoinCol.mTable.rows(), keys, keyOffset, data, ole.partyIdx(), offsets);
        setTimePoint("concat");
        keys.mData = {};

        // Apply the sortin permutation. What you end up with are the keys
        // in sorted order and the rows of L also in sorted order.
        temp.resize(data.numEntries(), data.bitsPerEntry() + 8);
        temp.resize(data.numEntries(), data.bitsPerEntry());

        assert(data.bytesPerEntry() == bytesPermuted0);

        MC_AWAIT(sPerm.apply(PermOp::Inverse, data, temp, prng, sock));//, ole
        std::swap(data, temp);
        setTimePoint("applyInv-sort");
        //std::cout << "Perm::apply done " << LOCATION << std::endl;

        if (mInsecurePrint)
            MC_AWAIT(print(data, controlBits, sock, ole.partyIdx(), "sort", offsets));

        // compare adjacent keys. controlBits[i] = 1 if k[i]==k[i-1].
        // put another way, controlBits[i] = 1 if keys[i] is from the
        // right table and has a matching key from the left table.
        MC_AWAIT(getControlBits(data, keyOffset, keys.bitsPerEntry(), sock, controlBits, ole, prng));
        setTimePoint("control");
        //std::cout << "controlBits done " << LOCATION << std::endl;

        // reshape data so that the key at then end of each row are discarded.
        offsets.pop_back();
        offsets.back() = Offset{ keyOffset * 8, 1, "LFlag*" };
        data.reshape(keyOffset * 8 + 1);
        temp.reshape(keyOffset * 8 + 1);
        for (u64 i = 0; i < data.rows(); ++i)
            data(i, keyOffset) = *oc::BitIterator(data.data(i), keyOffset * 8 + keys.bitsPerEntry());

        if (mInsecurePrint)
            MC_AWAIT(print(data, controlBits, sock, ole.partyIdx(), "control", offsets));

        // duplicate the rows in data that are from L into any matching
        // rows that correspond to R.
        dup = getDupCircuit();
        MC_AWAIT(aggTree.apply(data, controlBits, dup, AggTreeType::Prefix, sock, ole, prng, temp));
        std::swap(data, temp);
        setTimePoint("duplicate");

        //std::cout << "AggTree done " << LOCATION << std::endl;

        if (mInsecurePrint)
            MC_AWAIT(print(data, controlBits, sock, ole.partyIdx(), "agg", offsets));


        MC_AWAIT(updateActiveFlag(data, controlBits, temp, ole, prng, sock));
        std::swap(data, temp);
        //std::cout << "Active done " << LOCATION << std::endl;

        if (mInsecurePrint)
            MC_AWAIT(print(data, controlBits, sock, ole.partyIdx(), "isActive", offsets));


        // appendChoiceBit(data, controlBits, temp);
        //std::swap(data, temp);

        // unpermute `data`. What we are left with is
        //     L
        //     L'
        // where L' are the matching rows from L for each row of R.
        // That is, L' || R is the result we want.
        //data.resize(data.numEntries(), data.cols() * 8);
        temp.resize(data.numEntries(), data.bitsPerEntry());
        temp.reshape(data.bitsPerEntry());
        temp.setZero();
        assert(data.bytesPerEntry() == bytesPermuted1);
        MC_AWAIT(sPerm.apply(PermOp::Regular, data, temp, prng, sock));//, ole
        std::swap(data, temp);
        setTimePoint("apply-sort");


        //std::cout << "Perm::Apply inv done " << LOCATION << std::endl;


        if (mInsecurePrint)
            MC_AWAIT(print(data, controlBits, sock, ole.partyIdx(), "unsort", offsets));




        // unpack L' in `data` and write the result to `out`.
        getOutput(data, selects, leftJoinCol, out, offsets);


        MC_END();
    }
}