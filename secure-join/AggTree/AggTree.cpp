#include "AggTree.h"


namespace secJoin
{

    //void validateShares(BinMatrix& shares, CommPkg& comm, oc::BitVector& mask, bool debug);
    //inline void randomize(TBinMatrix& d)
    //{
    //	static PRNG prng(oc::CCBlock);
    //	for (u64 j = 0; j < 2; ++j)
    //	{
    //		prng.get(
    //			d.mShares[j].data(),
    //			d.mShares[j].size());
    //	}
    //}

    //inline block hash(const TBinMatrix& d)
    //{
    //	oc::RandomOracle ro(sizeof(block));
    //	for (u64 j = 0; j < 2; ++j)
    //	{
    //		ro.Update(
    //			d.mShares[j].data(),
    //			d.mShares[j].size());
    //	}

    //	block mR;
    //	ro.Final(mR);
    //	return mR;
    //}

    //inline std::string hexx(const TBinMatrix& d)
    //{
    //	std::stringstream ss;
    //	for (u64 j = 0; j < d.bitsPerEntry(); ++j)
    //	{
    //		ss << j << ".0." << hex(d.mShares[0][j]) << std::endl;
    //		ss << j << ".1." << hex(d.mShares[1][j]) << std::endl;
    //	}

    //	return ss.str();
    //}
    //inline block hash(const BinMatrix& d)
    //{
    //	oc::RandomOracle ro(sizeof(block));
    //	for (u64 j = 0; j < 2; ++j)
    //	{
    //		ro.Update(
    //			d.mShares[j].data(),
    //			d.mShares[j].size());
    //	}

    //	block mR;
    //	ro.Final(mR);
    //	return mR;
    //}

    inline void perfectUnshuffle(
        TBinMatrix& in,
        TBinMatrix& out0,
        TBinMatrix& out1,
        u64 dstShift,
        u64 numEntries)
    {
        if (dstShift % 8)
            throw RTE_LOC;
        if (numEntries == 0 || in.numEntries() == 0)
            numEntries = in.numEntries();

        if (numEntries == 0)
            return;

        auto dstShift2 = dstShift / 2;
        if (dstShift2 > out0.numEntries())
            throw RTE_LOC;
        if (dstShift2 > out1.numEntries())
            throw RTE_LOC;
        //auto ss = in.() * sizeof(*in.mShares[0].data()) * 8;
        if (numEntries > in.numEntries())
            throw RTE_LOC;
        if (numEntries != out0.numEntries() + out1.numEntries() - dstShift)
            throw RTE_LOC;

        for (u64 i = 0; i < in.bitsPerEntry(); ++i)
        {
            //for (u64 j = 0; j < 2; ++j)
            {
                auto ss = oc::divCeil(out0.numEntries() - dstShift2, 8);
                auto inn = in[i].subspan(0, oc::divCeil(numEntries, 8));
                auto o0 = out0[i].subspan(dstShift2 / 8, ss);
                auto o1 = out1[i].subspan(dstShift2 / 8, ss);
                assert(inn.data() + inn.size() <= (u8*)(in[i].data() + in[i].size()));
                assert(o0.data() + o0.size() <= (u8*)(out0[i].data() + out0[i].size()));
                assert(o1.data() + o1.size() <= (u8*)(out1[i].data() + out1[i].size()));

                if (dstShift2)
                {
                    memset(out0[i].data(), 0, dstShift2 / 8);
                    memset(out1[i].data(), 0, dstShift2 / 8);
                }

                perfectUnshuffle(inn, o0, o1);


                assert(inn.data() + inn.size() <= (u8*)(in[i].data() + in[i].size()));
            }
        }
    }


    inline void perfectUnshuffle(TBinMatrix& in, TBinMatrix& out0, TBinMatrix& out1)
    {
        auto numEntries = in.numEntries();
        if (numEntries == 0)
            return;

        for (u64 i = 0; i < in.bitsPerEntry(); ++i)
        {
            {
                auto inn = in[i].subspan(0, oc::divCeil(numEntries, 8));
                auto o0 = out0[i].subspan(0, oc::divCeil(numEntries / 2, 8));
                auto o1 = out1[i].subspan(0, oc::divCeil(numEntries / 2, 8));
                assert(inn.data() + inn.size() <= (u8*)(in[i].data() + in[i].size()));
                assert(o0.data() + o0.size() <= (u8*)(out0[i].data() + out0[i].size()));
                assert(o1.data() + o1.size() <= (u8*)(out1[i].data() + out1[i].size()));

                perfectUnshuffle(inn, o0, o1);
                assert(inn.data() + inn.size() <= (u8*)(in[i].data() + in[i].size()));
            }
        }
    }


