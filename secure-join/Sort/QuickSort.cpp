#include "QuickSort.h"
#include "MergeInternal.h"
#include "secure-join/Perm/AltModComposedPerm.h"
#include <chrono>
#include <cmath>
#include <cstring>
#include <numeric>

namespace secJoin
{
    namespace
    {
        using namespace merge_detail;
        u64 logCeil(u64 n) { u64 k=0; for(u64 v=n?n-1:0;v;v>>=1)++k; return k; }
        u64 add(u64 a,u64 b)
        {
            if(a>std::numeric_limits<u64>::max()-b)
                throw std::overflow_error("QuickSort: dimension overflow");
            return a+b;
        }
        u64 padded(u64 n) {return product(add(n,127)/128,128);}

        // The hidden original-position suffix guarantees distinct inputs.
        // A segment's ordering may be arbitrary on equality, including at the
        // least significant bit. Omitting the strict-LSB AND gives exactly
        // ceil(log2(width)) nonlinear layers and fewer than 2*width ANDs.
        BetaCircuit distinctComparison(u64 bits)
        {
            Circuit c;auto a=c.input(bits),b=c.input(bits),out=c.output(1);
            struct Segment {u32 equal,less;};
            std::function<Segment(u64,u64,bool)> build=[&](u64 start,u64 size,bool needEqual)->Segment
            {
                if(size==1)return {needEqual?c.neg(c.gate(a[start],b[start],oc::GateType::Xor)):c.constants[0],b[start]};
                auto low=size/2;
                auto lo=build(start,low,needEqual),hi=build(start+low,size-low,true);
                return {needEqual?c.knownAnd(lo.equal,hi.equal):c.constants[0],c.select(hi.equal,lo.less,hi.less)};
            };
            c.save(build(0,bits,false).less,out[0]);return c.finish();
        }
        u64 depth(const BetaCircuit& c)
        {return std::count_if(c.mLevelAndCounts.begin(),c.mLevelAndCounts.end(),[](u64 v){return v!=0;});}
        double harmonic(u64 k)
        {
            if(k<1024){double h=0;for(u64 i=1;i<=k;++i)h+=1.0/i;return h;}
            double x=double(k),x2=x*x;
            return std::log(x)+0.5772156649015328606+0.5/x-1/(12*x2)+1/(120*x2*x2);
        }
    }

    struct QuickSort::Impl
    {
        QuickSortPlan plan;
        QuickSortStats stats;
        u64 role=0,batchSize=0,concurrency=0;
        bool started=false,preparing=false,prepared=false,used=false;
        BetaCircuit comparison;
        AltModComposedPerm shuffleGen;
        ComposedPerm shuffle;
        BinOleRequest pool;
        std::unique_ptr<PRNG> privatePrng;

        // Carve single-use requests from a reserve. Each GMW multiplication
        // needs pairs of 128-bit blocks; drop an odd backend-segment tail
        // (possible when a different protocol precedes this pool's request).
        u64 available() const
        {
            if(!pool.initialized())return 0;
            u64 count=0;auto& s=*pool.mReqState;
            for(u64 i=s.mNextBatchIdx;i<s.mBatches_.size();++i)
                count+=s.mBatches_[i].mSize/256*256;
            return count;
        }
        BinOleRequest take(u64 count)
        {
            if(!count||count%256||available()<count)
                throw std::logic_error("QuickSort: insufficient aligned OLE reserve");
            auto& source=*pool.mReqState;
            auto state=std::make_shared<RequestState>(CorType::Ole,bool(role),count,source.mGenState,source.mReqIndex);
            while(count)
            {
                auto& segment=source.mBatches_[source.mNextBatchIdx];
                auto size=std::min(count,segment.mSize/256*256);
                if(size)
                {
                    state->addBatch(BatchSegment(segment.mBatch,segment.mBegin,size));
                    segment.mBegin+=size;segment.mSize-=size;count-=size;
                }
                if(segment.mSize<256)
                {segment.mBatch.reset();segment.mSize=0;++source.mNextBatchIdx;}
            }
            return BinOleRequest{std::move(state)};
        }
        macoro::task<> ensure(u64 count,coproto::Socket& sock)
        {
            if(available()>=count)co_return;
            // This decision depends only on the opened random-order trace.
            // Arbitrarily bad pivot trees remain correct. Refill generation is
            // explicitly reported as online work, never hidden as offline.
            pool.clear();CorGenerator cor;
            cor.init(sock.fork(),*privatePrng,role,concurrency,batchSize,false);
            auto amount=std::max(count,plan.reservedBinaryOle);
            pool=cor.binOleRequest(amount);pool.start();
            co_await sock.flush();
            auto sent=sock.bytesSent(),received=sock.bytesReceived();
            auto begin=std::chrono::steady_clock::now();
            co_await cor.start();
            co_await sock.flush();
            ++stats.refills;stats.extraReservedBinaryOle+=amount;
            stats.refillSentBytes+=sock.bytesSent()-sent;
            stats.refillReceivedBytes+=sock.bytesReceived()-received;
            stats.refillMilliseconds+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        }
    };

