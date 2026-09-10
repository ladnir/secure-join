#include "PiMedian.h"
#include "MergeInternal.h"
#include "StableSecretExtract.h"
#include "secure-join/Perm/AltModComposedPerm.h"
#include <chrono>
#include <cstring>

namespace secJoin
{
    namespace
    {
        using namespace merge_detail;
        bool isPower(u64 n) { return n && !(n&(n-1)); }
        u64 log2Exact(u64 n) { u64 w=0; while(n>1){n>>=1;++w;}return w; }

        // Batch-wide shuffle of (scatter rank, payload), followed by opening
        // only ranks. Their multiset is exactly [0,rows), independent of keys.
        struct Scatter
        {
            u64 rows=0,rankBits=0,payloadBits=0;
            AltModComposedPerm gen;
            ComposedPerm perm;
            void init(u64 n,u64 bits,CorGenerator& cor)
            {rows=n;rankBits=width(n);payloadBits=bits;gen.init(cor.partyIdx(),n,oc::divCeil(rankBits+bits,8),cor);}
            void preprocess(){gen.preprocess();}
            macoro::task<> prepare(coproto::Socket& sock,PRNG& prng)
            {co_await gen.generate(sock,prng,rows,perm);}
            macoro::task<> apply(BinMatrix ranks,BinMatrix payload,BinMatrix& out,coproto::Socket& sock,
                u64 halfSize=0,u64 childSize=0)
            {
                BinMatrix records(rows,rankBits+payloadBits),shuffled(rows,rankBits+payloadBits);
                for(u64 i=0;i<rows;++i)
                {
                    copy(records.data(i),0,ranks.data(i),0,rankBits);
                    copy(records.data(i),rankBits,payload.data(i),0,payloadBits);
                }
                ranks={};payload={};
                co_await perm.apply<u8>(PermOp::Regular,records.mData,shuffled.mData,sock);
                records={};perm={};
                auto opened=co_await openPacked(shuffled,rankBits,sock);
                out.resize(rows,payloadBits);std::vector<bool> seen(rows);
                for(u64 i=0;i<rows;++i)
                {
                    auto rank=integer(opened.data(),i*rankBits,rankBits);
                    if(rank>=rows||seen[rank])throw std::runtime_error("PiMedian: invalid shuffled scatter ranks");
                    seen[rank]=true;
                    // Fuse the public regrouping with scatter: corresponding
                    // blocks of the two alignments become the next run pair.
                    // This bijection saves a full expanded-row buffer and copy.
                    auto destination=rank;
                    if(childSize)
                    {
                        auto position=rank%(2*halfSize),side=(rank/(2*halfSize))%2;
                        destination=(rank/(4*halfSize))*4*halfSize+
                            (position/childSize)*2*childSize+side*childSize+position%childSize;
                    }
                    copy(out.data(destination),0,shuffled.data(i),rankBits,payloadBits);
                }
            }
            u64 shuffleBytes() const {return 2*rows*oc::divCeil(rankBits+payloadBits,8);}
            u64 openingBytes() const {return 2*oc::divCeil(rows*rankBits,8);}
        };