    inline void perfectShuffle(TBinMatrix& in0, TBinMatrix& in1, TBinMatrix& out)
    {
        auto size = out.numEntries();
        auto inSize = size / 2;
        if (in0.bitsPerEntry() != in1.bitsPerEntry())
            throw RTE_LOC;
        if (out.bitsPerEntry() != in1.bitsPerEntry())
            throw RTE_LOC;
        if (in0.numEntries() != inSize)
            throw RTE_LOC;
        if (in1.numEntries() != inSize)
            throw RTE_LOC;

        for (u64 i = 0; i < in0.bitsPerEntry(); ++i)
        {
            //for (u64 j = 0; j < 2; ++j)
            {
                auto inn0 = span<u8>((u8*)in0[i].data(), oc::divCeil(inSize, 8));
                auto inn1 = span<u8>((u8*)in1[i].data(), oc::divCeil(inSize, 8));
                auto oo = span<u8>((u8*)out[i].data(), oc::divCeil(size, 8));

                assert(oo.data() + oo.size() <= (u8*)(out[i].data() + out[i].size()));
                assert(inn0.data() + inn0.size() <= (u8*)(in0[i].data() + in0[i].size()));
                assert(inn1.data() + inn1.size() <= (u8*)(in1[i].data() + in1[i].size()));
                perfectShuffle(inn0, inn1, oo);
            }
        }
    }


    //void AggTree::toPackedBin(const BinMatrix& in, TBinMatrix& dest,
    //    u64 srcRowStartIdx,
    //    u64 numRows)
    //{
    //    if (numRows > dest.numEntries())
    //        throw RTE_LOC;
    //    if (in.bitsPerEntry() != dest.bitsPerEntry())
    //        throw RTE_LOC;
    //    if (numRows + srcRowStartIdx > in.numEntries())
    //        throw RTE_LOC;

    //    //dest.reset(numRows, in.bitsPerEntry());

    //    if (in.bitsPerEntry() == 1)
    //        std::cout << "optimize me" << std::endl;


    //    auto& s = in;
    //    auto& d = dest;

    //    oc::MatrixView<u8> inView(
    //        (u8*)s.data() + srcRowStartIdx * s.bytesPerEntry(),
    //        (u8*)s.data() + (srcRowStartIdx + numRows) * s.bytesPerEntry(),
    //        s.bytesPerEntry());

    //    assert(inView.data() + inView.size() <= s.data() + s.size());

    //    oc::MatrixView<u8> memView(
    //        d.data(),
    //        d.data() + d.size(),
    //        d.bytesPerRow());

    //    assert(memView.data() + memView.size() <= d.data() + d.size());

    //    oc::transpose(inView, memView);
    //}

    oc::BetaCircuit AggTree::upstreamCir(
        u64 bitsPerEntry,
        Type type,
        const Operator& add)
    {
        oc::BetaCircuit cir;

        oc::BetaBundle
            leftPreVal(bitsPerEntry),
            leftSufVal(bitsPerEntry),
            rghtPreVal(bitsPerEntry),
            rghtSufVal(bitsPerEntry),
            leftPreBit(1),
            leftSufBit(1),
            rghtPreBit(1),
            rghtSufBit(1),
            prntPreVal(bitsPerEntry),
            prntSufVal(bitsPerEntry),
            prntPreBit(1),
            prntSufBit(1),
            temp1(bitsPerEntry), temp2(bitsPerEntry);

        if (type & Type::Prefix)
        {
            cir.addInputBundle(leftPreVal);
            cir.addInputBundle(rghtPreVal);
            cir.addInputBundle(leftPreBit);
            cir.addInputBundle(rghtPreBit);
        }
        if (type & Type::Suffix)
        {
            cir.addInputBundle(leftSufVal);
            cir.addInputBundle(rghtSufVal);
            cir.addInputBundle(leftSufBit);
            cir.addInputBundle(rghtSufBit);
        }
        if (type & Type::Prefix)
        {
            cir.addOutputBundle(prntPreVal);
            cir.addOutputBundle(prntPreBit);
        }
        if (type & Type::Suffix)
        {
            cir.addOutputBundle(prntSufVal);
            cir.addOutputBundle(prntSufBit);
        }

        cir.addTempWireBundle(temp1);
        cir.addTempWireBundle(temp2);


        // Apply the computation.
        oc::BetaLibrary lib;

        if (type & Type::Prefix)
        {

            for (int w : leftPreVal)
            {
                auto flag = cir.mWireFlags[w];
                if (flag == oc::BetaWireFlag::Uninitialized)
                    throw RTE_LOC;
            }
            for (int w : rghtPreVal)
            {
                auto flag = cir.mWireFlags[w];
                if (flag == oc::BetaWireFlag::Uninitialized)
                    throw RTE_LOC;
            }

            add(cir, leftPreVal, rghtPreVal, temp1);

            cir << "B0    " << leftPreVal << "\n";
            cir << "B1    " << rghtPreVal << "\n";
            cir << "B0+B1 " << temp1 << "\n";
            cir << "P1    " << rghtPreBit << "\n";
            //cir << "temp  " << std::to_string(temp1[0]) << " .. " << std::to_string(temp1.back()) << "\mN16";

            lib.multiplex_build(cir, temp1, rghtPreVal, rghtPreBit, prntPreVal, temp2);
            cir.addGate(leftPreBit[0], rghtPreBit[0], oc::GateType::And, prntPreBit[0]);

            cir << "B     " << prntPreVal << "\n\n";

        }

        if (type & Type::Suffix)
        {
            add(cir, leftSufVal, rghtSufVal, temp1);

            cir << "UP ****\n";
            cir << "B0    " << leftSufVal << "\n";
            cir << "B1    " << rghtSufVal << "\n";
            cir << "B0+B1 " << temp1 << "\n";
            cir << "P0    " << leftSufBit << "\n";


            lib.multiplex_build(cir, temp1, leftSufVal, leftSufBit, prntSufVal, temp2);
            cir.addGate(leftSufBit[0], rghtSufBit[0], oc::GateType::And, prntSufBit[0]);

            cir << "B     " << prntSufVal << "\n";
        }

        return cir;
    }


