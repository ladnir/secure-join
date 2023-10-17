#pragma once
#include "cryptoTools/Circuit/BetaCircuit.h"
#include "cryptoTools/Circuit/BetaLibrary.h"
#include "cryptoTools/Common/Log.h"
#include "cryptoTools/Crypto/PRNG.h"
#include <iomanip>
#include "PerfectShuffle.h"
#include <functional>
#include "Level.h"
#include "libOTe/Tools/Tools.h"
#include "coproto/Socket/Socket.h"
#include "secure-join/CorGenerator/CorGenerator.h"
#include "secure-join/GMW/Gmw.h"

//#include "reveal.h"

namespace secJoin
{
    // This class implements a prefix, suffix or full aggregation tree, 
    // elements an input vector based on a bit vector.
    // Given an input vector x=(x1,x2,...,xn) and bit vector preBit=(0,sufBit,...,bn),
    // 
    // A 'block' is defined as {i,i+1...,j} where 𝑏_𝑖,𝑏_{𝑖+1},…, 𝑏_𝑗=(0,1,1,…,1)
    // and 𝑏_{𝑗+1}=0.
    // 
    // The apply(x,preBit,...) function will output the vector y=(y1,y2,...,yn)
    // 
    // prefix: for each block {i,...,j}
    //		𝑦_𝑖  = 𝑣_𝑖 
    //      𝑦_𝑖′ = 𝑦_𝑖′ ⊕ 𝑣_{𝑖′−1}      :𝑖′∈{𝑖+1,…,𝑗}
    //
    // suffix: for each block {i,...,j}
    //     𝑦_𝑗  = 𝑣_𝑗
    //     𝑦_𝑗′ = 𝑣_𝑗′ ⊕ 𝑦_{𝑗′+1}      :𝑗^′∈{𝑗−1,…,𝑖}
    // 
    // Full: for each block {i,...,j}
    //	   𝑦_𝑖′ = 𝑣_𝑖 ⊕ … ⊕ 𝑣_𝑗      :𝑖^′∈{𝑖,…,𝑗}
    // 
    // 
    class AggTree : public AggTreeParam
    {
    public:
        using Type = AggTreeType;

        // The + function. Takes as input two values
        // and output the addition of them.
        using Operator = std::function<
            void(
                oc::BetaCircuit& c,
                const oc::BetaBundle& left,
                const oc::BetaBundle& right,
                oc::BetaBundle& out)>;

        using Level = AggTreeLevel;
        using SplitLevel = AggTreeSplitLevel;


        macoro::task<> apply(
            const BinMatrix& src,
            const BinMatrix& controlBits,
            const Operator& op,
            Type type,
            coproto::Socket& comm,
            CorGenerator& gen,
            PRNG& prng,
            BinMatrix& dst)
        {
            MC_BEGIN(macoro::task<>, this, &src, &controlBits, &op, type, &comm, &gen, &dst, &prng,
                root = Level{},
                upLevels = std::vector<SplitLevel>{},
                newVals = SplitLevel{}
            );

            computeTreeSizes(src.numEntries());

            upLevels.resize(mLogn);

            MC_AWAIT(upstream(src, controlBits, op, type, comm, gen, prng, root, upLevels));
            MC_AWAIT(downstream(src, op, root, upLevels, newVals, type, comm, gen, prng));

            upLevels.resize(1);

            if (dst.numEntries() != mN || dst.bitsPerEntry() != src.bitsPerEntry())
                dst.resize(mN, src.bitsPerEntry());

            MC_AWAIT(computeLeaf(upLevels[0], newVals, op, dst, type, prng, comm, gen));

            MC_END();
        }


        // take the rows
        //  in[srcRowStartIdx]
        //  ...
        //  in[srcRowStartIdx + numRows]
        //
        // and store the bit-transpose in dest. So each 
        // row j of dest will have the form 
        // 
        // dest[j] =  in(srcRowStartIdx,j), in(srcRowStartIdx+1,j),...,in(srcRowStartIdx+numRows,j)
        // 
        //static void toPackedBin(const BinMatrix& in, TBinMatrix& dest,
        //    u64 srcRowStartIdx,
        //    u64 numRows);

        /* This circuit outputs pre,suf,preVal where

                     |  pre  = prep1 ? pre_0 + pre_1 : pre_1,
                     |  suf  = sufp0 ? suf_0 + suf_1 : suf_0
                     *  prep = prep0 * prep1
                     *  sufp = sufp0 * sufp1
                    / \
                   /   \
                  /     \
              pre_0,    pre_1
              suf_0,    suf_1
              prep_0,   prep_1
              sufp_0,   sufp_1
        */
        static oc::BetaCircuit upstreamCir(
            u64 bitsPerEntry,
            Type type,
            const Operator& add);

