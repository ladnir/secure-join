#pragma once
#include "BatcherMerge.h"
#include "macoro/when_all.h"
#include <algorithm>
#include <functional>
#include <limits>
#include <stdexcept>
namespace secJoin { namespace merge_detail {
        inline u64 product(u64 a, u64 b)
        {
            if (b && a > std::numeric_limits<u64>::max()/b)
                throw std::overflow_error("RootMerge public dimensions overflow");
            return a*b;
        }
        inline u64 width(u64 size) { u64 w=1; while ((u64(1)<<w)<size) ++w; return w; }
        inline u64 power(u64 size) { u64 p=1; while(p<size) p*=2; return p; }
        inline u8 bit(const u8* p, u64 k) { return (p[k/8]>>(k%8))&1; }
        inline void put(u8* p, u64 k, u8 v) { p[k/8] |= (v&1)<<(k%8); }
        inline void copy(u8* d, u64 to, const u8* s, u64 from, u64 count)
        {
            while(count)
            {
                auto take=std::min<u64>(8-to%8,count);
                u16 v=s[from/8]>>(from%8);
                if(from%8+take>8) v|=u16(s[from/8+1])<<(8-from%8);
                d[to/8]|=(v&((1u<<take)-1))<<(to%8);
                to+=take; from+=take; count-=take;
            }
        }
        inline void integer(u8* d, u64 off, u64 value, u64 bits)
        {
            while(bits)
            {
                auto take=std::min<u64>(8-off%8,bits);
                d[off/8]|=(value&((1u<<take)-1))<<(off%8);
                value>>=take;off+=take;bits-=take;
            }
        }
        inline u64 integer(const u8* s, u64 off, u64 bits)
        {
            u64 value=0,shift=0;
            while(bits)
            {
                auto take=std::min<u64>(8-off%8,bits);
                value|=u64((s[off/8]>>(off%8))&((1u<<take)-1))<<shift;
                shift+=take;off+=take;bits-=take;
            }
            return value;
        }
        // Telescope the adjacent-XOR boundary encoding of a unary prefix.
        // Each share bit is loaded once, with weight i XOR (i+1).
        template<class F> u64 unaryCount(u64 count,F get)
        {
            u64 result=0;
            for(u64 i=0;i<count;++i)
                result^=(u64(0)-u64(get(i)&1))&(i^(i+1));
            return result;
        }
        inline u64 rounds(const Gmw& g)
        { return std::count_if(g.mCir.mLevelAndCounts.begin(),g.mCir.mLevelAndCounts.end(),[](auto n){return n!=0;}); }
        inline u64 ands(const Gmw& g) { return product(g.mCir.mNonlinearGateCount,oc::roundUpTo(g.mN,128)); }