    macoro::task<> AggTree::upstream(
        const BinMatrix& src,
        const BinMatrix& controlBits,
        coproto::Socket& comm,
        PRNG& prng,
        Level& root,
        span<SplitLevel> levels)
    {
        MC_BEGIN(macoro::task<>, this, &src, &controlBits, comm, &root, levels,&prng,
            lvl = u64{}
        );
        if (src.numEntries() != controlBits.numEntries())
            throw RTE_LOC;

        // load the values of the leafs. Its possible that we need to split
        // these values across two levels of the tree (non-power of 2 input lengths).
        levels[0].resize(mN16, mBitsPerEntry, mType);
        levels[0].setLeafVals(src, controlBits, 0, 0);

        if (mCompleteTree == false)
        {
            // split the leaf values across two levels of the tree.
            levels[1].copy(levels[0], mR*2, mR, mLevelSizes[1]);
            levels[0].reshape(mLevelSizes[0]);
        }
        // we start at the preSuf and move up.
        for (lvl = 0; lvl < mUpGmw.size(); ++lvl)
        {

            {
                auto& children = levels[lvl];
                auto& parent = root;
                u64 inIdx = 0, outIdx = 0;
                if (mType & Type::Prefix)
                {
                    parent.mPreVal.resize(mLevelSizes[lvl]/2, mBitsPerEntry, 2 * sizeof(block));
                    parent.mPreBit.resize(mLevelSizes[lvl]/2, 1, 2 * sizeof(block));
                    mUpGmw[lvl].mapInput(inIdx++, children[0].mPreVal);
                    mUpGmw[lvl].mapInput(inIdx++, children[1].mPreVal);
                    mUpGmw[lvl].mapInput(inIdx++, children[0].mPreBit);
                    mUpGmw[lvl].mapInput(inIdx++, children[1].mPreBit);
                    mUpGmw[lvl].mapOutput(outIdx++, parent.mPreVal);
                    mUpGmw[lvl].mapOutput(outIdx++, parent.mPreBit);
                }
                if (mType & Type::Suffix)
                {
                    parent.mSufVal.resize(mLevelSizes[lvl] / 2, mBitsPerEntry, 2 * sizeof(block));
                    parent.mSufBit.resize(mLevelSizes[lvl] / 2, 1, 2 * sizeof(block));
                    mUpGmw[lvl].mapInput(inIdx++, children[0].mSufVal);
                    mUpGmw[lvl].mapInput(inIdx++, children[1].mSufVal);
                    mUpGmw[lvl].mapInput(inIdx++, children[0].mSufBit);
                    mUpGmw[lvl].mapInput(inIdx++, children[1].mSufBit);
                    mUpGmw[lvl].mapOutput(outIdx++, parent.mSufVal);
                    mUpGmw[lvl].mapOutput(outIdx++, parent.mSufBit);
                }
            }

            // eval
            MC_AWAIT(mUpGmw[lvl].run(comm));
            mUpGmw[lvl] = {};

            if (lvl != mUpGmw.size() - 1)
            {
                auto& parent = root;
                auto& splitParent = levels[lvl + 1];
                splitParent[0].resize(mLevelSizes[lvl+1] / 2, mBitsPerEntry, mType);
                splitParent[1].resize(mLevelSizes[lvl+1] / 2, mBitsPerEntry, mType);

                if (mType & Type::Prefix)
                {
                    perfectUnshuffle(parent.mPreVal, splitParent[0].mPreVal, splitParent[1].mPreVal);
                    perfectUnshuffle(parent.mPreBit, splitParent[0].mPreBit, splitParent[1].mPreBit);
                    assert(splitParent[0].mPreBit.numEntries());
                    assert(splitParent[1].mPreBit.numEntries());
                    assert(splitParent[0].mPreVal.numEntries());
                    assert(splitParent[1].mPreVal.numEntries());

                }

                if (mType & Type::Suffix)
                {
                    perfectUnshuffle(parent.mSufVal, splitParent[0].mSufVal, splitParent[1].mSufVal);
                    perfectUnshuffle(parent.mSufBit, splitParent[0].mSufBit, splitParent[1].mSufBit);
                    assert(splitParent[0].mSufBit.numEntries());
                    assert(splitParent[1].mSufBit.numEntries());
                    assert(splitParent[0].mSufVal.numEntries());
                    assert(splitParent[1].mSufVal.numEntries());
                }

            }
        }



        MC_END();
    }