        // The two-pass asymmetric cube construction, SIMD-batched over every
        // alignment at a recursion level. Return counts, avoiding the root
        // API's rank additions and gather-permutation inversion entirely.
        struct CubeCounts
        {
            u64 batches=0,k=0,n=0,bits=0,block=0,blocks=0,cw=0,tw=0,iw=0,shift=0,role=0;
            u64 recordBits=0,keyOff=0,rows=0,detailRows=0,numRounds=0,numAnds=0,numComparisons=0;
            u64 shuffleBytes=0,openingBytes=0;
            std::unique_ptr<Gmw> boundary,tags,detail;
            AltModComposedPerm gen;
            ComposedPerm perm;
            void init(u64 b,u64 small,u64 large,u64 keyBits,u64 blockSize,CorGenerator& cor)
            {
                batches=b;k=small;n=large;bits=keyBits;block=blockSize;blocks=n/block;
                cw=width(k+1);tw=width(b*k+1);iw=width(blocks);shift=log2Exact(block);role=cor.partyIdx();
                rows=b*(blocks+k);keyOff=tw+cw;recordBits=keyOff+block*(bits+1);
                auto make=[&](u64 lanes,BetaCircuit c){auto g=std::make_unique<Gmw>();g->init(lanes,c,cor);numRounds+=rounds(*g);numAnds+=ands(*g);return g;};
                if(blocks>1)boundary=make(b*k*(blocks-1),comparison(bits));
                if(k>1)tags=make(b*(k-1)*blocks,scalarAnd());
                detailRows=b*k*(k+1)/2*block;detail=make(detailRows,comparison(bits+1));
                numComparisons=b*k*(blocks-1)+detailRows;
                auto forward=oc::divCeil(recordBits,8),back=oc::divCeil(block*cw,8);
                gen.init(role,rows,forward+back,cor);
                shuffleBytes=2*rows*(forward+back);openingBytes=2*oc::divCeil(rows*tw,8);
            }
            void preprocess(){if(boundary)boundary->preprocess();if(tags)tags->preprocess();detail->preprocess();gen.preprocess();}
            macoro::task<> prepare(coproto::Socket& sock,PRNG& prng){co_await gen.generate(sock,prng,rows,perm);}
            template<class OnX,class OnY>
            macoro::task<> apply(const BinMatrix& x,const BinMatrix& y,coproto::Socket& sock,OnX onX,OnY onY)
            {
                BinMatrix bounds(batches*k,blocks),maps(batches*k,blocks),first(batches*k,1),target(batches*k,iw);
                if(boundary)
                {
                    auto lanes=batches*k*(blocks-1);BinMatrix a(lanes,bits),b(lanes,bits),q(lanes,1);
                    for(u64 h=0;h<batches;++h)for(u64 i=0;i<k;++i)for(u64 j=0;j+1<blocks;++j)
                    {
                        auto r=(h*k+i)*(blocks-1)+j;
                        copy(a.data(r),0,y.data(h*n+(j+1)*block-1),0,bits);
                        copy(b.data(r),0,x.data(h*k+i),0,bits);
                    }
                    co_await evaluate(*boundary,role,{&a,&b},{&q},sock);boundary.reset();
                    for(u64 i=0;i<batches*k;++i)for(u64 j=0;j+1<blocks;++j)put(bounds.data(i),j,q(i*(blocks-1)+j,0));
                }
                for(u64 i=0;i<batches*k;++i)for(u64 j=0;j<blocks;++j)
                {
                    auto v=(j?bit(bounds.data(i),j-1):u8(!role))^bit(bounds.data(i),j);
                    put(maps.data(i),j,v);
                    for(u64 t=0;t<iw;++t)if((j>>t)&1)target(i,t/8)^=v<<(t%8);
                }
                BinMatrix products(batches*k*blocks,1),coarse(batches*blocks,cw);
                for(u64 h=0;h<batches;++h)
                {
                    first(h*k,0)=!role;
                    for(u64 j=0;j<blocks;++j)
                    {
                        products(h*k*blocks+j,0)=bit(maps.data(h*k),j);
                        integer(coarse.data(h*blocks+j),0,unaryCount(k,[&](u64 i){return bit(bounds.data(h*k+i),j)^u8(!role);}),cw);
                    }
                }
                bounds={};
                if(tags)
                {
                    auto lanes=batches*(k-1)*blocks;BinMatrix a(lanes,1),b(lanes,1),q(lanes,1);
                    for(u64 h=0;h<batches;++h)for(u64 i=1;i<k;++i)for(u64 j=0;j<blocks;++j)
                    {
                        auto r=(h*(k-1)+i-1)*blocks+j;
                        a(r,0)=bit(maps.data(h*k+i),j);b(r,0)=bit(maps.data(h*k+i-1),j)^u8(!role);
                    }
                    co_await evaluate(*tags,role,{&a,&b},{&q},sock);tags.reset();
                    for(u64 h=0;h<batches;++h)for(u64 i=1;i<k;++i)for(u64 j=0;j<blocks;++j)
                    {
                        auto v=q((h*(k-1)+i-1)*blocks+j,0)&1;
                        products((h*k+i)*blocks+j,0)=v;first(h*k+i,0)^=v;
                    }
                }
                maps={};
                BinMatrix selected(batches*k,recordBits);std::vector<u32> positions(batches*k);
                {
                    BinMatrix records(rows,recordBits),shuffled(rows,recordBits);
                    for(u64 h=0;h<batches;++h)
                    {
                        for(u64 j=0;j<blocks;++j)
                        {
                            auto r=h*(blocks+k)+j;u64 tag=0;
                            for(u64 i=0;i<k;++i)if(products((h*k+i)*blocks+j,0)&1)tag^=h*k+i+1;
                            integer(records.data(r),0,tag,tw);copy(records.data(r),tw,coarse.data(h*blocks+j),0,cw);
                            for(u64 t=0;t<block;++t)copy(records.data(r),keyOff+t*(bits+1),y.data(h*n+j*block+t),0,bits);
                        }
                        for(u64 i=0;i<k;++i)
                        {
                            auto r=h*(blocks+k)+blocks+i;
                            if((first(h*k+i,0)^u8(!role))&1)integer(records.data(r),0,h*k+i+1,tw);
                            if(!role)for(u64 t=0;t<block;++t)put(records.data(r),keyOff+t*(bits+1)+bits,1);
                        }
                    }
                    co_await perm.apply<u8>(PermOp::Regular,records.mData,shuffled.mData,sock);
                    auto opened=co_await openPacked(shuffled,tw,sock);std::vector<bool> seen(batches*k);
                    for(u64 r=0;r<rows;++r)
                    {
                        auto tag=integer(opened.data(),r*tw,tw);if(!tag)continue;
                        if(tag>batches*k||seen[tag-1])throw std::runtime_error("PiMedian: invalid shuffled block tags");
                        seen[tag-1]=true;positions[tag-1]=r;
                        std::copy(shuffled[r].begin(),shuffled[r].end(),selected[tag-1].begin());
                    }
                    if(std::find(seen.begin(),seen.end(),false)!=seen.end())throw std::runtime_error("PiMedian: missing shuffled block tag");
                }
                products={};
                BinMatrix q(detailRows,1);
                auto detailIndex=[&](u64 h,u64 i,u64 j,u64 t){return (h*k*(k+1)/2+i*(i+1)/2+j)*block+t;};
                {
                    BinMatrix a(detailRows,bits+1),b(detailRows,bits+1);
                    for(u64 h=0;h<batches;++h)for(u64 i=0;i<k;++i)for(u64 j=0;j<=i;++j)for(u64 t=0;t<block;++t)
                    {
                        auto r=detailIndex(h,i,j,t);
                        copy(a.data(r),0,selected.data(h*k+j),keyOff+t*(bits+1),bits+1);
                        copy(b.data(r),0,x.data(h*k+i),0,bits);
                    }
                    co_await evaluate(*detail,role,{&a,&b},{&q},sock);detail.reset();
                }
                BinMatrix xc(batches*k,width(n+1));
                for(u64 h=0;h<batches;++h)
                {
                    u8 groupParity=0;
                    for(u64 i=0;i<k;++i)
                    {
                        groupParity^=first(h*k+i,0)&1;u64 low=0;u8 full=0;
                        for(u64 j=0;j<=i;++j)
                        {
                            low^=unaryCount(block,[&](u64 t){return q(detailIndex(h,i,j,t),0);})&(block-1);
                            full^=q(detailIndex(h,i,j,block-1),0)&1;
                        }
                        auto ownFull=full^groupParity^u8(!role);
                        auto id=integer(target.data(h*k+i),0,iw)^((u64(0)-u64(ownFull))&((blocks-1)^blocks));
                        integer(xc.data(h*k+i),0,(id<<shift)|low,xc.bitsPerEntry());
                    }
                }
                first={};target={};
                // Return only count corrections through disjoint fresh masks
                // of the same hidden permutation. Keys never travel backwards.
                auto recoverY=[&]() -> macoro::task<>
                {
                    BinMatrix back(rows,block*cw),restored(rows,block*cw);
                    for(u64 h=0;h<batches;++h)for(u64 j=0;j<k;++j)for(u64 t=0;t<block;++t)
                    {
                        auto fine=unaryCount(k,[&](u64 i){return (i<j?u8(0):q(detailIndex(h,i,j,t),0))^u8(!role);});
                        auto correction=fine^integer(selected.data(h*k+j),tw,cw);
                        integer(back.data(positions[h*k+j]),t*cw,correction,cw);
                    }
                    q={};selected={};
                    co_await perm.apply<u8>(PermOp::Inverse,back.mData,restored.mData,sock);perm={};
                    BinMatrix yc(batches*n,cw);
                    for(u64 h=0;h<batches;++h)for(u64 j=0;j<n;++j)
                        integer(yc.data(h*n+j),0,integer(restored.data(h*(blocks+k)+j/block),(j%block)*cw,cw)^integer(coarse.data(h*blocks+j/block),0,cw),cw);
                    back={};restored={};coarse={};
                    co_await onY(yc,sock);
                };
                // X counts are ready two routing steps before Y counts. Let
                // rank construction consume them while inverse routing runs.
                auto xSocket=sock.fork();
                auto done=co_await macoro::when_all_ready(onX(xc,xSocket),recoverY());
                std::get<0>(done).result();std::get<1>(done).result();
            }
        };

