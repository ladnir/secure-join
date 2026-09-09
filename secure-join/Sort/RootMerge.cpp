#include "RootMerge.h"
#include "BatcherMerge.h"
#include "StableSecretExtract.h"
#include "secure-join/AggTree/BatchPrefix.h"
#include "cryptoTools/Circuit/BetaLibrary.h"
#include "macoro/when_all.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace secJoin
{
    namespace
    {
        u64 product(u64 a, u64 b)
        {
            if (b && a > std::numeric_limits<u64>::max()/b)
                throw std::overflow_error("RootMerge public dimensions overflow");
            return a*b;
        }
        u64 width(u64 size) { u64 w=1; while ((u64(1)<<w)<size) ++w; return w; }
        u64 power(u64 size) { u64 p=1; while(p<size) p*=2; return p; }
        u8 bit(const u8* p, u64 k) { return (p[k/8]>>(k%8))&1; }
        void put(u8* p, u64 k, u8 v) { p[k/8] |= (v&1)<<(k%8); }
        void copy(u8* d, u64 to, const u8* s, u64 from, u64 count)
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
        void integer(u8* d, u64 off, u64 value, u64 bits)
        { for(u64 i=0;i<bits;++i) put(d,off+i,value>>i); }
        u64 integer(const u8* s, u64 off, u64 bits)
        { u64 v=0; for(u64 i=0;i<bits;++i) v|=u64(bit(s,off+i))<<i; return v; }
        u64 rounds(const Gmw& g)
        { return std::count_if(g.mCir.mLevelAndCounts.begin(),g.mCir.mLevelAndCounts.end(),[](auto n){return n!=0;}); }
        u64 ands(const Gmw& g) { return product(g.mCir.mNonlinearGateCount,oc::roundUpTo(g.mN,128)); }

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
            void save(u32 a,u32 b)
            { if(a==constants[0])c.addCopy(a,b);else c.addGate(a,constants[0],oc::GateType::Xor,b); }
            BetaBundle extend(const BetaBundle& a,u64 n)
            { BetaBundle b; b.mWires.assign(n,constants[0]); std::copy_n(a.mWires.begin(),std::min<u64>(a.size(),n),b.mWires.begin()); return b; }
            BetaBundle add(const BetaBundle& a,const BetaBundle& b,u64 n,bool subtract=false)
            {
                auto z=temp(n); auto aa=extend(a,n),bb=extend(b,n);
                // The circuit levelizer requires distinct operands even for
                // padding zeros. A free copy preserves the shared value.
                for(u64 i=0;i<n;++i)if(aa[i]==bb[i])
                {auto w=temp(1)[0];c.addCopy(bb[i],w);bb[i]=w;}
                using L=oc::BetaLibrary;
                L::parallelPrefix_build(c,aa,bb,z,L::IntType::Unsigned,
                    subtract ? L::AdderType::Subtraction : L::AdderType::Addition);
                return z;
            }
            BetaBundle sumBits(const BetaBundle& in)
            {
                std::vector<BetaBundle> rows;
                for(auto w:in.mWires) { BetaBundle b; b.mWires={w}; rows.push_back(b); }
                while(rows.size()>1)
                {
                    std::vector<BetaBundle> next;
                    for(u64 i=0;i<rows.size();i+=2)
                        next.push_back(i+1<rows.size()?add(rows[i],rows[i+1],std::max(rows[i].size(),rows[i+1].size())+1):rows[i]);
                    rows=std::move(next);
                }
                return rows.empty()?extend({},1):rows[0];
            }
            BetaCircuit finish() { c.levelByAndDepth(); return std::move(c); }
        };
        BetaCircuit comparison(u64 bits,bool valid)
        {
            Circuit c; auto a=c.input(bits),b=c.input(bits); BetaBundle real;
            if(valid) real=c.input(1);
            auto out=c.output(1); auto less=logstarLessThan(c.c,a,b);
            if(valid) less=c.gate(less,real[0],oc::GateType::And);
            c.save(less,out[0]); return c.finish();
        }
        BetaCircuit scalarAnd()
        { Circuit c; auto a=c.input(1),b=c.input(1),o=c.output(1); c.c.addGate(a[0],b[0],oc::GateType::And,o[0]); return c.finish(); }
        BetaCircuit popcount(u64 inputs,u64 outBits,bool complement=false)
        {
            Circuit c; auto a=c.input(inputs),o=c.output(outBits);
            if(complement) for(auto& w:a.mWires) w=c.neg(w);
            auto sum=c.extend(c.sumBits(a),outBits);
            for(u64 j=0;j<outBits;++j)c.save(sum[j],o[j]);
            return c.finish();
        }
        BetaCircuit xRank(u64 r,u64 offsetBits,u64 idBits,u64 countBits,u64 shift,bool cube)
        {
            Circuit c; auto offset=c.input(offsetBits),id=c.input(idBits),idx=c.input(r);
            BetaBundle groups;
            if(cube)groups=c.input(countBits);
            auto out=c.output(r); auto start=c.extend({},r);
            for(u64 j=0;j<idBits && j+shift<r;++j)start[j+shift]=id[j];
            auto result=c.add(c.add(offset,start,r),idx,r);
            if(cube)
            {
                auto skipped=c.extend({},r);
                for(u64 j=0;j<countBits && j+shift<r;++j)skipped[j+shift]=groups[j];
                result=c.add(result,skipped,r,true);
            }
            for(u64 j=0;j<r;++j)c.save(result[j],out[j]);
            return c.finish();
        }
        BetaCircuit countDelta(u64 w,bool sqrt)
        {
            Circuit c; auto fine=c.input(w),coarse=c.input(w); BetaBundle index;
            if(sqrt) index=c.input(w);
            auto out=c.output(w);
            auto value=sqrt?c.add(fine,index,w):fine;
            value=c.add(value,coarse,w,true);
            for(u64 j=0;j<w;++j)c.save(value[j],out[j]);
            return c.finish();
        }
        BetaCircuit yRank(u64 w,u64 r)
        {
            Circuit c; auto delta=c.input(w),coarse=c.input(w),index=c.input(r),out=c.output(r);
            auto count=c.add(delta,coarse,w);
            auto rank=c.add(count,index,r);
            for(u64 j=0;j<r;++j)c.save(rank[j],out[j]);
            return c.finish();
        }
        BetaCircuit compareSwap(u64 keyBits,u64 rowBits,u64 retained)
        {
            Circuit c; auto a=c.input(rowBits),b=c.input(rowBits);
            auto left=c.output(retained),right=c.output(retained);
            BetaBundle ak,bk;ak.mWires.assign(a.mWires.begin(),a.mWires.begin()+keyBits);
            bk.mWires.assign(b.mWires.begin(),b.mWires.begin()+keyBits);
            auto swap=logstarLessThan(c.c,bk,ak);
            for(u64 i=0;i<retained;++i)
            {
                auto diff=c.gate(a[i],b[i],oc::GateType::Xor);
                auto mask=c.gate(diff,swap,oc::GateType::And);
                c.c.addGate(a[i],mask,oc::GateType::Xor,left[i]);
                c.c.addGate(b[i],mask,oc::GateType::Xor,right[i]);
            }
            return c.finish();
        }
        macoro::task<> evaluate(Gmw& g,u64 role,const std::vector<const BinMatrix*>& inputs,
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

        // Brent--Kung segmented sum. SIMD lanes flatten (tree edge, word),
        // avoiding 128 copies of an entire wide block at the root of the tree.
        struct SuffixSum
        {
            struct Stage { std::vector<std::pair<u64,u64>> edges; std::unique_ptr<Gmw> g; };
            std::vector<Stage> stages;
            u64 leaves=0,fields=0,bits=0,role=0,numRounds=0,numAnds=0;
            void init(u64 l,u64 f,u64 w,CorGenerator& cor)
            {
                leaves=l;fields=f;bits=w;role=cor.partyIdx();
                Circuit c;auto a=c.input(w),b=c.input(w),ac=c.input(1),bc=c.input(1);
                auto out=c.output(w),control=c.output(1),masked=c.temp(w);
                for(u64 i=0;i<w;++i)c.c.addGate(a[i],bc[0],oc::GateType::And,masked[i]);
                auto sum=c.add(masked,b,w);
                for(u64 i=0;i<w;++i)c.save(sum[i],out[i]);
                c.c.addGate(ac[0],bc[0],oc::GateType::And,control[0]);
                auto circuit=c.finish();
                auto stage=[&](u64 stride,bool up)
                {
                    Stage s;
                    for(u64 i=(up?2:3)*stride-1;i<l;i+=2*stride)s.edges.emplace_back(i-stride,i);
                    s.g=std::make_unique<Gmw>();s.g->init(product(s.edges.size(),f),circuit,cor);
                    numRounds+=rounds(*s.g);numAnds+=ands(*s.g);stages.push_back(std::move(s));
                };
                for(u64 stride=1;stride<l;stride*=2)stage(stride,true);
                for(u64 stride=l/4;stride;stride/=2)stage(stride,false);
            }
            void preprocess(){for(auto& s:stages)s.g->preprocess();}
            macoro::task<> apply(const BinMatrix& input,const BinMatrix& ctrl,BinMatrix& out,coproto::Socket& sock)
            {
                BinMatrix work(leaves,fields*bits),controls(leaves,1);
                for(u64 i=0;i<leaves;++i)
                {
                    std::copy(input[leaves-1-i].begin(),input[leaves-1-i].end(),work[i].begin());
                    if(i)controls(i,0)=ctrl(leaves-i,0);
                }
                for(auto& s:stages)
                {
                    auto lanes=s.edges.size()*fields;
                    BinMatrix a(lanes,bits),b(lanes,bits),ac(lanes,1),bc(lanes,1),v(lanes,bits),cv(lanes,1);
                    u64 row=0;
                    for(auto [left,right]:s.edges)for(u64 j=0;j<fields;++j,++row)
                    {
                        copy(a.data(row),0,work.data(left),j*bits,bits);
                        copy(b.data(row),0,work.data(right),j*bits,bits);
                        ac(row,0)=controls(left,0);bc(row,0)=controls(right,0);
                    }
                    co_await evaluate(*s.g,role,{&a,&b,&ac,&bc},{&v,&cv},sock);
                    row=0;
                    for(auto [left,right]:s.edges)
                    {
                        std::fill(work[right].begin(),work[right].end(),0);
                        controls(right,0)=cv(row,0);
                        for(u64 j=0;j<fields;++j,++row)copy(work.data(right),j*bits,v.data(row),0,bits);
                    }
                    s.g.reset();
                }
                out.resize(leaves,fields*bits);
                for(u64 i=0;i<leaves;++i)std::copy(work[leaves-1-i].begin(),work[leaves-1-i].end(),out[i].begin());
            }
        };

        struct UnequalBatcher
        {
            std::vector<std::vector<std::pair<u64,u64>>> layers;
            std::vector<u64> output,depth;
            std::vector<std::unique_ptr<Gmw>> gmws;
            u64 role=0,rows=0,rowBits=0,rankBits=0,numRounds=0,numAnds=0,numComparisons=0;
            void comparator(u64 a,u64 b)
            {
                auto d=std::max(depth[a],depth[b]);
                if(layers.size()<=d)layers.resize(d+1);
                layers[d].emplace_back(a,b);depth[a]=depth[b]=d+1;++numComparisons;
            }
            std::vector<u64> merge(std::vector<u64> a,std::vector<u64> b)
            {
                if(a.empty())return b;
                if(b.empty())return a;
                if(a.size()==1&&b.size()==1){comparator(a[0],b[0]);return {a[0],b[0]};}
                std::vector<u64> ae,ao,be,bo;
                for(u64 i=0;i<a.size();++i)(i%2?ao:ae).push_back(a[i]);
                for(u64 i=0;i<b.size();++i)(i%2?bo:be).push_back(b[i]);
                auto even=merge(std::move(ae),std::move(be)),odd=merge(std::move(ao),std::move(bo));
                std::vector<u64> out;out.reserve(a.size()+b.size());
                for(u64 i=0;i<even.size();++i){out.push_back(even[i]);if(i<odd.size())out.push_back(odd[i]);}
                for(u64 i=1;i+1<out.size();i+=2)comparator(out[i],out[i+1]);
                return out;
            }
            void init(u64 m,u64 n,u64 bits,u64 ranks,CorGenerator& cor)
            {
                rows=m+n;rowBits=bits+ranks;rankBits=ranks;role=cor.partyIdx();depth.resize(rows);
                std::vector<u64>a(m),b(n);std::iota(a.begin(),a.end(),0);std::iota(b.begin(),b.end(),m);
                output=merge(std::move(a),std::move(b));
                for(u64 i=0;i<layers.size();++i)
                {
                    auto g=std::make_unique<Gmw>();
                    g->init(layers[i].size(),compareSwap(rowBits,rowBits,i+1==layers.size()?rankBits:rowBits),cor);
                    numRounds+=rounds(*g);numAnds+=ands(*g);gmws.push_back(std::move(g));
                }
            }
            void preprocess(){for(auto& g:gmws)g->preprocess();}
            macoro::task<> apply(const BinMatrix& x,const BinMatrix& y,AdditivePerm& result,coproto::Socket& sock)
            {
                BinMatrix work(rows,rowBits);
                for(u64 i=0;i<rows;++i)
                {
                    if(!role)integer(work.data(i),0,i,rankBits);
                    copy(work.data(i),rankBits,i<x.rows()?x.data(i):y.data(i-x.rows()),0,x.bitsPerEntry());
                }
                for(u64 i=0;i<layers.size();++i)
                {
                    auto count=layers[i].size(),retained=i+1==layers.size()?rankBits:rowBits;
                    BinMatrix a(count,rowBits),b(count,rowBits),lo(count,retained),hi(count,retained);
                    u64 j=0;for(auto [l,r]:layers[i])
                    {
                        std::copy(work[l].begin(),work[l].end(),a[j].begin());
                        std::copy(work[r].begin(),work[r].end(),b[j].begin());++j;
                    }
                    co_await evaluate(*gmws[i],role,{&a,&b},{&lo,&hi},sock);gmws[i].reset();
                    j=0;for(auto [l,r]:layers[i])
                    {
                        std::fill(work[l].begin(),work[l].end(),0);std::fill(work[r].begin(),work[r].end(),0);
                        copy(work.data(l),0,lo.data(j),0,retained);copy(work.data(r),0,hi.data(j),0,retained);++j;
                    }
                }
                result.mShare.resize(rows);
                for(u64 i=0;i<rows;++i)result.mShare[i]=integer(work.data(output[i]),0,rankBits);
            }
        };
    }

    struct RootMerge::Impl
    {
        explicit Impl(RootMergeKind k):kind(k){}
        RootMergeKind kind;
        bool initialized=false,preprocessed=false,prepareStarted=false,prepared=false,used=false;
        u64 m=0,n=0,bits=0,block=0,blocks=0,leaves=0,role=0,r=0,cw=0,idw=0,shift=0;
        u64 recordBits=0,forwardBytes=0,backBytes=0,offsetBits=0,comparisonCount=0;
        std::unique_ptr<Gmw> boundary,same,tagProducts,coarse,detail,xCount,fineCount,groups,xRanks,delta,yRanks;
        AltModComposedPerm blockGen;
        ComposedPerm blockPerm;
        BatchPrefix broadcast;
        SuffixSum suffix;
        StableSecretExtract invert;
        UnequalBatcher batcher;
        std::vector<PiLogStarStage> stats;
        std::unique_ptr<Gmw> gate(u64 lanes,BetaCircuit c,CorGenerator& cor)
        { auto g=std::make_unique<Gmw>();g->init(lanes,c,cor);return g; }
        void add(u64 stage,const std::unique_ptr<Gmw>& g)
        { if(g){stats[stage].gmwRounds+=rounds(*g);stats[stage].paddedAnds+=ands(*g);} }
        void init(u64 m_,u64 n_,u64 bits_,CorGenerator& cor,u64 block_)
        {
            if(initialized||!m_||m_>n_||n_>=(u64(1)<<28)||!bits_||bits_>256||
               !cor.initialized()||cor.partyIdx()>1||cor.mGenState->mMock||cor.mGenState->mDebug)
                throw std::invalid_argument("RootMerge: invalid initialization");
            m=m_;n=n_;bits=bits_;role=cor.partyIdx();r=width(m+n);cw=width(m+1);
            if(kind==RootMergeKind::Batcher)
            {
                if(block_)throw std::invalid_argument("BatcherUnequalMerge has no block-size parameter");
                batcher.init(m,n,bits,r,cor);comparisonCount=batcher.numComparisons;
                stats={{"batcher_odd_even_merge",1,n,0,batcher.numRounds,batcher.numAnds}};
                initialized=true;return;
            }
            block=block_?block_:power(m);
            if((block&(block-1))||block>power(n))throw std::invalid_argument("RootMerge block must be a power of two no greater than padded n");
            blocks=oc::divCeil(n,block);leaves=power(m);idw=width(blocks);
            while((u64(1)<<shift)<block)++shift;
            const bool cube=kind==RootMergeKind::CubeRoot;
            // One real block for each first X in a block, otherwise one dummy.
            // Record: tag, continuation, block ID, coarse count, then padded keys.
            recordBits=product(block,bits+1)+cw+1+idw+cw+(cube?0:cw);
            forwardBytes=oc::divCeil(recordBits,8);backBytes=oc::divCeil(product(block,cw),8);
            if(recordBits>std::numeric_limits<u32>::max()/8)throw std::overflow_error("RootMerge block exceeds circuit dimensions");
            blockGen.init(role,blocks+m,forwardBytes+backBytes,cor);
            invert.init(m+n,m+n,r,cor,true);
            auto q=product(m,blocks-1);
            if(q)boundary=gate(q,comparison(bits+1,false),cor);
            if(m>1)same=gate(product(m-1,blocks),scalarAnd(),cor);
            tagProducts=gate(product(m,blocks),scalarAnd(),cor);
            coarse=gate(blocks,popcount(m,cw,true),cor);
            auto detailRows=product(m,block);
            if(cube)detailRows=product(detailRows,m);
            detail=gate(detailRows,comparison(bits+1,cube),cor);
            comparisonCount=q+detailRows;
            offsetBits=width((cube?m:1)*block+1);
            xCount=gate(m,popcount((cube?m:1)*block,offsetBits),cor);
            if(cube)
            {
                groups=gate(m,popcount(m,cw),cor);
                fineCount=gate(product(m,block),popcount(m,cw,true),cor);
            }
            else
            {
                broadcast.init(1,leaves,recordBits,cor,8);
                suffix.init(leaves,block,cw,cor);
            }
            xRanks=gate(m,xRank(r,offsetBits,idw,cw,shift,cube),cor);
            delta=gate(product(m,block),countDelta(cw,!cube),cor);
            yRanks=gate(n,yRank(cw,r),cor);
            stats={{"boundary_comparisons_and_tags",1,n,block},
                   {"selected_blocks_and_detail_comparisons",1,n,block},
                   {"rank_recovery",1,n,block},
                   {"inverse_permutation",1,m+n,0}};
            add(0,boundary);add(0,same);add(0,tagProducts);add(0,coarse);
            add(1,detail);add(1,xCount);add(1,groups);
            stats[1].gmwRounds+=broadcast.numRounds();stats[1].paddedAnds+=broadcast.numAnds();
            add(2,xRanks);add(2,fineCount);add(2,delta);add(2,yRanks);
            stats[2].gmwRounds+=suffix.numRounds;stats[2].paddedAnds+=suffix.numAnds;
            initialized=true;
        }
        void preprocess()
        {
            if(!initialized||preprocessed)throw std::logic_error("RootMerge preprocess state");
            if(kind==RootMergeKind::Batcher)batcher.preprocess();
            else
            {
                for(auto* g:{&boundary,&same,&tagProducts,&coarse,&detail,&xCount,&fineCount,&groups,&xRanks,&delta,&yRanks})if(*g)(*g)->preprocess();
                blockGen.preprocess();invert.preprocess();
                if(kind==RootMergeKind::SquareRoot){broadcast.preprocess();suffix.preprocess();}
            }
            preprocessed=true;
        }
        macoro::task<> prepare(coproto::Socket& sock,PRNG& prng)
        {
            if(!preprocessed||prepareStarted)throw std::logic_error("RootMerge prepare state");
            prepareStarted=true;
            if(kind!=RootMergeKind::Batcher)
            {
                co_await blockGen.generate(sock,prng,blocks+m,blockPerm);
                co_await invert.prepare(sock,prng);
            }
            prepared=true;
        }
        macoro::task<> run(const BinMatrix& x,const BinMatrix& y,AdditivePerm& out,coproto::Socket& sock,PRNG& prng);
    };

    macoro::task<> RootMerge::Impl::run(const BinMatrix& x,const BinMatrix& y,
        AdditivePerm& out,coproto::Socket& sock,PRNG& prng)
    {
        (void)prng;
        if(!prepared||used)throw std::logic_error("RootMerge requires fresh prepared correlations");
        if(x.rows()!=m||y.rows()!=n||x.bitsPerEntry()!=bits||y.bitsPerEntry()!=bits||
           x.bytesPerEntry()!=oc::divCeil(bits,8)||y.bytesPerEntry()!=oc::divCeil(bits,8))
            throw std::invalid_argument("RootMerge input dimensions differ from init");
        used=true;
        auto start=std::chrono::steady_clock::now();
        u64 sent=sock.bytesSent(),received=sock.bytesReceived();
        auto record=[&](u64 i)
        {
            auto now=std::chrono::steady_clock::now();
            stats[i].milliseconds=std::chrono::duration<double,std::milli>(now-start).count();
            stats[i].sentBytes=sock.bytesSent()-sent;stats[i].receivedBytes=sock.bytesReceived()-received;
            start=now;sent=sock.bytesSent();received=sock.bytesReceived();
        };
        if(kind==RootMergeKind::Batcher)
        {co_await batcher.apply(x,y,out,sock);record(0);co_return;}
        const bool cube=kind==RootMergeKind::CubeRoot;
        const u64 coarseOff=cw+1+idw,firstOff=coarseOff+cw,keyOff=firstOff+(cube?0:cw);
        // Cross comparisons against every block maximum except the last.
        // Force the final comparison to zero so the last block also receives
        // keys above Y's maximum; their within-block insertion offset is its length.
        BinMatrix boundaryBits(m,blocks),maps(m,blocks),first(m,1),target(m,idw);
        if(boundary)
        {
            auto lanes=m*(blocks-1);
            BinMatrix a(lanes,bits+1),b(lanes,bits+1),q(lanes,1);
            for(u64 i=0;i<m;++i)for(u64 j=0;j+1<blocks;++j)
            {
                auto row=i*(blocks-1)+j;
                copy(a.data(row),0,y.data((j+1)*block-1),0,bits);
                copy(b.data(row),0,x.data(i),0,bits);
            }
            co_await evaluate(*boundary,role,{&a,&b},{&q},sock);boundary.reset();
            for(u64 i=0;i<m;++i)for(u64 j=0;j+1<blocks;++j)
                put(boundaryBits.data(i),j,q(i*(blocks-1)+j,0));
        }
        for(u64 i=0;i<m;++i)for(u64 j=0;j<blocks;++j)
        {
            const u8 prev=j?bit(boundaryBits.data(i),j-1):u8(!role);
            auto mapping=prev^bit(boundaryBits.data(i),j);
            put(maps.data(i),j,mapping);
            for(u64 k=0;k<idw;++k)if((j>>k)&1)target(i,k/8)^=mapping<<(k%8);
        }
        first(0,0)=!role;
        if(same)
        {
            BinMatrix a((m-1)*blocks,1),b((m-1)*blocks,1),q((m-1)*blocks,1);
            for(u64 i=1;i<m;++i)for(u64 j=0;j<blocks;++j)
            {a((i-1)*blocks+j,0)=bit(maps.data(i-1),j);b((i-1)*blocks+j,0)=bit(maps.data(i),j);}
            co_await evaluate(*same,role,{&a,&b},{&q},sock);same.reset();
            for(u64 i=1;i<m;++i)
            {first(i,0)=!role;for(u64 j=0;j<blocks;++j)first(i,0)^=q((i-1)*blocks+j,0)&1;}
        }
        BinMatrix products(m*blocks,1);
        {
            BinMatrix a(m*blocks,1),b(m*blocks,1);
            for(u64 i=0;i<m;++i)for(u64 j=0;j<blocks;++j)
            {a(i*blocks+j,0)=first(i,0);b(i*blocks+j,0)=bit(maps.data(i),j);}
            co_await evaluate(*tagProducts,role,{&a,&b},{&products},sock);tagProducts.reset();
        }
        BinMatrix coarseCounts(blocks,cw);
        {
            BinMatrix in(blocks,m);
            for(u64 j=0;j<blocks;++j)for(u64 i=0;i<m;++i)put(in.data(j),i,bit(boundaryBits.data(i),j));
            co_await evaluate(*coarse,role,{&in},{&coarseCounts},sock);coarse.reset();
        }
        boundaryBits={};maps={};
        record(0);

        BinMatrix selected(leaves,recordBits),control(leaves,1);
        std::vector<u32> positions(m);
        {
            BinMatrix records(blocks+m,recordBits),shuffled(blocks+m,recordBits);
            for(u64 j=0;j<blocks;++j)
            {
                u64 tag=0,index=0;
                for(u64 i=0;i<m;++i)if(products(i*blocks+j,0)&1){tag^=i+1;index^=i;}
                integer(records.data(j),0,tag,cw);
                if(!role)integer(records.data(j),cw+1,j,idw);
                copy(records.data(j),coarseOff,coarseCounts.data(j),0,cw);
                if(!cube)integer(records.data(j),firstOff,index,cw);
                for(u64 t=0;t<block;++t)
                {
                    if(j*block+t<n)copy(records.data(j),keyOff+t*(bits+1),y.data(j*block+t),0,bits);
                    else if(!role)put(records.data(j),keyOff+t*(bits+1)+bits,1);
                }
            }
            for(u64 i=0;i<m;++i)
            {
                auto row=blocks+i;
                if((first(i,0)^u8(!role))&1)integer(records.data(row),0,i+1,cw);
                if(!role)
                {
                    put(records.data(row),cw,1);
                    if(!cube)integer(records.data(row),firstOff,i,cw);
                    for(u64 t=0;t<block;++t)put(records.data(row),keyOff+t*(bits+1)+bits,1);
                }
            }
            co_await blockPerm.apply<u8>(PermOp::Regular,records.mData,shuffled.mData,sock);
            std::vector<u32> mine(blocks+m),peer(blocks+m);
            for(u64 j=0;j<blocks+m;++j)mine[j]=integer(shuffled.data(j),0,cw);
            auto opened=co_await macoro::when_all_ready(sock.send(coproto::copy(mine)),sock.recv(peer));
            std::get<0>(opened).result();std::get<1>(opened).result();
            std::vector<bool> seen(m);
            for(u64 j=0;j<blocks+m;++j)
            {
                auto tag=mine[j]^peer[j];
                if(!tag)continue;
                if(tag>m||seen[tag-1])throw std::runtime_error("RootMerge invalid shuffled tags");
                seen[tag-1]=true;positions[tag-1]=j;
                std::copy(shuffled[j].begin(),shuffled[j].end(),selected[tag-1].begin());
                control(tag-1,0)=bit(selected.data(tag-1),cw);
            }
            if(std::find(seen.begin(),seen.end(),false)!=seen.end())throw std::runtime_error("RootMerge incomplete shuffled tags");
        }
        products={};
        BinMatrix values;
        if(cube)values=selected;
        else co_await broadcast.apply(selected,control,values,sock);
        const u64 detailWidth=(cube?m:1)*block,detailRows=m*detailWidth;
        BinMatrix q(detailRows,1),offsets(m,offsetBits),groupCounts(m,cw);
        {
            BinMatrix a(detailRows,bits+1),b(detailRows,bits+1),valid;
            if(cube)valid.resize(detailRows,1);
            for(u64 i=0;i<m;++i)for(u64 j=0;j<(cube?m:1);++j)for(u64 t=0;t<block;++t)
            {
                auto row=i*detailWidth+j*block+t,slot=cube?j:i;
                copy(a.data(row),0,values.data(slot),keyOff+t*(bits+1),bits+1);
                copy(b.data(row),0,x.data(i),0,bits);
                if(cube)valid(row,0)=control(slot,0)^u8(!role);
            }
            if(cube)co_await evaluate(*detail,role,{&a,&b,&valid},{&q},sock);
            else co_await evaluate(*detail,role,{&a,&b},{&q},sock);
            detail.reset();
        }
        {
            BinMatrix in(m,detailWidth);
            for(u64 i=0;i<m;++i)for(u64 j=0;j<detailWidth;++j)put(in.data(i),j,q(i*detailWidth+j,0));
            co_await evaluate(*xCount,role,{&in},{&offsets},sock);xCount.reset();
        }
        if(cube)
        {
            BinMatrix in(m,m);
            for(u64 i=0;i<m;++i)for(u64 j=0;j<=i;++j)put(in.data(i),j,first(j,0));
            co_await evaluate(*groups,role,{&in},{&groupCounts},sock);groups.reset();
        }
        first={};record(1);
        BinMatrix xr(m,r),yr(n,r),fine(m*block,cw);
        {
            BinMatrix index(m,r);
            if(!role)for(u64 i=0;i<m;++i)integer(index.data(i),0,i+(cube?block:0),r);
            if(cube)co_await evaluate(*xRanks,role,{&offsets,&target,&index,&groupCounts},{&xr},sock);
            else co_await evaluate(*xRanks,role,{&offsets,&target,&index},{&xr},sock);
            xRanks.reset();
        }
        offsets={};target={};groupCounts={};
        if(cube)
        {
            BinMatrix in(m*block,m);
            for(u64 j=0;j<m*block;++j)for(u64 i=0;i<m;++i)put(in.data(j),i,q(i*detailWidth+j,0));
            co_await evaluate(*fineCount,role,{&in},{&fine},sock);fineCount.reset();
        }
        else
        {
            BinMatrix in(leaves,block*cw),aggregated;
            for(u64 i=0;i<m;++i)for(u64 t=0;t<block;++t)put(in.data(i),t*cw,q(i*block+t,0)^u8(!role));
            co_await suffix.apply(in,control,aggregated,sock);
            for(u64 i=0;i<m;++i)for(u64 t=0;t<block;++t)copy(fine.data(i*block+t),0,aggregated.data(i),t*cw,cw);
        }
        q={};selected={};control={};
        BinMatrix corrections(m*block,cw);
        {
            BinMatrix base(m*block,cw),index(m*block,cw);
            for(u64 i=0;i<m;++i)for(u64 t=0;t<block;++t)
            {
                copy(base.data(i*block+t),0,values.data(i),coarseOff,cw);
                if(!cube)copy(index.data(i*block+t),0,values.data(i),firstOff,cw);
            }
            if(cube)co_await evaluate(*delta,role,{&fine,&base},{&corrections},sock);
            else co_await evaluate(*delta,role,{&fine,&base,&index},{&corrections},sock);
            delta.reset();
        }
        fine={};values={};
        {
            BinMatrix back(blocks+m,block*cw),unshuffled(blocks+m,block*cw);
            for(u64 i=0;i<m;++i)for(u64 t=0;t<block;++t)
                copy(back.data(positions[i]),t*cw,corrections.data(i*block+t),0,cw);
            // Same secret permutation, disjoint fresh mask bytes. Reverse routing
            // restores block positions without opening tags on the original order.
            co_await blockPerm.apply<u8>(PermOp::Inverse,back.mData,unshuffled.mData,sock);
            BinMatrix d(n,cw),base(n,cw),index(n,r);
            for(u64 i=0;i<n;++i)
            {
                copy(d.data(i),0,unshuffled.data(i/block),(i%block)*cw,cw);
                copy(base.data(i),0,coarseCounts.data(i/block),0,cw);
                if(!role)integer(index.data(i),0,i,r);
            }
            co_await evaluate(*yRanks,role,{&d,&base,&index},{&yr},sock);yRanks.reset();
        }
        corrections={};coarseCounts={};blockPerm={};record(2);
        {
            BinMatrix ranks(m+n,r),flags(m+n,1);
            oc::Matrix<u32> payload(m+n,1);
            for(u64 i=0;i<m+n;++i)
            {
                copy(ranks.data(i),0,i<m?xr.data(i):yr.data(i-m),0,r);
                flags(i,0)=!role;payload(i,0)=role?0:i;
            }
            co_await invert.applyRanked(flags,ranks,payload,out,sock);
        }
        record(3);
    }

    RootMerge::RootMerge(RootMergeKind kind):mImpl(std::make_unique<Impl>(kind)){}
    RootMerge::~RootMerge()=default;
    RootMerge::RootMerge(RootMerge&&) noexcept=default;
    RootMerge& RootMerge::operator=(RootMerge&&) noexcept=default;
    void RootMerge::init(u64 m,u64 n,u64 bits,CorGenerator& cor,u64 block){mImpl->init(m,n,bits,cor,block);}
    void RootMerge::preprocess(){mImpl->preprocess();}
    macoro::task<> RootMerge::prepare(coproto::Socket& sock,PRNG& prng){return mImpl->prepare(sock,prng);}
    macoro::task<> RootMerge::merge(const BinMatrix& x,const BinMatrix& y,AdditivePerm& out,coproto::Socket& sock,PRNG& prng)
    {return mImpl->run(x,y,out,sock,prng);}
    const std::vector<PiLogStarStage>& RootMerge::stages()const{return mImpl->stats;}
    u64 RootMerge::blockSize()const{return mImpl->block;}
    u64 RootMerge::gmwRounds()const{u64 total=0;for(auto& s:mImpl->stats)total+=s.gmwRounds;return total;}
    u64 RootMerge::onlineRoundBound()const{return gmwRounds()+(mImpl->kind==RootMergeKind::Batcher?0:9);}
    u64 RootMerge::paddedAnds()const{u64 total=0;for(auto& s:mImpl->stats)total+=s.paddedAnds;return total;}
    u64 RootMerge::comparisons()const{return mImpl->comparisonCount;}
}