    oc::BetaCircuit AggTree::downstreamCir(u64 bitsPerEntry, const Operator& op, Type type)
    {
        oc::BetaCircuit cir;

        //auto inSize = TreeRecord::recordBitCount(bitsPerEntry, type);
        oc::BetaBundle //in(inSize), inC(2 * inSize), out(2 * inSize),
            temp1(bitsPerEntry), temp2(bitsPerEntry);
        using namespace oc;

        BetaBundle preLeftVal(bitsPerEntry);
        BetaBundle sufRghtVal(bitsPerEntry);

        BetaBundle preLeftVal_out(bitsPerEntry);
        BetaBundle preRghtVal_out(bitsPerEntry);
        BetaBundle sufLeftVal_out(bitsPerEntry);
        BetaBundle sufRghtVal_out(bitsPerEntry);

        BetaBundle preLeftBit(1);
        BetaBundle sufRghtBit(1);

        BetaBundle prePrntVal(bitsPerEntry);
        BetaBundle sufPrntVal(bitsPerEntry);

        if (type & Type::Prefix)
        {
            cir.addInputBundle(preLeftVal);
            cir.addInputBundle(preLeftBit);
            cir.addInputBundle(prePrntVal);
        }

        if (type & Type::Suffix)
        {
            cir.addInputBundle(sufRghtVal);
            cir.addInputBundle(sufRghtBit);
            cir.addInputBundle(sufPrntVal);
        }

        if (type & Type::Prefix)
        {
            cir.addOutputBundle(preLeftVal_out);
            cir.addOutputBundle(preRghtVal_out);
        }
        if (type & Type::Suffix)
        {
            cir.addOutputBundle(sufLeftVal_out);
            cir.addOutputBundle(sufRghtVal_out);
        }


        // out is both input and output...
        //cir.mOutputs.push_back(out);
        cir.addTempWireBundle(temp1);
        cir.addTempWireBundle(temp2);


        oc::BetaLibrary lib;

        if (type & Type::Prefix)
        {
            cir << "down---  \n";
            cir << "B0    " << preLeftVal << " " << std::to_string(preLeftVal[0]) << " .. " << std::to_string(preLeftVal.back()) << "\n";
            //cir << "B1    " << preRghtVal << "\mN16";
            cir << "B     " << prePrntVal << "\n";
            cir << "P0    " << preLeftBit << "\n";

            op(cir, prePrntVal, preLeftVal, temp1);
            {
                auto& ifFalse = preLeftVal;
                auto& ifTrue = temp1;
                auto& cd = cir;
                auto& choice = preLeftBit; //leftIn.mPreProd;
                auto& out = preRghtVal_out; //rightOut.mPrefix;
                //lib.multiplex_build(cir, temp1, left.mPrefix, left.mPreProd, right.mPrefix, t2);
                for (u64 i = 0; i < out.mWires.size(); ++i)
                    cd.addGate(ifFalse.mWires[i], ifTrue.mWires[i], oc::GateType::Xor, temp2.mWires[i]);
                //cd.addPrint("a^preBit  [" + std::to_string(i) + "] = ");
                //cd.addPrint(temp.mWires[0]);
                //cd.addPrint("\mN16");

                for (u64 i = 0; i < out.mWires.size(); ++i)
                    cd.addGate(temp2.mWires[i], choice.mWires[0], oc::GateType::And, temp1.mWires[i]);

                //cd.addPrint("a^preBit&sufVal[" + std::to_string(i) + "] = ");
                //cd.addPrint(temp.mWires[0]);
                //cd.addPrint("\mN16");

                for (u64 i = 0; i < out.mWires.size(); ++i)
                    cd.addGate(ifFalse.mWires[i], temp1.mWires[i], oc::GateType::Xor, out.mWires[i]);
            }

            cir.addCopy(prePrntVal, preLeftVal_out);

            cir << "B0*   " << preLeftVal_out << "\n";
            cir << "B1*   " << preRghtVal_out << "\n";

        }
        if (type & Type::Suffix)
        {


            cir << "down suf---  \n";
            //cir << "B0    " << leftIn.mSuffix << "\mN16";
            //cir << "temp  " <<  << "\mN16";
            cir << "B1    " << sufRghtVal << "\n";
            cir << "B     " << sufPrntVal << "\n";
            cir << "P1    " << sufRghtBit << "\n";
            op(cir, sufRghtVal, sufPrntVal, temp1);
            //lib.multiplex_build(cir, temp1, right.mSuffix, right.mSufProd, left.mSuffix, temp2);
            {
                auto& ifFalse = sufRghtVal;
                auto& ifTrue = temp1;
                auto& cd = cir;
                auto& choice = sufRghtBit;
                auto& out = sufLeftVal_out;
                //lib.multiplex_build(cir, temp1, left.mPrefix, left.mPreProd, right.mPrefix, t2);
                for (u64 i = 0; i < out.mWires.size(); ++i)
                    cd.addGate(ifFalse.mWires[i], ifTrue.mWires[i], oc::GateType::Xor, temp2.mWires[i]);
                //cd.addPrint("a^preBit  [" + std::to_string(i) + "] = ");
                //cd.addPrint(temp.mWires[0]);
                //cd.addPrint("\mN16");

                for (u64 i = 0; i < out.mWires.size(); ++i)
                    cd.addGate(temp2.mWires[i], choice.mWires[0], oc::GateType::And, temp1.mWires[i]);

                //cd.addPrint("a^preBit&sufVal[" + std::to_string(i) + "] = ");
                //cd.addPrint(temp.mWires[0]);
                //cd.addPrint("\mN16");

                for (u64 i = 0; i < out.mWires.size(); ++i)
                    cd.addGate(ifFalse.mWires[i], temp1.mWires[i], oc::GateType::Xor, out.mWires[i]);
            }
            cir.addCopy(sufPrntVal, sufRghtVal_out);


            cir << "B0*   " << sufLeftVal_out << "\n";
            cir << "B1*   " << sufRghtVal_out << "\n";
        }

        return cir;
    }