        struct PairCounts
        {
            u64 batches=0,n=0,bits=0,keyOffset=0,role=0,numRounds=0,numAnds=0;
            bool packed=false;
            Gmw compare;
            void init(u64 b,u64 size,u64 keyBits,u64 offset,CorGenerator& cor)
            {
                batches=b;n=size;bits=keyBits;keyOffset=offset;role=cor.partyIdx();
                packed=n<=64 && oc::roundUpTo(b,128)*n*n==oc::roundUpTo(b*n*n,128);
                if(packed)
                {
                    // One SIMD lane per leaf pair. Reuse its 2n keys inside
                    // the circuit instead of materializing 2n^2 input keys.
                    Circuit c;auto input=c.input(2*n*bits),out=c.output(2*n*width(n+1));
                    std::vector<BetaBundle> keys(2*n);
                    for(u64 i=0;i<2*n;++i)keys[i].mWires.assign(input.mWires.begin()+i*bits,input.mWires.begin()+(i+1)*bits);
                    std::vector<u32> q(n*n);
                    for(u64 i=0;i<n;++i)for(u64 j=0;j<n;++j)q[i*n+j]=c.lessThan(keys[n+j],keys[i]);
                    auto w=width(n+1);
                    for(u64 side=0;side<2;++side)for(u64 i=0;i<n;++i)
                    {
                        std::vector<u32> count(w,c.constants[0]);
                        for(u64 j=0;j<n;++j)
                        {
                            auto a=side?c.neg(q[j*n+i]):q[i*n+j];
                            auto b=j+1==n?c.constants[0]:(side?c.neg(q[(j+1)*n+i]):q[i*n+j+1]);
                            auto boundary=c.gate(a,b,oc::GateType::Xor);
                            for(u64 t=0;t<w;++t)if(((j+1)>>t)&1)count[t]=c.gate(count[t],boundary,oc::GateType::Xor);
                        }
                        for(u64 t=0;t<w;++t)c.save(count[t],out[(side*n+i)*w+t]);
                    }
                    compare.init(b,c.finish(),cor);
                }
                else compare.init(b*n*n,comparison(bits),cor);
                numRounds=rounds(compare);numAnds=ands(compare);
            }
            void preprocess(){compare.preprocess();}
            macoro::task<> apply(const BinMatrix& input,BinMatrix& counts,coproto::Socket& sock)
            {
                if(packed)
                {
                    auto w=width(n+1);BinMatrix keys(batches,2*n*bits),result(batches,2*n*w);
                    for(u64 h=0;h<batches;++h)for(u64 i=0;i<2*n;++i)copy(keys.data(h),i*bits,input.data(h*2*n+i),keyOffset,bits);
                    co_await evaluate(compare,role,{&keys},{&result},sock);counts.resize(2*batches*n,w);
                    for(u64 h=0;h<batches;++h)for(u64 i=0;i<2*n;++i)copy(counts.data(h*2*n+i),0,result.data(h),i*w,w);
                    co_return;
                }
                auto lanes=batches*n*n;BinMatrix a(lanes,bits),b(lanes,bits),q(lanes,1);
                for(u64 h=0;h<batches;++h)for(u64 i=0;i<n;++i)for(u64 j=0;j<n;++j)
                {
                    auto r=(h*n+i)*n+j;
                    copy(a.data(r),0,input.data(h*2*n+n+j),keyOffset,bits);copy(b.data(r),0,input.data(h*2*n+i),keyOffset,bits);
                }
                co_await evaluate(compare,role,{&a,&b},{&q},sock);a={};b={};counts.resize(2*batches*n,width(n+1));
                for(u64 h=0;h<batches;++h)
                {
                    for(u64 i=0;i<n;++i)integer(counts.data(h*2*n+i),0,unaryCount(n,[&](u64 j){return q((h*n+i)*n+j,0);}),counts.bitsPerEntry());
                    for(u64 j=0;j<n;++j)integer(counts.data(h*2*n+n+j),0,unaryCount(n,[&](u64 i){return q((h*n+i)*n+j,0)^u8(!role);}),counts.bitsPerEntry());
                }
            }
        };
    }