    QuickSort::QuickSort()=default;
    QuickSort::~QuickSort()=default;
    QuickSort::QuickSort(QuickSort&&) noexcept=default;
    QuickSort& QuickSort::operator=(QuickSort&&) noexcept=default;

    QuickSortPlan QuickSort::plan(u64 n,u64 bits,const QuickSortOptions& options)
    {
        if(n>=std::numeric_limits<u32>::max()||!bits||bits>4096||options.terminalSize<2||options.terminalSize>32||
            (options.pivotCount!=1&&options.pivotCount!=3))
            throw std::invalid_argument("QuickSort: require rows < 2^32-1, 1 <= keyBits <= 4096, terminalSize in [2,32]");
        QuickSortPlan p;p.rows=n;p.keyBits=bits;p.indexBits=logCeil(n);
        p.comparisonBits=bits+p.indexBits;p.terminalSize=options.terminalSize;
        p.pivotCount=options.pivotCount;
        if(n<2)return p;
        auto circuit=distinctComparison(p.comparisonBits);
        p.andsPerComparison=circuit.mNonlinearGateCount;p.roundsPerComparison=depth(circuit);
        auto worst=product(n,n-1)/2;
        // Modest slack above the leading 1.386/1.919*n*log2(n) work terms,
        // plus terminal work and small-batch padding. Correctness does not
        // depend on this heuristic succeeding: ensure() replenishes it.
        auto perRow=((options.pivotCount==1?8:11)*logCeil(n)+4)/5+(options.terminalSize+3)/4;
        auto reserve=add(product(n,perRow),128*(4*logCeil(n)+1));
        // Each nonempty wave loses at most 127 lanes to SIMD rounding.
        reserve=std::min(reserve,add(worst,product(127,n-1)));
        if(n<=options.terminalSize)reserve=worst;
        p.reservedComparisonLanes=padded(options.reserveComparisons?options.reserveComparisons:reserve);
        p.reservedBinaryOle=product(product(2,p.andsPerComparison),p.reservedComparisonLanes);
        p.shufflePayloadBytes=product(2,product(n,oc::divCeil(p.comparisonBits,8)));
        // E_n=n-1+(2/n)*sum_{k<n}E_k, E_k=k(k-1)/2 for k<=t.
        // Solve the recurrence above s=t+1; the all-pairs boundary does not
        // itself satisfy the quicksort recurrence. Harmonics use a constant
        // time Euler-Maclaurin approximation for large public n.
        auto t=std::min(n,options.terminalSize);
        // For p random pivots the expected spacing entropy is H_(p+1)-1.
        // Comparing each other row to all p pivots has leading cost
        // [p/(H_(p+1)-1)]*n*ln(n). This is an asymptotic estimate, not a bound.
        p.leadingComparisonEstimate=(options.pivotCount==1?2.0:36.0/13)*n*std::log(double(n));
        if(n<=t)p.expectedComparisons=double(n)*(n-1)/2;
        else if(options.pivotCount==3)p.expectedComparisons=-1;
        else
        {
            double e=double(t)+double(t)*(t-1)/3;
            p.expectedComparisons=(n+1)*((e-2)/(t+2)+2*(harmonic(n+1)-harmonic(t+2)))+2;
        }
        return p;
    }