    // apply the downstream circuit to each level of the tree.
    macoro::task<> AggTree::downstream(
        const BinMatrix& src,
        Level& root,
        span<SplitLevel> levels,
        SplitLevel& newVals,
        coproto::Socket& comm,
        PRNG& prng,
        std::vector<SplitLevel>* debugLevels)
    {
        MC_BEGIN(macoro::task<>, this, &src, &root, levels, &newVals, &comm, debugLevels, &prng,
            pLvl = u64{},
            cLvl = u64{},
            temp = SplitLevel{}
        );


        if(debugLevels)
            debugLevels->resize(levels.size());


        // this will hold the downstream intermediate levels.
        temp[0].resize(std::max<u64>(mLevelSizes[0], mLevelSizes[1]), mBitsPerEntry, mType);
        temp[1].resize(std::max<u64>(mLevelSizes[0], mLevelSizes[1]), mBitsPerEntry, mType);


        // we start at the root and move down. We store intermidate 
        // levels in `temp`. levels is only read from. Once the children 
        // are computed, we combine them and write the result to `root`
        for (pLvl = mDownGmw.size(); pLvl != 0; --pLvl)
        {
            // how many parents are here at this level.
            // For the last level this might not be a power of 2.
            cLvl = pLvl - 1;

            {

                auto& parent = root;
                auto& children = levels[cLvl];


                u64 inIdx = 0, outIdx = 0;
                if (mType & Type::Prefix)
                {
                    // prefix only takes the left child and parent as input
                    mDownGmw[cLvl].mapInput(inIdx++, children[0].mPreVal);
                    mDownGmw[cLvl].mapInput(inIdx++, children[0].mPreBit);
                    mDownGmw[cLvl].mapInput(inIdx++, parent.mPreVal);

                    // set number of shares we have per wire.
                    temp[0].mPreVal.reshape(mLevelSizes[cLvl] / 2);
                    temp[1].mPreVal.reshape(mLevelSizes[cLvl] / 2);

                    // left and right child output
                    mDownGmw[cLvl].mapOutput(outIdx++, temp[0].mPreVal);
                    mDownGmw[cLvl].mapOutput(outIdx++, temp[1].mPreVal);
                }

                if (mType & Type::Suffix)
                {
                    // prefix only takes the right child and parent as input
                    mDownGmw[cLvl].mapInput(inIdx++, children[1].mSufVal);
                    mDownGmw[cLvl].mapInput(inIdx++, children[1].mSufBit);
                    mDownGmw[cLvl].mapInput(inIdx++, parent.mSufVal);

                    // set number of shares we have per wire.
                    temp[0].mSufVal.reshape(mLevelSizes[cLvl] / 2);
                    temp[1].mSufVal.reshape(mLevelSizes[cLvl] / 2);

                    // left and right child output
                    mDownGmw[cLvl].mapOutput(outIdx++, temp[0].mSufVal);
                    mDownGmw[cLvl].mapOutput(outIdx++, temp[1].mSufVal);
                }
            }

            // eval
            MC_AWAIT(mDownGmw[cLvl].run(comm));
            mDownGmw[cLvl] = {};

            // for unit testing, we want to save these intermediate values.
            if (debugLevels)
            {
                (*debugLevels)[cLvl][0].mPreVal = temp[0].mPreVal;
                (*debugLevels)[cLvl][1].mPreVal = temp[1].mPreVal;
                (*debugLevels)[cLvl][0].mSufVal = temp[0].mSufVal;
                (*debugLevels)[cLvl][1].mSufVal = temp[1].mSufVal;
            }

            // if we arent on the final level, we need to re-order
            // the children. Currently we have all the left children
            // and all the right children in separate lists. We need
            // to merge these two lists together so that the children 
            // are next to each other. This is done using an algorithm
            // called 'perfectShuffle'.
            if (cLvl)
            {
                // where we will store the merged children (ie the next set of parents).
                auto& nextParent = root;
                auto& nextChildren = levels[cLvl - 1];
                if (mType & Type::Prefix)
                {
                    nextParent.mPreVal.resize(nextChildren[0].mPreVal.numEntries(), mBitsPerEntry, sizeof(block));

                    auto old = temp[0].mPreVal.numEntries();

                    // For the second to last level we have a special case
                    // where some of the current level children are not parents
                    // for the next level. We dont want to merge these so we will
                    // skip them by calling reshape.
                    temp[0].mPreVal.reshape(nextParent.mPreVal.numEntries() / 2);
                    temp[1].mPreVal.reshape(nextParent.mPreVal.numEntries() / 2);

                    // merge the left and right children together.
                    perfectShuffle(temp[0].mPreVal, temp[1].mPreVal, nextParent.mPreVal);

                    temp[0].mPreVal.reshape(old);
                    temp[1].mPreVal.reshape(old);
                }

                if (mType & Type::Suffix)
                {
                    nextParent.mSufVal.resize(nextChildren[0].mSufVal.numEntries(), mBitsPerEntry, sizeof(block));

                    auto old = temp[0].mSufVal.numEntries();

                    // For the second to last level we have a special case
                    // where some of the current level children are not parents
                    // for the next level. We dont want to merge these so we will
                    // skip them by calling reshape.
                    temp[0].mSufVal.reshape(nextParent.mSufVal.numEntries() / 2);
                    temp[1].mSufVal.reshape(nextParent.mSufVal.numEntries() / 2);

                    // merge the left and right children together.
                    perfectShuffle(temp[0].mSufVal, temp[1].mSufVal, nextParent.mSufVal);

                    temp[0].mSufVal.reshape(old);
                    temp[1].mSufVal.reshape(old);
                }
            }

            // if we are on a leaf level, then we need to copy the values out.
            auto copyBytes = [](TBinMatrix& src, TBinMatrix& dst, u64 srcStart, u64 dstStart, u64 size)
            {
                auto bitsPerEntry = src.bitsPerEntry();
                assert(dst.bitsPerEntry() == bitsPerEntry);
                for (u64 k = 0; k < bitsPerEntry; ++k)
                {
                    for (u64 j = 0; j < 2; ++j)
                    {
                        auto s0 = ((u8*)src[k].data()) + srcStart;
                        auto d0 = ((u8*)dst[k].data()) + dstStart;
                        //auto max = src[k].size() * sizeof(*src[k].data());
                        auto es = (u8*)(src[k].data() + src[k].size());
                        auto ed = (u8*)(dst[k].data() + dst[k].size());
                        //assert(dstStart + size < max);
                        assert(d0 + size <= ed);
                        assert(s0 + size <= es);
                        memcpy(d0, s0, size);
                    }
                }
            };

            bool firstPartial = mCompleteTree == false && cLvl == 1;
            bool secondPartial = mCompleteTree == false && cLvl == 0;
            bool full = mCompleteTree && cLvl == 0;

            if (firstPartial)
            {
                newVals[0].resize(mN16 / 2, mBitsPerEntry, mType);
                newVals[1].resize(mN16 / 2, mBitsPerEntry, mType);
            }

            // if we have a partial level, then we need to copy 
            // the initial leaf values to vals that are on the 
            // second to last level. We will effectively do the
            // same for preSuf by shifting the bytes down.
            for (u64 j = 0; j < 2 * firstPartial; ++j)
            {
                if (mType & Type::Prefix)
                {
                    // shift the preSuf values down.
                    copyBytes(temp[j].mPreVal, newVals[j].mPreVal, mR / 16, mLevelSizes[0] / 16, mLevelSizes[1] / 16);
                }

                if (mType & Type::Suffix)
                {
                    // shift the preSuf values down.                     
                    //shiftBytes(preSuf[j].mSufVal, mR/16, mN0/16, mN1/16);   
                    copyBytes(temp[j].mSufVal, newVals[j].mSufVal, mR / 16, mLevelSizes[0] / 16, mLevelSizes[1] / 16);
                }
            }

            // copy the leaf values that are on the last (partial) level
            for (u64 j = 0; j < 2 * secondPartial; ++j)
            {
                if (mType & Type::Prefix)
                {
                    copyBytes(temp[j].mPreVal, newVals[j].mPreVal, 0, 0, mLevelSizes[0] / 16);
                }

                if (mType & Type::Suffix)
                {
                    copyBytes(temp[j].mSufVal, newVals[j].mSufVal, 0, 0, mLevelSizes[0] / 16);
                }
            }

            if (full)
            {
                newVals = std::move(temp);
            }
        }

        levels[0].reshape(mN16);
        MC_END();
    }