    struct PiMedian::Impl
    {
        struct Level
        {
            PiMedianLevel plan;
            CubeCounts counts;
            PublicIndexAdd xAdd,yAdd;
            Scatter scatter;
        };
        u64 n=0,padded=0,keyBits=0,indexBits=0,orderBits=0,compareBits=0,compareOffset=0,rowBits=0,role=0,expanded=0,leafN=0,leafBatches=0;
        u64 leafOrderBits=0,leafRowBits=0,leafPayloadOffset=0;
        bool initialized=false,preprocessed=false,prepareStarted=false,prepared=false,used=false,needsCompact=false;
        PiMedianLeaf leafKind=PiMedianLeaf::AllPairs;
        std::vector<std::unique_ptr<Level>> work;
        std::vector<PiMedianLevel> plans;
        std::vector<PiLogStarStage> stats;
        PairCounts pairs;
        PublicIndexAdd leafAdd;
        Scatter leafScatter;
        BatcherMerge batcher;
        StableSecretExtract compact;
        u64 totalComparisons=0,totalRounds=0,totalAnds=0,totalBound=0,totalBytes=0;
        void record(u64 s,coproto::Socket& sock,u64 sent,u64 received,std::chrono::steady_clock::time_point start)
        {
            stats[s].sentBytes=sock.bytesSent()-sent;stats[s].receivedBytes=sock.bytesReceived()-received;
            stats[s].milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        }
    };