    void QuickSort::init(u64 rows,u64 keyBits,CorGenerator& cor,const QuickSortOptions& options)
    {
        auto p=plan(rows,keyBits,options);
        if(!cor.initialized()||cor.partyIdx()>1||cor.mGenState->mMock||cor.mGenState->mDebug)
            throw std::invalid_argument("QuickSort requires a real, non-debug CorGenerator");
        auto next=std::make_unique<Impl>();next->plan=p;next->role=cor.partyIdx();
        next->batchSize=cor.mGenState->mBatchSize;next->concurrency=cor.mGenState->mNumConcurrent;
        if(next->batchSize%256)throw std::invalid_argument("QuickSort: correlation batch size must be a multiple of 256");
        if(rows>1)
        {
            next->comparison=distinctComparison(p.comparisonBits);
            next->pool=cor.binOleRequest(p.reservedBinaryOle);
            next->shuffleGen.init(next->role,rows,oc::divCeil(p.comparisonBits,8),cor);
        }
        m=std::move(next);
    }
    void QuickSort::preprocess()
    {
        if(!m||m->started)throw std::logic_error("QuickSort: preprocess requires a fresh initialization");
        m->started=true;
        if(m->plan.rows>1){m->pool.start();m->shuffleGen.preprocess();}
    }
    macoro::task<> QuickSort::prepare(coproto::Socket& sock,PRNG& prng)
    {
        if(!m||!m->started||m->preparing)throw std::logic_error("QuickSort: invalid prepare lifecycle");
        m->preparing=true;m->privatePrng=std::make_unique<PRNG>(prng.fork());
        if(m->plan.rows>1)
            co_await m->shuffleGen.generate(sock,prng,m->plan.rows,m->shuffle);
        m->prepared=true;
    }
    const QuickSortPlan& QuickSort::plan() const
    {
        if(!m)throw std::logic_error("QuickSort: uninitialized");
        return m->plan;
    }
    const QuickSortStats& QuickSort::stats() const
    {
        if(!m)throw std::logic_error("QuickSort: uninitialized");
        return m->stats;
    }