    oc::BetaCircuit AggTree::leafCircuit(u64 bitsPerEntry, const Operator& op, Type type)
    {
        oc::BetaLibrary lib;

        oc::BetaCircuit cir;
        oc::BetaBundle
            leftVal(bitsPerEntry),
            leftPreVal(bitsPerEntry),
            leftSufVal(bitsPerEntry),
            leftPreBit(1),
            leftSufBit(1),
            leftOut(bitsPerEntry),

            rghtVal(bitsPerEntry),
            rghtPreVal(bitsPerEntry),
            rghtSufVal(bitsPerEntry),
            rghtPreBit(1),
            rghtSufBit(1),
            rghtOut(bitsPerEntry),

            lt1(bitsPerEntry),
            lt2(bitsPerEntry),
            lt3(bitsPerEntry),

            rt1(bitsPerEntry),
            rt2(bitsPerEntry),
            rt3(bitsPerEntry);

        cir.addInputBundle(leftVal);
        cir.addInputBundle(rghtVal);
        if (type & Type::Prefix)
        {
            cir.addInputBundle(leftPreVal);
            cir.addInputBundle(rghtPreVal);
            cir.addInputBundle(leftPreBit);
            cir.addInputBundle(rghtPreBit);
        }
        if (type & Type::Suffix)
        {
            cir.addInputBundle(leftSufVal);
            cir.addInputBundle(rghtSufVal);
            cir.addInputBundle(leftSufBit);
            cir.addInputBundle(rghtSufBit);
        }

        cir.addOutputBundle(leftOut);
        cir.addOutputBundle(rghtOut);

        cir.addTempWireBundle(lt1);
        cir.addTempWireBundle(lt2);
        cir.addTempWireBundle(lt3);
        cir.addTempWireBundle(rt1);
        cir.addTempWireBundle(rt2);
        cir.addTempWireBundle(rt3);

        switch (type)
        {
        case AggTreeType::Prefix:
            op(cir, leftPreVal, leftVal, lt1);
            op(cir, rghtPreVal, rghtVal, rt1);
            lib.multiplex_build(cir, lt1, leftVal, leftPreBit, leftOut, lt2);
            lib.multiplex_build(cir, rt1, rghtVal, rghtPreBit, rghtOut, rt2);

            //cir << "\ncir " << leftVal << " " << leftPreVal << " " << leftPreBit << " -> " << leftOut << "\mN16";
            break;
        case AggTreeType::Suffix:
            op(cir, leftVal, leftSufVal, lt1);
            op(cir, rghtVal, rghtSufVal, rt1);
            lib.multiplex_build(cir, lt1, leftVal, leftSufBit, leftOut, lt2);
            lib.multiplex_build(cir, rt1, rghtVal, rghtSufBit, rghtOut, rt2);
            break;
        case AggTreeType::Full:
            // t1 = preVal + val;
            op(cir, leftPreVal, leftVal, lt1);
            op(cir, rghtPreVal, rghtVal, rt1);

            // t3 = preBit ? preVal + val : val;
            lib.multiplex_build(cir, lt1, leftVal, leftPreBit, lt3, lt2);
            lib.multiplex_build(cir, rt1, rghtVal, rghtPreBit, rt3, rt2);

            // t1 = t3 + sufVal;
            op(cir, lt3, leftSufVal, lt1);
            op(cir, rt3, rghtSufVal, rt1);

            // out = sufBit ? t3 + sufVal : t3;
            //     = preVal + val + sufVal    ~ preBit=1,sufBit=1
            //     = preVal + val             ~ preBit=1,sufBit=0
            //     =          val + sufVal    ~ preBit=0,sufBit=1
            //     =          val             ~ preBit=0,sufBit=0
            lib.multiplex_build(cir, lt1, lt3, leftSufBit, leftOut, lt2);
            lib.multiplex_build(cir, rt1, rt3, rghtSufBit, rghtOut, rt2);

            break;
        default:
            throw RTE_LOC;
            break;
        }

        return cir;
    }

