#include "RootMerge.h"
#include "MergeInternal.h"
#include "BatcherMerge.h"
#include "secure-join/AggTree/BatchPrefix.h"
#include "secure-join/Perm/AltModComposedPerm.h"
#include "macoro/when_all.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace secJoin
{
    namespace
    {
        using namespace merge_detail;
        // All ranks are active and labels 0..N-1 are public. Shuffle labels
        // during preparation, then shuffle only ranks online with disjoint masks.
        struct RankInverse
        {
            AltModComposedPerm gen;
            ComposedPerm permutation;
            BinMatrix labels;
            u64 rows=0,bits=0,role=0;
            void init(u64 n,u64 w,CorGenerator& cor)
            {rows=n;bits=w;role=cor.partyIdx();gen.init(role,n,2*oc::divCeil(w,8),cor);}
            void preprocess(){gen.preprocess();}
            macoro::task<> prepare(coproto::Socket& sock,PRNG& prng)
            {
                co_await gen.generate(sock,prng,rows,permutation);
                BinMatrix input(rows,bits);labels.resize(rows,bits);
                if(!role)for(u64 i=0;i<rows;++i)integer(input.data(i),0,i,bits);
                co_await permutation.apply<u8>(PermOp::Regular,input.mData,labels.mData,sock);
            }
            macoro::task<> apply(const BinMatrix& ranks,AdditivePerm& output,coproto::Socket& sock)
            {
                BinMatrix shuffled(rows,bits);
                co_await permutation.apply<u8>(PermOp::Regular,
                    {ranks.data(),rows,ranks.bytesPerEntry()},shuffled.mData,sock);
                auto opened=co_await openPacked(shuffled,bits,sock);
                output.mShare.resize(rows);std::vector<bool> seen(rows);
                for(u64 i=0;i<rows;++i)
                {
                    auto rank=integer(opened.data(),i*bits,bits);
                    if(rank>=rows||seen[rank])throw std::runtime_error("RootMerge invalid shuffled rank");
                    seen[rank]=true;output.mShare[rank]=integer(labels.data(i),0,bits);
                }
                labels={};permutation={};
            }
        };

        // Within each copied block, a_i = [X_i <= Y_t] is a unary prefix.
        // rank_i = (a_i AND continuation_{i+1}) ? rank_{i+1} : i+a_i.
        // i+a_i selects between PUBLIC integers locally. Reversed segmented
        // broadcast therefore replaces every carry-propagating suffix sum.
        struct SuffixRank
        {
            BatchPrefix scan;
            std::unique_ptr<Gmw> links;
            u64 rows=0,leaves=0,fields=0,bits=0,role=0,numRounds=0,numAnds=0;
            void init(u64 m,u64 l,u64 f,u64 w,CorGenerator& cor)
            {
                rows=m;leaves=l;fields=f;bits=w;role=cor.partyIdx();
                if(m>1)
                {
                    links=std::make_unique<Gmw>();links->init(product(m-1,f),scalarAnd(),cor);
                    numRounds=rounds(*links);numAnds=ands(*links);
                }
                scan.init(f,l,w,cor,8);
                numRounds+=scan.numRounds();numAnds+=scan.numAnds();
            }
            void preprocess(){if(links)links->preprocess();scan.preprocess();}
            macoro::task<> apply(const BinMatrix& less,const BinMatrix& ctrl,BinMatrix& out,coproto::Socket& sock)
            {
                BinMatrix work(fields*leaves,bits),controls(fields*leaves,1),result;
                for(u64 i=0;i<rows;++i)for(u64 t=0;t<fields;++t)
                {
                    auto a=(less(i*fields+t,0)^u8(!role))&1;
                    auto value=(role?0:i)^((u64(0)-u64(a))&(i^(i+1)));
                    integer(work.data(t*leaves+leaves-1-i),0,value,bits);
                }
                if(links)
                {
                    auto lanes=(rows-1)*fields;
                    BinMatrix a(lanes,1),b(lanes,1),c(lanes,1);
                    for(u64 i=0;i+1<rows;++i)for(u64 t=0;t<fields;++t)
                    {
                        a(i*fields+t,0)=less(i*fields+t,0)^u8(!role);
                        b(i*fields+t,0)=ctrl(i+1,0);
                    }
                    co_await evaluate(*links,role,{&a,&b},{&c},sock);links.reset();
                    for(u64 i=0;i+1<rows;++i)for(u64 t=0;t<fields;++t)
                        controls(t*leaves+leaves-1-i,0)=c(i*fields+t,0);
                }
                co_await scan.apply(work,controls,result,sock);
                out.resize(rows*fields,bits);
                for(u64 i=0;i<rows;++i)for(u64 t=0;t<fields;++t)
                    std::copy(result[t*leaves+leaves-1-i].begin(),result[t*leaves+leaves-1-i].end(),out[i*fields+t].begin());
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
        std::unique_ptr<Gmw> boundary,tagProducts,detail;
        PublicIndexAdd xRanks,yRanks;
        AltModComposedPerm blockGen;
        ComposedPerm blockPerm;
        BatchPrefix broadcast;
        SuffixRank suffix;
        RankInverse invert;
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
            // Record: tag, continuation, coarse count, then padded keys.
            recordBits=product(block,bits+1)+cw+1+cw;
            forwardBytes=oc::divCeil(recordBits,8);backBytes=oc::divCeil(product(block,cw),8);
            if(recordBits>std::numeric_limits<u32>::max()/8)throw std::overflow_error("RootMerge block exceeds circuit dimensions");
            blockGen.init(role,blocks+m,forwardBytes+backBytes,cor);
            invert.init(m+n,r,cor);
            auto q=product(m,blocks-1);
            if(q)boundary=gate(q,comparison(bits),cor);
            if(m>1)tagProducts=gate(product(m-1,blocks),scalarAnd(),cor);
            auto detailRows=product(m,block);
            if(cube)detailRows=product(product(m,m+1)/2,block);
            // Infinity padding already makes every dummy comparison false.
            detail=gate(detailRows,comparison(bits+1),cor);
            comparisonCount=q+detailRows;
            offsetBits=width(block+1);
            if(!cube)
            {
                broadcast.init(1,leaves,recordBits,cor,8);
                suffix.init(m,leaves,block,cw,cor);
            }
            xRanks.init(m,width(n+1),r,cor);
            yRanks.init(n,cw,r,cor);
            stats={{"boundary_comparisons_and_tags",1,n,block},
                   {"selected_blocks_and_detail_comparisons",1,n,block},
                   {"rank_recovery",1,n,block},
                   {"inverse_permutation",1,m+n,0}};
            add(0,boundary);add(0,tagProducts);
            add(1,detail);
            stats[1].gmwRounds+=broadcast.numRounds();stats[1].paddedAnds+=broadcast.numAnds();
            stats[2].gmwRounds+=xRanks.numRounds+yRanks.numRounds;
            stats[2].paddedAnds+=xRanks.numAnds+yRanks.numAnds;
            stats[2].gmwRounds+=suffix.numRounds;stats[2].paddedAnds+=suffix.numAnds;
            initialized=true;
        }
        void preprocess()
        {
            if(!initialized||preprocessed)throw std::logic_error("RootMerge preprocess state");
            if(kind==RootMergeKind::Batcher)batcher.preprocess();
            else
            {
                for(auto* g:{&boundary,&tagProducts,&detail})if(*g)(*g)->preprocess();
                xRanks.preprocess();yRanks.preprocess();
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
        const u64 coarseOff=cw+1,keyOff=coarseOff+cw;
        // Cross comparisons against every block maximum except the last.
        // Force the final comparison to zero so the last block also receives
        // keys above Y's maximum; their within-block insertion offset is its length.
        BinMatrix boundaryBits(m,blocks),maps(m,blocks),first(m,1),target(m,idw);
        if(boundary)
        {
            auto lanes=m*(blocks-1);
            BinMatrix a(lanes,bits),b(lanes,bits),q(lanes,1);
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
        BinMatrix products(m*blocks,1);
        for(u64 j=0;j<blocks;++j)products(j,0)=bit(maps.data(0),j);
        if(tagProducts)
        {
            // The map is one-hot. A newly occupied block directly identifies
            // its first X, avoiding a separate equality and tag-masking layer.
            BinMatrix a((m-1)*blocks,1),b((m-1)*blocks,1),q((m-1)*blocks,1);
            for(u64 i=1;i<m;++i)for(u64 j=0;j<blocks;++j)
            {a((i-1)*blocks+j,0)=bit(maps.data(i),j);b((i-1)*blocks+j,0)=bit(maps.data(i-1),j)^u8(!role);}
            co_await evaluate(*tagProducts,role,{&a,&b},{&q},sock);tagProducts.reset();
            for(u64 i=1;i<m;++i)for(u64 j=0;j<blocks;++j)
            {products(i*blocks+j,0)=q((i-1)*blocks+j,0);first(i,0)^=q((i-1)*blocks+j,0)&1;}
        }
        BinMatrix coarseCounts(blocks,cw);
        for(u64 j=0;j<blocks;++j)
            integer(coarseCounts.data(j),0,unaryCount(m,[&](u64 i){return bit(boundaryBits.data(i),j)^u8(!role);}),cw);
        boundaryBits={};maps={};
        record(0);

        BinMatrix selected(leaves,recordBits),control(leaves,1);
        std::vector<u32> positions(m);
        {
            BinMatrix records(blocks+m,recordBits),shuffled(blocks+m,recordBits);
            for(u64 j=0;j<blocks;++j)
            {
                u64 tag=0;
                for(u64 i=0;i<m;++i)if(products(i*blocks+j,0)&1)tag^=i+1;
                integer(records.data(j),0,tag,cw);
                copy(records.data(j),coarseOff,coarseCounts.data(j),0,cw);
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
                    for(u64 t=0;t<block;++t)put(records.data(row),keyOff+t*(bits+1)+bits,1);
                }
            }
            co_await blockPerm.apply<u8>(PermOp::Regular,records.mData,shuffled.mData,sock);
            auto opened=co_await openPacked(shuffled,cw,sock);
            std::vector<bool> seen(m);
            for(u64 j=0;j<blocks+m;++j)
            {
                auto tag=integer(opened.data(),j*cw,cw);
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
        const u64 detailRows=(cube?m*(m+1)/2:m)*block;
        BinMatrix q(detailRows,1),offsets(m,offsetBits);
        {
            BinMatrix a(detailRows,bits+1),b(detailRows,bits+1);
            for(u64 i=0;i<m;++i)for(u64 j=0;j<(cube?i+1:1);++j)for(u64 t=0;t<block;++t)
            {
                auto row=(cube?i*(i+1)/2+j:i)*block+t,slot=cube?j:i;
                copy(a.data(row),0,values.data(slot),keyOff+t*(bits+1),bits+1);
                copy(b.data(row),0,x.data(i),0,bits);
            }
            co_await evaluate(*detail,role,{&a,&b},{&q},sock);
            detail.reset();
        }
        if(cube)
        {
            u8 groupParity=0;
            for(u64 i=0;i<m;++i)
            {
                groupParity^=first(i,0)&1;
                u64 low=0;u8 fullParity=0;
                for(u64 j=0;j<=i;++j)
                {
                    auto begin=(i*(i+1)/2+j)*block;
                    low^=unaryCount(block,[&](u64 t){return q(begin+t,0);})&(block-1);
                    fullParity^=q(begin+block-1,0)&1;
                }
                // Full selected blocks = groups_so_far - 1 + own_block_full.
                // Their parities recover the one-bit difference without sums.
                auto ownFull=fullParity^groupParity^u8(!role);
                integer(offsets.data(i),0,low|(u64(ownFull)<<shift),offsetBits);
            }
        }
        else for(u64 i=0;i<m;++i)
            integer(offsets.data(i),0,unaryCount(block,[&](u64 t){return q(i*block+t,0);}),offsetBits);
        first={};record(1);
        BinMatrix xr(m,r),yr(n,r),fine(m*block,cw);
        BinMatrix xCounts(m,xRanks.bits);
        {
            for(u64 i=0;i<m;++i)
            {
                auto offset=integer(offsets.data(i),0,offsetBits),id=integer(target.data(i),0,idw);
                // A full target is possible only at the forced last block.
                // Its ID increment is therefore a selection of public constants.
                id^=(u64(0)-(offset>>shift))&((blocks-1)^blocks);
                integer(xCounts.data(i),0,(id<<shift)|(offset&(block-1)),xRanks.bits);
            }
        }
        offsets={};target={};
        // X rank addition is independent of suffix recovery and inverse routing.
        // A separate logical channel overlaps their interactive layers.
        auto recoverY=[&]() -> macoro::task<>
        {
            if(cube)
            {
                for(u64 j=0;j<m;++j)for(u64 t=0;t<block;++t)
                    integer(fine.data(j*block+t),0,unaryCount(m,[&](u64 i){
                        return (i<j?u8(0):q((i*(i+1)/2+j)*block+t,0))^u8(!role);}),cw);
            }
            else co_await suffix.apply(q,control,fine,sock);
            q={};selected={};control={};
            BinMatrix corrections(m*block,cw);
            for(u64 i=0;i<m;++i)for(u64 t=0;t<block;++t)
                integer(corrections.data(i*block+t),0,
                    integer(fine.data(i*block+t),0,cw)^integer(values.data(i),coarseOff,cw),cw);
            fine={};values={};
            {
                BinMatrix back(blocks+m,block*cw),unshuffled(blocks+m,block*cw);
                for(u64 i=0;i<m;++i)for(u64 t=0;t<block;++t)
                    copy(back.data(positions[i]),t*cw,corrections.data(i*block+t),0,cw);
                // Same secret permutation, disjoint fresh mask bytes. Reverse routing
                // restores block positions without opening tags on the original order.
                co_await blockPerm.apply<u8>(PermOp::Inverse,back.mData,unshuffled.mData,sock);
                BinMatrix counts(n,cw);
                for(u64 i=0;i<n;++i)
                    integer(counts.data(i),0,integer(unshuffled.data(i/block),(i%block)*cw,cw)^integer(coarseCounts.data(i/block),0,cw),cw);
                co_await yRanks.apply(counts,yr,sock);
            }
            corrections={};coarseCounts={};blockPerm={};
        };
        auto xSocket=sock.fork();
        auto recovered=co_await macoro::when_all_ready(xRanks.apply(xCounts,xr,xSocket),recoverY());
        std::get<0>(recovered).result();std::get<1>(recovered).result();
        xCounts={};record(2);
        {
            BinMatrix ranks(m+n,r);
            for(u64 i=0;i<m+n;++i)
            {
                copy(ranks.data(i),0,i<m?xr.data(i):yr.data(i-m),0,r);
            }
            co_await invert.apply(ranks,out,sock);
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
    u64 RootMerge::onlineRoundBound()const
    {
        if(mImpl->kind==RootMergeKind::Batcher)return gmwRounds();
        return gmwRounds()+8-std::min(mImpl->xRanks.numRounds,
            mImpl->suffix.numRounds+mImpl->yRanks.numRounds+2);
    }
    u64 RootMerge::paddedAnds()const{u64 total=0;for(auto& s:mImpl->stats)total+=s.paddedAnds;return total;}
    u64 RootMerge::comparisons()const{return mImpl->comparisonCount;}
    u64 RootMerge::rankAdderResidues()const{return mImpl->yRanks.period;}
}
