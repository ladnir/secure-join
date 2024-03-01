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

            BinMatrix keys(rows0 + rows1, compressesSize);

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
        PRNG&prng)
    {
        MC_BEGIN(macoro::task<>, this, &data, &sock, &out, keyByteOffset, keyBitCount, &prng,
            cir = oc::BetaCircuit{},
            sKeys = BinMatrix{},
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
         //bin.init(n, cir, ole);

        mControlBitGmw.setInput(0, sKeys.subMatrix(0, n));
        mControlBitGmw.setInput(1, sKeys.subMatrix(1, n));

        MC_AWAIT(mControlBitGmw.run(sock));

        out.resize(n, 1);
        mControlBitGmw.getOutput(0, out);
        out.mData(0) = 0;

        MC_END();
    }

    void OmJoin::concatColumns(
        BinMatrix& dst,
        span<BinMatrix*> cols,
        span<Offset> offsets)
    {
        auto m = cols.size();
        //auto n = cols[0]->rows();
        //auto d0 = dst.data();
        auto e0 = dst.data() + dst.size();

        //std::vector<u64>
        //    offsets,
        //    sizes,
        //    srcSteps;
        //std::vector<u8*> srcs;
        //u64 rem = dst.cols();
        //for (u64 i = 0; i < m; ++i)
        //{
        //    if (cols[i] == nullptr)
        //        continue;

        //    sizes.push_back(oc::divCeil(cols[i]->bitsPerEntry(), 8));
        //    if (sizes.size())
        //        offsets.push_back(offsets.back() + sizes.back());
        //    else
        //        offsets.push_back(0);

        //    srcs.push_back(cols[i]->data());
        //    srcSteps.push_back(cols[i]->mData.cols());
        //}

        for (u64 j = 0; j < cols.size(); ++j)
        {
            if (cols[j] == nullptr)
                continue;

            auto n = cols[j]->rows();
            assert(n <= dst.rows());
            assert(offsets[j].mStart % 8 == 0);

            auto d0 = dst.data() + offsets[j].mStart / 8;

            auto src = cols[j]->data();
            auto size = cols[j]->bytesPerEntry();
            assert(divCeil(offsets[j].mSize, 8) == size);
            //auto step = cols[j]->bytesPerEntry();
            for (u64 i = 0; i < n; ++i)
            {
                assert(d0 + size <= e0);
                memcpy(d0, src, size);

                src += size;
                d0 += dst.cols();
            }
        }
    }

    // concatinate all the columns in `select` that are part of the left table.
    // Then append `numDummies` empty rows to the end.
    void OmJoin::concatColumns(
        ColRef leftJoinCol,
        span<ColRef> selects,
        BinMatrix& keys,
        BinMatrix& ret,
        u8 role
    )
    {
        u64 m = selects.size();
        u64 n0 = leftJoinCol.mCol.rows();

        std::vector<BinMatrix*> left;

        for (u64 i = 0; i < m; ++i)
        {
            if (&leftJoinCol.mTable == &selects[i].mTable)
            {
                auto bytes = oc::divCeil(selects[i].mCol.getBitCount(), 8);
                assert(bytes == selects[i].mCol.getByteCount());
                assert(selects[i].mCol.rows() == n0);

                left.emplace_back(&selects[i].mCol.mData);
            }
        }

        left.emplace_back(nullptr);
        left.emplace_back(&keys);

        ret.resize(keys.rows(), mDataBitsPerEntry);
        concatColumns(ret, left, mOffsets);

        if (role)
        {
            //auto flagBit = mDataBitsPerEntry - 1;;
            auto flagByte = mOffsets[mOffsets.size() - 2].mStart / 8;
            for (u64 i = 0; i < n0; ++i)
            {
                ret(i, flagByte) = 1;
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
        Table& out,
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
        assert(8 == offsets[k].mSize);
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
        u64 flagBitIndex,
        PRNG& prng,
        coproto::Socket& sock)
    {
        MC_BEGIN(macoro::task<>, this, &data, &choice, &out, &sock, &prng, flagBitIndex,
            temp = BinMatrix{},
            offsets = std::vector<Offset>{},
            offset = u64{});

        //cir = *oc::BetaLibrary{}.int_int_bitwiseAnd(1, 1, 1);
        ////gmw.init(data.rows(), cir, ole);

        //if (data.bitsPerEntry() % 8 != 1)
        //{
        //    std::cout << "logic error, need to fix. " << LOCATION << std::endl;
        //    throw RTE_LOC;
        //}
        assert(flagBitIndex % 8 == 0);
        offset = flagBitIndex / 8;

        temp.resize(data.rows(), 1);
        for (u64 i = 0; i < data.rows(); ++i)
            temp(i) = data(i, offset);//*oc::BitIterator(data[i].data(), data.bitsPerEntry() - 1);

        mUpdateActiveFlagGmw.setInput(0, choice);
        mUpdateActiveFlagGmw.setInput(1, temp);

        offsets.emplace_back(Offset{ 0,1 });
        //MC_AWAIT(print(temp, choice, sock, ole.partyIdx(), "active", offsets));

        MC_AWAIT(mUpdateActiveFlagGmw.run(sock));

        mUpdateActiveFlagGmw.getOutput(0, temp);
        mUpdateActiveFlagGmw = {};

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

    void OmJoin::init(
        JoinQuerySchema schema,
        CorGenerator& ole)
    {
        u64 rows = schema.mLeftSize + schema.mRightSize;

        auto keySize = std::min<u64>(
            schema.mKey.mBitCount, 
            mStatSecParam + log2(schema.mLeftSize) + log2(schema.mRightSize));

        mPartyIdx = ole.partyIdx();
        mKeyIndex = -1;
        mDataBitsPerEntry = 0;

        mOffsets.clear();
        mOffsets.reserve(schema.mSelect.size() + 1);
        for (u64 i = 0; i < schema.mSelect.size(); ++i)
        {
            // copy all of the left columns except the key
            if (schema.mSelect[i].mIsLeftColumn)
            {
                auto bytes = oc::divCeil(schema.mSelect[i].getBitCount(), 8);
                assert(bytes == schema.mSelect[i].getByteCount());
                mOffsets.emplace_back(
                    Offset{ 
                        mDataBitsPerEntry, 
                        schema.mSelect[i].getBitCount(), 
                        schema.mSelect[i].name() });

                mDataBitsPerEntry += bytes * 8;
            }
        }

        mOffsets.emplace_back(Offset{ mDataBitsPerEntry, 8, "LFlag" });
        mDataBitsPerEntry += 8;

        mOffsets.emplace_back(Offset{ mDataBitsPerEntry, keySize, "key*" });
        mDataBitsPerEntry += schema.mKey.getByteCount() * 8;


        mSort.init(mPartyIdx, rows, keySize, ole);

        // in the forward direction we will permute the keys, a flag, 
         // and all of the select columns of the left table. In the 
         // backwards direction, we will unpermute the left table select
         // columns. Therefore, in total we will permute:
        u64 permForward = oc::divCeil(mDataBitsPerEntry, 8) + sizeof(u32);
        u64 permBackward = oc::divCeil(mDataBitsPerEntry - 1 - keySize, 8);

        mPerm.init(mPartyIdx, rows, permForward + permBackward, ole);


        mControlBitGmw.init(rows, getControlBitsCircuit(keySize), ole);

        mAggTree.init(rows, mDataBitsPerEntry, AggTreeType::Prefix, getDupCircuit(), ole);

        auto cir = *oc::BetaLibrary{}.int_int_bitwiseAnd(1, 1, 1);
        mUpdateActiveFlagGmw.init(rows, cir, ole);
    }

    // leftJoinCol should be unique
    macoro::task<> OmJoin::join(
        JoinQuery query,
        Table& out,
        PRNG& prng,
        coproto::Socket& sock)
    {
        MC_BEGIN(macoro::task<>, this, query, &out, &prng, &sock,
            keys = BinMatrix{},
            sPerm = AdditivePerm{},
            perm = ComposedPerm{},
            controlBits = BinMatrix{},
            data = BinMatrix{},
            temp = BinMatrix{},
            offsets_ = std::vector<Offset>{},
            prepro = macoro::eager_task<>{});

        setTimePoint("start");

        // left keys kL followed by the right keys kR
        keys = loadKeys(query.mLeftKey, query.mRightKey);
        setTimePoint("load");

        if (mInsecurePrint)
        {
            offsets_ = { Offset{0,keys.bitsPerEntry(), "key"} };
            MC_AWAIT(print(keys, controlBits, sock, mPartyIdx, "keys", offsets_));
        }

        mSort.preprocess();
        prepro = mSort.genPrePerm(sock, prng) | macoro::make_eager();

        // get the stable sorting permutation sPerm
        MC_AWAIT(mSort.genPerm(keys, sPerm, sock,prng));
        setTimePoint("sort");

        MC_AWAIT(sPerm.validate(sock));

        mPerm.preprocess();
        MC_AWAIT(prepro);

        // gather all of the columns from the left table and concatinate them
        // together. Append dummy rows after that. Then add the column of keys
        // to that. So it will look something like:
        //     L | kL | 1
        //     0 | kR | 0
        concatColumns(query.mLeftKey, query.mSelect, keys, data, mPartyIdx);
        setTimePoint("concat");
        keys.mData = {};


        if (mInsecurePrint)
            MC_AWAIT(print(data, controlBits, sock, mPartyIdx, "preSort", mOffsets));


        // Apply the sortin permutation. What you end up with are the keys
        // in sorted order and the rows of L also in sorted order.
        temp.resize(data.numEntries(), data.bitsPerEntry() + 8);
        temp.resize(data.numEntries(), data.bitsPerEntry());
        
        MC_AWAIT(mPerm.generate(sock, prng, data.rows(), perm));
        setTimePoint("perm cor gen");

        MC_AWAIT(perm.validate(sock));
        

        MC_AWAIT(perm.derandomize(sPerm, sock));
        setTimePoint("perm cor derand");

        MC_AWAIT(perm.apply<u8>(PermOp::Inverse, data, temp, sock));
        std::swap(data, temp);
        setTimePoint("applyInv-sort");

        if (mInsecurePrint)
            MC_AWAIT(print(data, controlBits, sock, mPartyIdx, "sort", mOffsets));

        // compare adjacent keys. controlBits[i] = 1 if k[i]==k[i-1].
        // put another way, controlBits[i] = 1 if keys[i] is from the
        // right table and has a matching key from the left table.
        MC_AWAIT(getControlBits(data, mOffsets[mOffsets.size()-1].mStart/8, keys.bitsPerEntry(), sock, controlBits, prng));
        setTimePoint("control");
        //std::cout << "controlBits done " << LOCATION << std::endl;

        if (mInsecurePrint)
            MC_AWAIT(print(data, controlBits, sock, mPartyIdx, "control", mOffsets));


        // reshape data so that the key at then end of each row are discarded.
        mOffsets.pop_back();
        data.reshape(mOffsets.back().mStart + mOffsets.back().mSize);
        temp.reshape(mOffsets.back().mStart + mOffsets.back().mSize);
        // duplicate the rows in data that are from L into any matching
        // rows that correspond to R.
        
        MC_AWAIT(mAggTree.apply(data, controlBits, sock, prng, temp));
        std::swap(data, temp);
        setTimePoint("duplicate");

        //std::cout << "AggTree done " << LOCATION << std::endl;

        if (mInsecurePrint)
            MC_AWAIT(print(data, controlBits, sock, mPartyIdx, "agg", mOffsets));


        MC_AWAIT(updateActiveFlag(data, controlBits, temp, mOffsets.back().mStart, prng, sock));
        std::swap(data, temp);
        //std::cout << "Active done " << LOCATION << std::endl;

        if (mInsecurePrint)
            MC_AWAIT(print(data, controlBits, sock, mPartyIdx, "isActive", mOffsets));


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
        //assert(data.bytesPerEntry() == bytesPermuted1);
        MC_AWAIT(perm.apply<u8>(PermOp::Regular, data, temp, sock));//, ole
        std::swap(data, temp);
        setTimePoint("apply-sort");

        //std::cout << "Perm::Apply inv done " << LOCATION << std::endl;


        if (mInsecurePrint)
            MC_AWAIT(print(data, controlBits, sock, mPartyIdx, "unsort", mOffsets));




        // unpack L' in `data` and write the result to `out`.
        getOutput(data, query.mSelect, query.mLeftKey, out, mOffsets);


        MC_END();
    }
}