    //	// apply the downstream circuit to each level of the tree.
    macoro::task<> AggTree::computeLeaf(
        SplitLevel& leaves,
        SplitLevel& preSuf,
        BinMatrix& dst,
        PRNG& prng,
        coproto::Socket& comm)
    {

        MC_BEGIN(macoro::task<>, this, &leaves, &preSuf, &dst, &comm, &prng
        );

        //bitsPerEntry = (type & Type::Prefix) ? leaves[0].mPreVal.bitsPerEntry() : leaves[0].mSufVal.bitsPerEntry();
        //size = (type & Type::Prefix) ? leaves[0].mPreBit.numEntries() : leaves[0].mSufBit.numEntries();



        {

            //bin.init(size, cir, gen);

            int inIdx = 0;
            // the input values v for each leaf node.
            if (mType & Type::Prefix)
            {
                mLeafGmw.mapInput(inIdx++, leaves[0].mPreVal);
                mLeafGmw.mapInput(inIdx++, leaves[1].mPreVal);
            }
            else
            {
                mLeafGmw.mapInput(inIdx++, leaves[0].mSufVal);
                mLeafGmw.mapInput(inIdx++, leaves[1].mSufVal);
            }

            if (mType & Type::Prefix)
            {
                // prefix val
                mLeafGmw.mapInput(inIdx++, preSuf[0].mPreVal);
                mLeafGmw.mapInput(inIdx++, preSuf[1].mPreVal);

                // prefix bit
                mLeafGmw.mapInput(inIdx++, leaves[0].mPreBit);
                mLeafGmw.mapInput(inIdx++, leaves[1].mPreBit);

            }

            if (mType & Type::Suffix)
            {
                // prefix val
                mLeafGmw.mapInput(inIdx++, preSuf[0].mSufVal);
                mLeafGmw.mapInput(inIdx++, preSuf[1].mSufVal);

                // prefix bit					 
                mLeafGmw.mapInput(inIdx++, leaves[0].mSufBit);
                mLeafGmw.mapInput(inIdx++, leaves[1].mSufBit);
            }
        }

        MC_AWAIT(mLeafGmw.run(comm));

        {
            BinMatrix leftOut(mLeafGmw.mN, mBitsPerEntry), rghtOut(mLeafGmw.mN, mBitsPerEntry);
            mLeafGmw.getOutput(0, leftOut);
            mLeafGmw.getOutput(1, rghtOut);

            auto d = dst.data();
            auto ds = dst.bytesPerEntry();
            auto l = leftOut.data();
            auto r = rghtOut.data();
            auto ls = leftOut.bytesPerEntry();
            auto rs = rghtOut.bytesPerEntry();
            assert(ds >= ls && ds >= rs);

            auto n2 = mN / 2;
            for (u64 i = 0; i < n2; ++i)
            {
                assert(d + ds <= dst.data() + dst.size());
                assert(l + ls <= leftOut.data() + leftOut.size());
                assert(r + rs <= rghtOut.data() + rghtOut.size());

                memcpy(d, l, ls); d += ds; l += ls;
                memcpy(d, r, rs); d += ds; r += rs;
            }

            if (mN & 1)
            {
                memcpy(d, l, ls);
            }
        }

        MC_END();
    }


}