    PiMedian::PiMedian():m(std::make_unique<Impl>()){}
    PiMedian::~PiMedian()=default;
    PiMedian::PiMedian(PiMedian&&) noexcept=default;
    PiMedian& PiMedian::operator=(PiMedian&&) noexcept=default;

    void PiMedian::init(u64 n,u64 keyBits,CorGenerator& cor,const PiMedianOptions& opts)
    {
        if(m->initialized||!n||n>(u64(1)<<26)||!keyBits||keyBits>256||!isPower(opts.baseCase)||
            !opts.maxExpandedRows||opts.maxExpandedRows>=(u64(1)<<32)||
            (opts.leaf!=PiMedianLeaf::AllPairs&&opts.leaf!=PiMedianLeaf::Batcher&&opts.leaf!=PiMedianLeaf::Bitonic)||
            !cor.initialized()||cor.partyIdx()>1||cor.mGenState->mMock||cor.mGenState->mDebug)
            throw std::invalid_argument("PiMedian: invalid public configuration or insecure correlations");
        // Validate the entire shape before registering any requests. No secret
        // data or runtime timing is involved in choosing this schedule.
        auto padded=power(n);u64 b=1,size=padded,depth=0;
        std::vector<PiMedianLevel> shapes;
        while(size>opts.baseCase && (!opts.maxDepth||depth<opts.maxDepth))
        {
            auto child=depth<opts.childSizes.size()?opts.childSizes[depth]:0;
            if(!child)child=u64(1)<<(2*log2Exact(size)/3);
            if(!isPower(child)||child>=size||size%child)throw std::invalid_argument("PiMedian: child must be a power of two dividing and smaller than its parent");
            auto k=size/child,block=depth<opts.cubeBlocks.size()?opts.cubeBlocks[depth]:0;
            if(!block)block=k;
            if(!isPower(block)||block>size)throw std::invalid_argument("PiMedian: cube block must be a power of two no greater than its run");
            if(product(4*b,size)>opts.maxExpandedRows)throw std::invalid_argument("PiMedian: maxExpandedRows exceeded");
            // GMW lane and block bit dimensions are bounded before allocation.
            if(product(product(2*b,k*(k+1)/2),block)>=(u64(1)<<32)||product(block,keyBits+width(2*padded)+3)>std::numeric_limits<u32>::max()/8)
                throw std::invalid_argument("PiMedian: asymmetric circuit dimensions too large");
            shapes.push_back({b,size,child,k,block});b*=2*k;size=child;++depth;
        }
        if(opts.childSizes.size()>depth||opts.cubeBlocks.size()>depth)throw std::invalid_argument("PiMedian: unused per-level overrides");
        auto expanded=2*b*size;
        if(expanded>opts.maxExpandedRows||expanded>=(u64(1)<<32)||
            (opts.leaf==PiMedianLeaf::AllPairs&&product(b,size*size)>=(u64(1)<<32)))
            throw std::invalid_argument("PiMedian: terminal dimensions exceed resource limits");
        m->n=n;m->padded=padded;m->keyBits=keyBits;m->role=cor.partyIdx();m->indexBits=width(2*padded);
        m->orderBits=keyBits+m->indexBits+2;m->rowBits=m->orderBits+1;m->expanded=expanded;
        m->compareOffset=opts.fullIndexComparisons?0:m->indexBits;
        m->compareBits=m->orderBits-m->compareOffset;
        m->leafN=size;m->leafBatches=b;m->leafKind=opts.leaf;m->needsCompact=!shapes.empty()||n!=padded;
        for(auto shape:shapes)
        {
            auto level=std::make_unique<Impl::Level>();auto& l=*level;l.plan=shape;
            auto alignments=2*shape.batches,rows=alignments*shape.halfSize;
            l.counts.init(alignments,shape.medians,shape.halfSize,m->compareBits,shape.cubeBlock,cor);
            l.xAdd.init(rows,width(shape.halfSize+1),width(2*shape.halfSize),cor,shape.halfSize);
            // Multiplication by childSize is a bit shift. Only the high word
            // of a long-list rank needs an addition; low offset bits are public.
            l.yAdd.init(rows,width(shape.medians+1),width(2*shape.medians),cor,shape.medians);
            l.scatter.init(2*rows,m->rowBits,cor);
            l.plan.comparisons=l.counts.numComparisons;l.plan.gmwRounds=l.counts.numRounds+l.xAdd.numRounds+l.yAdd.numRounds;
            l.plan.paddedAnds=l.counts.numAnds+l.xAdd.numAnds+l.yAdd.numAnds;
            // X addition overlaps the two inverse-routing steps and Y addition.
            l.plan.onlineRoundBound=l.counts.numRounds+6+std::max(l.xAdd.numRounds,2+l.yAdd.numRounds);
            l.plan.shufflePayloadBytes=l.counts.shuffleBytes+l.scatter.shuffleBytes();
            l.plan.openingPayloadBytes=l.counts.openingBytes+l.scatter.openingBytes();
            m->totalComparisons+=l.plan.comparisons;m->totalBound+=l.plan.onlineRoundBound;
            m->totalBytes+=l.plan.shufflePayloadBytes+l.plan.openingPayloadBytes;
            m->stats.push_back({"median_alignment",shape.batches,shape.halfSize,shape.childSize,l.plan.gmwRounds,l.plan.paddedAnds});
            m->plans.push_back(l.plan);m->work.push_back(std::move(level));
        }
        if(opts.leaf==PiMedianLeaf::AllPairs)
        {
            m->leafOrderBits=m->compareBits;
            m->pairs.init(b,size,m->compareBits,m->compareOffset,cor);m->leafAdd.init(expanded,width(size+1),width(2*size),cor,size);
            m->leafScatter.init(expanded,m->indexBits+1,cor);
            m->totalComparisons+=b*size*size;
            m->stats.push_back({"all_pairs_leaves",b,size,0,m->pairs.numRounds+m->leafAdd.numRounds,m->pairs.numAnds+m->leafAdd.numAnds});
            m->totalBound+=m->pairs.numRounds+m->leafAdd.numRounds+3;
            m->totalBytes+=m->leafScatter.shuffleBytes()+m->leafScatter.openingBytes();
        }
        else
        {
            std::vector<u64> retain;
            if(m->compareOffset)
            {
                // Each leaf input is sorted by (key, source). Public local
                // positions make equal keys strictly ordered for the network,
                // without putting global payload indices in every comparison.
                m->leafOrderBits=width(2*size)+m->compareBits;
                m->leafPayloadOffset=m->leafOrderBits;
                m->leafRowBits=m->leafOrderBits+m->indexBits+1;
                for(u64 i=0;i<=m->indexBits;++i)retain.push_back(m->leafPayloadOffset+i);
            }
            else
            {
                m->leafOrderBits=m->orderBits;m->leafRowBits=m->rowBits;
                for(u64 i=0;i<m->indexBits;++i)retain.push_back(i);
                retain.push_back(m->orderBits);
            }
            m->batcher.init(b,size,m->leafOrderBits,m->leafRowBits,cor,retain,true,
                opts.leaf==PiMedianLeaf::Batcher,m->compareOffset!=0);
            m->stats.push_back({"batcher_leaves",b,size,0,m->batcher.numRounds(),m->batcher.numAnds()});
            m->totalComparisons+=m->batcher.numComparisons();m->totalBound+=m->batcher.numRounds();
        }
        if(m->needsCompact)
        {
            m->compact.init(expanded,2*n,m->indexBits,cor);
            m->stats.push_back({"stable_compaction",1,expanded,0,rounds(m->compact.ranks),ands(m->compact.ranks)});
            m->totalBound+=rounds(m->compact.ranks)+6;
            // OT choice correction + one 32-bit update; shuffle; flags; active u32 ranks.
            m->totalBytes+=4*expanded+oc::divCeil(expanded,8)+2*expanded*oc::divCeil(width(2*n)+m->indexBits+1,8)+2*oc::divCeil(expanded,8)+16*n;
        }
        for(auto& s:m->stats){m->totalRounds+=s.gmwRounds;m->totalAnds+=s.paddedAnds;}
        m->totalBytes+=m->totalAnds/2;m->initialized=true;
    }