        // Input zero is a two-bit public (zero,one) bundle. Explicit XOR with
        // the one input materializes complements instead of exporting InvWire.
        struct Circuit
        {
            BetaCircuit c;
            BetaBundle constants{2};
            Circuit() { c.addInputBundle(constants); }
            BetaBundle input(u64 n) { BetaBundle b(n); c.addInputBundle(b); return b; }
            BetaBundle output(u64 n) { BetaBundle b(n); c.addOutputBundle(b); return b; }
            BetaBundle temp(u64 n) { BetaBundle b(n); c.addTempWireBundle(b); return b; }
            u32 gate(u32 a,u32 b,oc::GateType op) { auto w=temp(1)[0]; c.addGate(a,b,op,w); return w; }
            u32 neg(u32 a) { return gate(a,constants[1],oc::GateType::Xor); }
            u32 knownAnd(u32 a,u32 b)
            {
                if(a==constants[0]||b==constants[0])return constants[0];
                if(a==constants[1]||a==b)return b;
                if(b==constants[1])return a;
                return gate(a,b,oc::GateType::And);
            }
            u32 select(u32 control,u32 yes,u32 no)
            {
                if(yes==no)return yes;
                if(yes==constants[1]&&no==constants[0])return control;
                if(yes==constants[0]&&no==constants[1])return neg(control);
                auto difference=gate(yes,no,oc::GateType::Xor);
                return gate(no,knownAnd(control,difference),oc::GateType::Xor);
            }
            u32 lessThan(const BetaBundle& a,const BetaBundle& b)
            {
                // An unequal segment is ordered by its most significant
                // differing bit of b. For equal segments the value may be
                // arbitrary, except the least-significant segment must encode
                // strict comparison. This removes the per-bit generate ANDs.
                struct Segment { u32 equal,less; };
                // Split by actual width, rather than attaching a short high
                // tail above a complete power-of-two tree. At 33 bits this
                // trades one AND for one fewer layer; at 32 bits it is identical.
                std::function<Segment(u64,u64)> build=[&](u64 begin,u64 size)->Segment
                {
                    if(size==1)
                    {
                        auto equal=neg(gate(a[begin],b[begin],oc::GateType::Xor));
                        return {equal,begin?b[begin]:gate(a[begin],b[begin],oc::GateType::na_And)};
                    }
                    auto lowSize=size/2;
                    auto lo=build(begin,lowSize),hi=build(begin+lowSize,size-lowSize);
                    auto value=select(hi.equal,lo.less,hi.less);
                    auto equal=begin?knownAnd(lo.equal,hi.equal):constants[0];
                    return {equal,value};
                };
                return build(0,a.size()).less;
            }
            void save(u32 a,u32 b)
            { if(a==constants[0])c.addCopy(a,b);else c.addGate(a,constants[0],oc::GateType::Xor,b); }
            BetaCircuit finish() { c.levelByAndDepth(); return std::move(c); }
        };
        inline BetaCircuit comparison(u64 bits)
        {
            Circuit c; auto a=c.input(bits),b=c.input(bits);
            auto out=c.output(1); auto less=c.lessThan(a,b);
            c.save(less,out[0]); return c.finish();
        }
        inline BetaCircuit scalarAnd()
        { Circuit c; auto a=c.input(1),b=c.input(1),o=c.output(1); c.c.addGate(a[0],b[0],oc::GateType::And,o[0]); return c.finish(); }
        inline BetaCircuit compareSwap(u64 keyBits,u64 rowBits,u64 retained)
        {
            Circuit c; auto a=c.input(rowBits),b=c.input(rowBits);
            auto left=c.output(retained),right=c.output(retained);
            BetaBundle ak,bk;ak.mWires.assign(a.mWires.begin(),a.mWires.begin()+keyBits);
            bk.mWires.assign(b.mWires.begin(),b.mWires.begin()+keyBits);
            // Keep the matched unequal Batcher baseline on the same comparator.
            auto swap=c.lessThan(bk,ak);
            for(u64 i=0;i<retained;++i)
            {
                auto diff=c.gate(a[i],b[i],oc::GateType::Xor);
                auto mask=c.gate(diff,swap,oc::GateType::And);
                c.c.addGate(a[i],mask,oc::GateType::Xor,left[i]);
                c.c.addGate(b[i],mask,oc::GateType::Xor,right[i]);
            }
            return c.finish();
        }
        inline macoro::task<> evaluate(Gmw& g,u64 role,const std::vector<const BinMatrix*>& inputs,
            const std::vector<BinMatrix*>& outputs,coproto::Socket& sock)
        {
            BinMatrix constants(g.mN,2);
            if(!role)for(u64 i=0;i<g.mN;++i)constants(i,0)=2;
            g.setInput(0,constants);
            for(u64 i=0;i<inputs.size();++i)g.setInput(i+1,*inputs[i]);
            co_await g.run(sock);
            for(u64 i=0;i<outputs.size();++i)g.getOutput(i,*outputs[i]);
            g.clear();
        }

        // Open exactly the requested low bits per row, including across byte
        // boundaries. Compression changes only encoding, never what is opened.
        inline macoro::task<std::vector<u8>> openPacked(const BinMatrix& values,u64 bits,coproto::Socket& sock)
        {
            auto bytes=oc::divCeil(product(values.rows(),bits),8);
            std::vector<u8> mine(bytes),peer(bytes);
            for(u64 i=0;i<values.rows();++i)copy(mine.data(),i*bits,values.data(i),0,bits);
            auto exchanged=co_await macoro::when_all_ready(sock.send(coproto::copy(mine)),sock.recv(peer));
            std::get<0>(exchanged).result();std::get<1>(exchanged).result();
            for(u64 i=0;i<bytes;++i)mine[i]^=peer[i];
            co_return mine;
        }