        // Perform the upstream computation. Here we want to compute several
        // values for each node of the tree. Each node will hold 
        // 
        //  push up value val, left child value v0, a product preVal
        //
        // The value will be val=v_{1+p1} where v0,v1 are the left and right child
        // values. The product preVal will be preVal=p0*p1 where p0,p1 are the left and 
        // right child product values.  
        //
        // At the leaf level val will be the corresponding input value. preVal will be
        // the control bit to the leaf'sufVal right. I.e., for leaf i, preVal = controlBit[i-1] 
        // and preVal will be zero for the left most leaf (i.e. i=0).
        //
        // For each level of the tree, starting at the preSuf, we compute the 
        // parent values as described.
        macoro::task<> upstream(
            const BinMatrix& src,
            const BinMatrix& controlBits,
            const Operator& op,
            Type type,
            coproto::Socket& comm,
            CorGenerator& gen,
            PRNG& prng,
            Level& root,
            span<SplitLevel> levels);

        /* this circuit pushes values down the tree.

                       |  pre, suf, _
                       |
                       *
                      / \
                     /   \
                    /     \
              pre_0,      pre_1
              suf_0,      suf_1
              p_0,        p_1

           These values are updated as
             pre_1 := p_0 ? pre_0 + pre : pre_0
             pre_0 := pre
             suf_0 := suf_1 + suf : suf_1
             suf_1 := suf

           circuit inputs are
           - left Val
           - left bit
           - parent val

           outputs are:
           - left val
           - right val

           Prefix input outputs go first and then suffix. So the i/o above is repeated twice.

        */
        static oc::BetaCircuit downstreamCir(u64 bitsPerEntry, const Operator& op, Type type);


        // apply the downstream circuit to each level of the tree.
        macoro::task<> downstream(
            const BinMatrix& src,
            const Operator& op,
            Level& root,
            span<SplitLevel> levels,
            SplitLevel& preSuf,
            Type type,
            coproto::Socket& comm,
            CorGenerator& gen,
            PRNG& prng,
            std::vector<SplitLevel>* debugLevels = nullptr);

        //	// apply the downstream circuit to each level of the tree.
        macoro::task<> computeLeaf(
            SplitLevel& leaves,
            SplitLevel& preSuf,
            const Operator& op,
            BinMatrix& dst,
            Type type,
            PRNG& prng,
            coproto::Socket& comm,
            CorGenerator& gen)
        {

            MC_BEGIN(macoro::task<>, this, &leaves, &preSuf, &op, &dst, type, &comm, &gen,&prng,
                lib = oc::BetaLibrary{},
                cir = oc::BetaCircuit{},
                bitsPerEntry = u64{},
                size = u64{},
                bin = Gmw{}
            );

            bitsPerEntry = (type & Type::Prefix) ? leaves[0].mPreVal.bitsPerEntry() : leaves[0].mSufVal.bitsPerEntry();
            size = (type & Type::Prefix) ? leaves[0].mPreBit.numEntries() : leaves[0].mSufBit.numEntries();

            {
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
            }


            {

                bin.init(size, cir);

                int inIdx = 0;
                // the input values v for each leaf node.
                if (type & Type::Prefix)
                {
                    bin.mapInput(inIdx++, leaves[0].mPreVal);
                    bin.mapInput(inIdx++, leaves[1].mPreVal);
                }
                else
                {
                    bin.mapInput(inIdx++, leaves[0].mSufVal);
                    bin.mapInput(inIdx++, leaves[1].mSufVal);
                }

                if (type & Type::Prefix)
                {
                    // prefix val
                    bin.mapInput(inIdx++, preSuf[0].mPreVal);
                    bin.mapInput(inIdx++, preSuf[1].mPreVal);

                    // prefix bit
                    bin.mapInput(inIdx++, leaves[0].mPreBit);
                    bin.mapInput(inIdx++, leaves[1].mPreBit);

                }

                if (type & Type::Suffix)
                {
                    // prefix val
                    bin.mapInput(inIdx++, preSuf[0].mSufVal);
                    bin.mapInput(inIdx++, preSuf[1].mSufVal);

                    // prefix bit					 
                    bin.mapInput(inIdx++, leaves[0].mSufBit);
                    bin.mapInput(inIdx++, leaves[1].mSufBit);
                }
            }

            MC_AWAIT(bin.run(gen, comm, prng));

            {
                BinMatrix leftOut(size, bitsPerEntry), rghtOut(size, bitsPerEntry);
                bin.getOutput(0, leftOut);
                bin.getOutput(1, rghtOut);

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


    };



}