    macoro::task<> QuickSort::sort(const BinMatrix& keys,AdditivePerm& output,coproto::Socket& sock)
    {
        if(!m||!m->prepared||m->used)throw std::logic_error("QuickSort: sort requires unused prepared state");
        auto& p=m->plan;
        if(keys.rows()!=p.rows||keys.bitsPerEntry()!=p.keyBits||keys.cols()!=oc::divCeil(p.keyBits,8))
            throw std::invalid_argument("QuickSort: input shape mismatch");
        m->used=true;
        if(p.rows<2){output.mShare.assign(p.rows,0);co_return;}
        BinMatrix records(p.rows,p.comparisonBits),shuffled(p.rows,p.comparisonBits);
        for(u64 i=0;i<p.rows;++i)
        {
            integer(records.data(i),0,m->role?0:i,p.indexBits);
            copy(records.data(i),p.indexBits,keys.data(i),0,p.keyBits);
        }
        co_await m->shuffle.apply<u8>(PermOp::Regular,records.mData,shuffled.mData,sock);
        records={};m->shuffle={};
        m->stats.shuffleRounds=2;m->stats.onlinePayloadBytes=p.shufflePayloadBytes;

        // Records stay stationary. Partition public handles into hidden
        // shuffled positions, NEVER handles into the original input order.
        std::vector<u32> order(p.rows),scratch(p.rows);
        std::iota(order.begin(),order.end(),0);
        struct Range{u64 begin,end;};
        std::vector<Range> active{{0,p.rows}},next;
        while(!active.empty())
        {
            u64 count=0;
            for(auto r:active)
            {
                auto size=r.end-r.begin,pivots=std::min(p.pivotCount,size-1);
                count=add(count,size<=p.terminalSize?size*(size-1)/2:pivots*(size-pivots)+pivots*(pivots-1)/2);
            }
            auto gates=product(p.andsPerComparison,padded(count));
            co_await m->ensure(product(2,gates),sock);
            Gmw g;g.init(count,m->comparison,m->role,m->take(2*gates));
            BinMatrix a,b,q(count,1);
            a.resize(count,p.comparisonBits,1,oc::AllocType::Uninitialized);
            b.resize(count,p.comparisonBits,1,oc::AllocType::Uninitialized);
            u64 pos=0;auto bytes=shuffled.cols();
            auto pair=[&](u64 i,u64 j)
            {
                std::memcpy(a.data(pos),shuffled.data(order[i]),bytes);
                std::memcpy(b.data(pos),shuffled.data(order[j]),bytes);++pos;
            };
            for(auto r:active)
            {
                if(r.end-r.begin<=p.terminalSize)
                    for(u64 i=r.begin;i<r.end;++i)for(u64 j=i+1;j<r.end;++j)pair(i,j);
                else
                {
                    auto pivots=std::min(p.pivotCount,r.end-r.begin-1),start=r.end-pivots;
                    for(u64 i=start;i<r.end;++i)for(u64 j=i+1;j<r.end;++j)pair(i,j);
                    for(u64 i=r.begin;i<start;++i)for(u64 j=start;j<r.end;++j)pair(i,j);
                }
            }
            // Use MatrixView inputs: Gmw's convenience BinMatrix overload
            // takes by value. Transpose once, then release row-major scratch
            // before allocating internal evaluation wires and messages.
            BinMatrix constants(count,2);
            if(!m->role)for(u64 i=0;i<count;++i)constants(i,0)=2;
            g.setInput<u8>(0,constants.mData);g.setInput<u8>(1,a.mData);g.setInput<u8>(2,b.mData);
            constants={};a={};b={};
            co_await g.run(sock);g.getOutput(0,q);g.clear();
            auto opened=co_await openPacked(q,1,sock);
            ++m->stats.comparisonBatches;m->stats.comparisons+=count;m->stats.paddedAnds+=gates;
            m->stats.comparisonRounds+=p.roundsPerComparison;++m->stats.openingRounds;
            auto openingBytes=2*oc::divCeil(count,8);
            m->stats.openingPayloadBytes+=openingBytes;m->stats.onlinePayloadBytes+=gates/2+openingBytes;
            pos=0;next.clear();
            for(auto r:active)
            {
                auto size=r.end-r.begin;
                if(size<=p.terminalSize)
                {
                    std::array<u64,32> ranks{};
                    for(u64 i=0;i<size;++i)for(u64 j=i+1;j<size;++j)
                    {auto less=bit(opened.data(),pos++);ranks[i]+=1-less;ranks[j]+=less;}
                    u64 seen=0;
                    for(u64 i=0;i<size;++i)
                    {
                        if(ranks[i]>=size||(seen&(u64(1)<<ranks[i])))
                            throw std::runtime_error("QuickSort: inconsistent terminal comparison results");
                        seen|=u64(1)<<ranks[i];scratch[r.begin+ranks[i]]=order[r.begin+i];
                    }
                    std::copy(scratch.begin()+r.begin,scratch.begin()+r.end,order.begin()+r.begin);
                }
                else
                {
                    auto pivots=std::min(p.pivotCount,size-1),start=r.end-pivots;
                    std::array<u64,3> pivotRanks{};
                    for(u64 i=0;i<pivots;++i)for(u64 j=i+1;j<pivots;++j)
                    {auto less=bit(opened.data(),pos++);pivotRanks[i]+=1-less;pivotRanks[j]+=less;}
                    u64 seen=0;
                    for(u64 j=0;j<pivots;++j)
                    {
                        if(pivotRanks[j]>=pivots||(seen&(u64(1)<<pivotRanks[j])))
                            throw std::runtime_error("QuickSort: inconsistent pivot comparison results");
                        seen|=u64(1)<<pivotRanks[j];
                    }
                    std::array<u64,4> sizes{},begins{},write{};
                    auto begin=pos;
                    auto bucket=[&](u64 offset)
                    {
                        u64 below=0;
                        for(u64 j=0;j<pivots;++j)below+=1-bit(opened.data(),offset+j);
                        return below;
                    };
                    for(u64 i=r.begin;i<start;++i){++sizes[bucket(pos)];pos+=pivots;}
                    begins[0]=r.begin;
                    for(u64 j=0;j<pivots;++j)begins[j+1]=begins[j]+sizes[j]+1;
                    write=begins;
                    for(u64 i=r.begin;i<start;++i)
                        scratch[write[bucket(begin+(i-r.begin)*pivots)]++]=order[i];
                    for(u64 j=0;j<pivots;++j)
                        scratch[begins[pivotRanks[j]]+sizes[pivotRanks[j]]]=order[start+j];
                    std::copy(scratch.begin()+r.begin,scratch.begin()+r.end,order.begin()+r.begin);
                    for(u64 j=0;j<=pivots;++j)
                        if(sizes[j]>1)next.push_back({begins[j],begins[j]+sizes[j]});
                }
            }
            active.swap(next);
        }
        output.mShare.resize(p.rows);
        for(u64 i=0;i<p.rows;++i)output.mShare[i]=integer(shuffled.data(order[i]),0,p.indexBits);
        m->pool.clear();m->privatePrng.reset();
    }
    macoro::task<> QuickSort::merge(const BinMatrix& x,const BinMatrix& y,AdditivePerm& output,coproto::Socket& sock)
    {
        if(!m||x.rows()>m->plan.rows||y.rows()!=m->plan.rows-x.rows()||
            x.bitsPerEntry()!=m->plan.keyBits||y.bitsPerEntry()!=m->plan.keyBits||
            x.cols()!=oc::divCeil(m->plan.keyBits,8)||y.cols()!=x.cols())
            throw std::invalid_argument("QuickSort: merge shape mismatch");
        BinMatrix keys(m->plan.rows,m->plan.keyBits);
        if(x.size())std::memcpy(keys.data(),x.data(),x.size());
        if(y.size())std::memcpy(keys.data()+x.size(),y.data(),y.size());
        co_await sort(keys,output,sock);
    }
}