        // Add a secret w-bit count to the PUBLIC row index. Initial carry
        // generate/propagate bits are local, and only the w low bits interact.
        // The carry into the public high word selects between public constants.
        struct PublicIndexAdd
        {
            std::unique_ptr<Gmw> g;
            u64 rows=0,bits=0,outBits=0,role=0,numRounds=0,numAnds=0,period=0,indexPeriod=0;
            // A public power-of-two period lets independent instances share
            // one SIMD call without adding their batch offsets in the circuit.
            void init(u64 n,u64 w,u64 r,CorGenerator& cor,u64 repeat=0)
            {
                if(repeat && (repeat&(repeat-1)))throw std::invalid_argument("PublicIndexAdd: non-power-of-two period");
                rows=n;bits=w;outBits=r;role=cor.partyIdx();indexPeriod=repeat;
                Circuit c;auto p=c.input(w),carry=c.input(w),out=c.output(w);
                for(u64 stride=1;stride<w;stride*=2)
                {
                    auto previousP=p,previousCarry=carry;
                    for(u64 i=0;i<w;++i)if(i%(2*stride)>=stride)
                    {
                        auto left=i/(2*stride)*(2*stride)+stride-1;
                        carry[i]=c.gate(previousCarry[i],c.gate(previousP[i],previousCarry[left],oc::GateType::And),oc::GateType::Xor);
                        // A prefix rooted at bit zero never needs its propagate.
                        if(i>=2*stride)p[i]=c.gate(previousP[i],previousP[left],oc::GateType::And);
                    }
                }
                for(u64 i=0;i<w;++i)c.save(carry[i],out[i]);
                auto chosen=c.finish();u64 lanes=n;
                auto depth=[](const BetaCircuit& cir){return std::count_if(cir.mLevelAndCounts.begin(),cir.mLevelAndCounts.end(),[](auto v){return v!=0;});};
                // Public-index residues specialize the carry circuit. Batch
                // all residues in one circuit so they still run in parallel.
                // Use it only when padded AND cost falls without added depth.
                // A short public index period never uses the other residues.
                // Including them would duplicate dead circuit work and prevent
                // specialization of the narrow alignment-rank additions.
                auto residues=repeat?std::min(u64(1)<<w,repeat):(u64(1)<<w);
                auto candidateLanes=oc::divCeil(n,residues);
                u64 residueBits=0;for(auto v=residues;v>1;v>>=1)++residueBits;
                // For public addend k>0, the w-1-ctz(k) nonlinear carries
                // are linearly independent modulo linear input functions.
                // Summing this lower bound over the independent residue
                // inputs rejects impossible wins before building a large
                // circuit. Sum_{k=1}^{2^p-1} ctz(k) = 2^p-1-p.
                auto minimumAnds=w>1?(residues-1)*(w-2)+residueBits:0;
                if(residues<=2048&&residues<=n&&
                    product(minimumAnds,oc::roundUpTo(candidateLanes,128))<product(chosen.mNonlinearGateCount,oc::roundUpTo(n,128)))
                {
                    Circuit specialized;auto input=specialized.input(residues*w),output=specialized.output(residues*w);
                    for(u64 k=0;k<residues;++k)
                    {
                        std::vector<u32> propagate(w),value(w);
                        for(u64 j=0;j<w;++j)
                        {
                            auto a=input[k*w+j];bool one=(k>>j)&1;
                            propagate[j]=one?specialized.neg(a):a;
                            // When a segment does not propagate, its carry is
                            // the public index bit. Otherwise its value may be
                            // arbitrary, except bit zero binds input carry zero.
                            value[j]=j?specialized.constants[one]:(one?a:specialized.constants[0]);
                        }
                        for(u64 stride=1;stride<w;stride*=2)
                        {
                            auto oldP=propagate,oldV=value;
                            for(u64 j=0;j<w;++j)if(j%(2*stride)>=stride)
                            {
                                auto left=j/(2*stride)*(2*stride)+stride-1;
                                value[j]=specialized.select(oldP[j],oldV[left],oldV[j]);
                                if(j>=2*stride)propagate[j]=specialized.knownAnd(oldP[j],oldP[left]);
                            }
                        }
                        for(u64 j=0;j<w;++j)specialized.save(value[j],output[k*w+j]);
                    }
                    auto candidate=specialized.finish();
                    if(product(candidate.mNonlinearGateCount,oc::roundUpTo(candidateLanes,128))<product(chosen.mNonlinearGateCount,oc::roundUpTo(n,128))
                        &&depth(candidate)<=depth(chosen))
                    {chosen=std::move(candidate);lanes=candidateLanes;period=residues;}
                }
                g=std::make_unique<Gmw>();g->init(lanes,chosen,cor);
                numRounds=rounds(*g);numAnds=ands(*g);
            }
            void preprocess(){g->preprocess();}
            macoro::task<> apply(const BinMatrix& count,BinMatrix& output,coproto::Socket& sock)
            {
                BinMatrix carry(rows,bits);
                if(period)
                {
                    BinMatrix input(g->mN,period*bits),groupCarry(g->mN,period*bits);
                    for(u64 i=0;i<rows;++i)copy(input.data(i/period),(i%period)*bits,count.data(i),0,bits);
                    co_await evaluate(*g,role,{&input},{&groupCarry},sock);
                    for(u64 i=0;i<rows;++i)copy(carry.data(i),0,groupCarry.data(i/period),(i%period)*bits,bits);
                }
                else
                {
                    BinMatrix p(rows,bits),generate(rows,bits);
                    for(u64 i=0;i<rows;++i)
                    {
                        auto value=integer(count.data(i),0,bits);
                        auto index=indexPeriod?i%indexPeriod:i;
                        integer(p.data(i),0,value^(role?0:index),bits);
                        integer(generate.data(i),0,value&index,bits);
                    }
                    co_await evaluate(*g,role,{&p,&generate},{&carry},sock);
                }
                g.reset();
                output.resize(rows,outBits);
                for(u64 i=0;i<rows;++i)
                {
                    auto index=indexPeriod?i%indexPeriod:i;
                    auto carries=integer(carry.data(i),0,bits),high=index>>bits;
                    auto low=(integer(count.data(i),0,bits)^(role?0:index)^(carries<<1))&((u64(1)<<bits)-1);
                    high=(role?0:high)^((u64(0)-(carries>>(bits-1)))&(high^(high+1)));
                    integer(output.data(i),0,low|(high<<bits),outBits);
                }
            }
        };

}}