    void PiMedian::preprocess()
    {
        if(!m->initialized||m->preprocessed)throw std::logic_error("PiMedian: invalid preprocess state");
        for(auto& l:m->work){l->counts.preprocess();l->xAdd.preprocess();l->yAdd.preprocess();l->scatter.preprocess();}
        if(m->leafKind==PiMedianLeaf::AllPairs){m->pairs.preprocess();m->leafAdd.preprocess();m->leafScatter.preprocess();}else m->batcher.preprocess();
        if(m->needsCompact)m->compact.preprocess();
        m->preprocessed=true;
    }
    macoro::task<> PiMedian::prepare(coproto::Socket& sock,PRNG& prng)
    {
        if(!m->preprocessed||m->prepareStarted)throw std::logic_error("PiMedian: invalid prepare state");
        m->prepareStarted=true;
        for(auto& l:m->work){co_await l->counts.prepare(sock,prng);co_await l->scatter.prepare(sock,prng);}
        if(m->leafKind==PiMedianLeaf::AllPairs)co_await m->leafScatter.prepare(sock,prng);
        if(m->needsCompact)co_await m->compact.prepare(sock,prng);
        m->prepared=true;
    }
    macoro::task<> PiMedian::merge(const BinMatrix& x,const BinMatrix& y,AdditivePerm& output,coproto::Socket& sock,PRNG& prng)
    {
        (void)prng;
        if(!m->prepared||m->used)throw std::logic_error("PiMedian: fresh prepared instance required");
        if(x.rows()!=m->n||y.rows()!=m->n||x.bitsPerEntry()!=m->keyBits||y.bitsPerEntry()!=m->keyBits||
            x.bytesPerEntry()!=oc::divCeil(m->keyBits,8)||y.bytesPerEntry()!=oc::divCeil(m->keyBits,8))
            throw std::invalid_argument("PiMedian: input dimensions differ from configuration");
        m->used=true;BinMatrix rows(2*m->padded,m->rowBits);
        for(u64 side=0;side<2;++side)for(u64 i=0;i<m->padded;++i)
        {
            auto d=rows.data(side*m->padded+i);
            if(i<m->n)copy(d,m->indexBits+1,(side?y:x).data(i),0,m->keyBits);
            if(!m->role)
            {
                integer(d,0,i<m->n?side*m->n+i:side*m->padded+i,m->indexBits);
                put(d,m->indexBits,side);
                put(d,m->orderBits-1,i>=m->n);put(d,m->orderBits,i<m->n);
            }
        }
        u64 stage=0;
        for(auto& ptr:m->work)
        {
            auto start=std::chrono::steady_clock::now();auto sent=sock.bytesSent(),received=sock.bytesReceived();
            auto& l=*ptr;const auto& p=l.plan;auto count=2*p.batches,n=p.halfSize,k=p.medians,child=p.childSize;
            BinMatrix medians(count*k,m->orderBits),compareMedians(count*k,m->compareBits),other(count*n,m->compareBits);
            for(u64 h=0;h<count;++h)
            {
                auto parent=h/2,side=h%2;
                for(u64 i=0;i<k;++i)
                {
                    auto row=rows.data(parent*2*n+side*n+(i+1)*child-1);
                    copy(medians.data(h*k+i),0,row,0,m->orderBits);copy(compareMedians.data(h*k+i),0,row,m->compareOffset,m->compareBits);
                }
                for(u64 i=0;i<n;++i)copy(other.data(h*n+i),0,rows.data(parent*2*n+(side^1)*n+i),m->compareOffset,m->compareBits);
            }
            BinMatrix xr,yr;
            auto addX=[&](const BinMatrix& xc,coproto::Socket& channel) -> macoro::task<>
            {
                BinMatrix counts(count*n,width(n+1));
                for(u64 h=0;h<count;++h)for(u64 i=0;i<n;++i)copy(counts.data(h*n+i),0,xc.data(h*k+i/child),0,counts.bitsPerEntry());
                co_await l.xAdd.apply(counts,xr,channel);
            };
            auto addY=[&](const BinMatrix& yc,coproto::Socket& channel) -> macoro::task<>
            {
                BinMatrix counts(count*n,width(k+1));
                // Transpose long-list rows: a run over public block indices for
                // each within-block offset. This exposes narrow public addends.
                for(u64 h=0;h<count;++h)for(u64 i=0;i<n;++i)copy(counts.data(h*n+(i%child)*k+i/child),0,yc.data(h*n+i),0,counts.bitsPerEntry());
                co_await l.yAdd.apply(counts,yr,channel);
            };
            co_await l.counts.apply(compareMedians,other,sock,addX,addY);other={};compareMedians={};
            auto total=2*count*n;BinMatrix ranks(total,width(total)),payload(total,m->rowBits);
            for(u64 h=0;h<count;++h)for(u64 i=0;i<n;++i)
            {
                auto base=h*2*n;
                auto rx=integer(xr.data(h*n+i),0,xr.bitsPerEntry());
                auto ry=integer(yr.data(h*n+(i%child)*k+i/child),0,yr.bitsPerEntry())*child;
                integer(ranks.data(base+i),0,rx^(m->role?0:base),ranks.bitsPerEntry());
                integer(ranks.data(base+n+i),0,ry^(m->role?0:base+i%child),ranks.bitsPerEntry());
                copy(payload.data(base+i),0,medians.data(h*k+i/child),0,m->orderBits); // inserted dummy flag = 0
                copy(payload.data(base+n+i),0,rows.data((h/2)*2*n+((h%2)^1)*n+i),0,m->rowBits);
            }
            rows={};medians={};xr={};yr={};
            co_await l.scatter.apply(std::move(ranks),std::move(payload),rows,sock,n,child);
            m->record(stage++,sock,sent,received,start);
        }
        auto start=std::chrono::steady_clock::now();auto sent=sock.bytesSent(),received=sock.bytesReceived();
        BinMatrix leaves;
        if(m->leafKind==PiMedianLeaf::AllPairs)
        {
            BinMatrix counts,ranks;co_await m->pairs.apply(rows,counts,sock);co_await m->leafAdd.apply(counts,ranks,sock);counts={};
            BinMatrix global(m->expanded,width(m->expanded)),payload(m->expanded,m->indexBits+1);
            for(u64 i=0;i<m->expanded;++i)
            {
                auto local=integer(ranks.data(i),0,ranks.bitsPerEntry());
                integer(global.data(i),0,local^(m->role?0:(i/(2*m->leafN))*2*m->leafN),global.bitsPerEntry());
                copy(payload.data(i),0,rows.data(i),0,m->indexBits);put(payload.data(i),m->indexBits,bit(rows.data(i),m->orderBits));
            }
            rows={};ranks={};co_await m->leafScatter.apply(std::move(global),std::move(payload),leaves,sock);
        }
        else
        {
            BinMatrix merged;
            if(m->compareOffset)
            {
                auto localBits=width(2*m->leafN);BinMatrix decorated(m->expanded,m->leafRowBits);
                for(u64 i=0;i<m->expanded;++i)
                {
                    if(!m->role)integer(decorated.data(i),0,i%(2*m->leafN),localBits);
                    copy(decorated.data(i),localBits,rows.data(i),m->compareOffset,m->compareBits);
                    copy(decorated.data(i),m->leafPayloadOffset,rows.data(i),0,m->indexBits);
                    put(decorated.data(i),m->leafPayloadOffset+m->indexBits,bit(rows.data(i),m->orderBits));
                }
                rows={};co_await m->batcher.applyOwned(std::move(decorated),merged,sock);
            }
            else co_await m->batcher.applyOwned(std::move(rows),merged,sock);
            leaves.resize(m->expanded,m->indexBits+1);
            for(u64 i=0;i<m->expanded;++i)
            {
                copy(leaves.data(i),0,merged.data(i),m->leafPayloadOffset,m->indexBits);
                put(leaves.data(i),m->indexBits,bit(merged.data(i),m->compareOffset?m->leafPayloadOffset+m->indexBits:m->orderBits));
            }
        }
        // Drain already-issued final messages so per-stage accounting includes
        // the last queued send. flush does not introduce a protocol message.
        if(!m->needsCompact)co_await sock.flush();
        m->record(stage++,sock,sent,received,start);
        if(m->needsCompact)
        {
            start=std::chrono::steady_clock::now();sent=sock.bytesSent();received=sock.bytesReceived();
            BinMatrix flags(m->expanded,1);oc::Matrix<u32> indices(m->expanded,1);
            for(u64 i=0;i<m->expanded;++i){flags(i,0)=bit(leaves.data(i),m->indexBits);indices(i,0)=integer(leaves.data(i),0,m->indexBits);}
            leaves={};co_await m->compact.apply(flags,indices,output,sock);
            co_await sock.flush();m->record(stage,sock,sent,received,start);
        }
        else
        {output.mShare.resize(2*m->n);for(u64 i=0;i<2*m->n;++i)output.mShare[i]=integer(leaves.data(i),0,m->indexBits);}
    }
    const std::vector<PiMedianLevel>& PiMedian::levels()const{return m->plans;}
    const std::vector<PiLogStarStage>& PiMedian::stages()const{return m->stats;}
    u64 PiMedian::paddedSize()const{return m->padded;}
    u64 PiMedian::expandedSize()const{return m->expanded;}
    u64 PiMedian::comparisons()const{return m->totalComparisons;}
    u64 PiMedian::comparisonBits()const{return m->compareBits;}
    u64 PiMedian::leafComparisonBits()const{return m->leafOrderBits;}
    u64 PiMedian::gmwRounds()const{return m->totalRounds;}
    u64 PiMedian::paddedAnds()const{return m->totalAnds;}
    u64 PiMedian::onlineRoundBound()const{return m->totalBound;}
    u64 PiMedian::onlinePayloadBytes()const{return m->totalBytes;}
}
