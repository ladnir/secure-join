#include "secure-join/Sort/PiMedian.h"
#include "coproto/Socket/LocalAsyncSock.h"
#include "coproto/Socket/AsioSocket.h"
#include "macoro/sync_wait.h"
#include "macoro/when_all.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <tuple>
#ifdef __linux__
#include <sys/resource.h>
#endif

namespace
{
    using namespace secJoin;
    using Clock=std::chrono::steady_clock;
    const std::array<std::string,7> patterns={"random","equal","disjoint","reverse-disjoint","interleaved","max","duplicates"};
    const std::array<std::string,3> methods={"allpairs","batcher","bitonic"};
    struct Options
    {
        u64 m=0,n=1024,bits=32,base=16,maxDepth=2,batch=1<<20,concurrency=2;
        std::vector<u64> children,cubes;
        u64 maxRows=u64(1)<<28;
        std::string method="batcher",pattern="random",seed="1",address="127.0.0.1:12123";
        int party=-1;
        bool plan=false,selfTest=false,help=false,fullIndex=false;
    };
    u64 number(const std::string& s)
    {
        if(s.empty()||s[0]=='-')throw std::invalid_argument("Expected a nonnegative integer");
        std::size_t end=0;auto v=std::stoull(s,&end);
        if(end!=s.size())throw std::invalid_argument("Invalid integer: "+s);
        return v;
    }
    std::vector<u64> numbers(const std::string& s)
    {
        std::vector<u64> out;std::size_t begin=0;
        do {auto end=s.find(',',begin);out.push_back(number(s.substr(begin,end-begin)));
            if(end==std::string::npos)break;
            begin=end+1;}while(true);
        return out;
    }
    PiMedianOptions parameters(const Options& o)
    {
        PiMedianOptions p;p.baseCase=o.base;p.maxDepth=o.maxDepth;p.childSizes=o.children;p.cubeBlocks=o.cubes;p.maxExpandedRows=o.maxRows;
        p.leaf=o.method=="allpairs"?PiMedianLeaf::AllPairs:o.method=="bitonic"?PiMedianLeaf::Bitonic:PiMedianLeaf::Batcher;
        p.fullIndexComparisons=o.fullIndex;return p;
    }
    u64 seed(const std::string& s)
    {u64 h=14695981039346656037ull;for(unsigned char c:s){h^=c;h*=1099511628211ull;}return h;}
    std::string quote(const std::string& s)
    {std::string out="\"";for(char c:s){if(c=='\\'||c=='\"')out+='\\';if(static_cast<unsigned char>(c)<32)throw std::invalid_argument("Control character in argument");out+=c;}return out+'\"';}
    Options parse(int argc,char** argv)
    {
        Options o;
        for(int i=1;i<argc;++i)
        {
            std::string arg=argv[i];
            if(arg=="--plan"){o.plan=true;continue;}
            if(arg=="--full-index"){o.fullIndex=true;continue;}
            if(arg=="--self-test"){o.selfTest=true;continue;}
            if(arg=="--help"||arg=="-h"){o.help=true;continue;}
            if(i+1==argc)throw std::invalid_argument("Missing value for "+arg);
            std::string v=argv[++i];
            if(arg=="--n")o.n=number(v);
            else if(arg=="--bits")o.bits=number(v);else if(arg=="--base-case")o.base=number(v);
            else if(arg=="--children")o.children=numbers(v);else if(arg=="--cube-blocks")o.cubes=numbers(v);
            else if(arg=="--max-expanded-rows")o.maxRows=number(v);
            else if(arg=="--max-depth")o.maxDepth=number(v);
            else if(arg=="--batch-size")o.batch=number(v);else if(arg=="--concurrency")o.concurrency=number(v);
            else if(arg=="--leaf")o.method=v;else if(arg=="--pattern")o.pattern=v;
            else if(arg=="--seed")o.seed=v;else if(arg=="--address")o.address=v;
            else if(arg=="--party"){if(v!="0"&&v!="1")throw std::invalid_argument("party must be 0 or 1");o.party=v=="1";}
            else throw std::invalid_argument("Unknown option: "+arg);
        }
        if(!o.n||o.n>(u64(1)<<26)||!o.bits||o.bits>(o.plan?256:64))
            throw std::invalid_argument("Require 1 <= n <= 2^26; bits <= 64 for synthetic runs or <= 256 for --plan");
        if(std::find(methods.begin(),methods.end(),o.method)==methods.end())throw std::invalid_argument("leaf: allpairs, batcher, bitonic");
        if(std::find(patterns.begin(),patterns.end(),o.pattern)==patterns.end())throw std::invalid_argument("Unknown input pattern");
        o.m=o.n;
        if(o.batch<1024||o.batch>(u64(1)<<26)||(o.batch&(o.batch-1))||!o.concurrency||o.concurrency>1024)
            throw std::invalid_argument("Invalid correlation batch or concurrency");
        if(o.selfTest&&o.party!=-1)throw std::invalid_argument("Self-test is local");
        return o;
    }
    template<class... T>void complete(T&&... tasks)
    {
        auto result=macoro::sync_wait(macoro::when_all_ready(std::forward<T>(tasks)...));
        std::apply([](auto&... r){(r.result(),...);},result);
    }
    template<class F>void rejects(F action)
    {try{action();}catch(const std::exception&){return;}throw std::runtime_error("Expected rejection");}
    struct Data{std::vector<u64>x,y;std::vector<u32>expected;};
    Data dataset(const Options& o)
    {
        Data d;d.x.resize(o.m);d.y.resize(o.n);std::mt19937_64 rng(seed(o.seed));
        auto mask=o.bits==64?~u64(0):(u64(1)<<o.bits)-1;
        for(u64 side=0;side<2;++side)
        {
            auto& run=side?d.y:d.x;
            for(u64 i=0;i<run.size();++i)
            {
                if(o.pattern=="equal")run[i]=mask/2;
                else if(o.pattern=="max")run[i]=mask;
                else if(o.pattern=="duplicates")run[i]=rng()%std::min<u64>(8,o.bits==64?8:mask+1);
                else if(o.pattern=="interleaved")run[i]=(2*i+side)&mask;
                else if(o.pattern=="disjoint"||o.pattern=="reverse-disjoint")
                {auto split=u64(1)<<(o.bits-1);run[i]=(rng()&(split-1))|((side^(o.pattern=="reverse-disjoint"))?split:0);}
                else run[i]=rng()&mask;
            }
            std::sort(run.begin(),run.end());
        }
        u64 i=0,j=0;
        while(i<o.m||j<o.n)
            if(j==o.n||(i<o.m&&d.x[i]<=d.y[j]))d.expected.push_back(i++);
            else d.expected.push_back(o.m+j++);
        return d;
    }
    BinMatrix encode(const std::vector<u64>& data,u64 bits)
    {BinMatrix out(data.size(),bits);for(u64 i=0;i<data.size();++i)for(u64 j=0;j<out.bytesPerEntry();++j)out(i,j)=data[i]>>(8*j);return out;}
    void verify(const AdditivePerm& a,const AdditivePerm& b,const std::vector<u32>& expected)
    {
        if(a.size()!=expected.size()||b.size()!=expected.size())throw std::runtime_error("Wrong output size");
        for(u64 i=0;i<a.size();++i)if((a.mShare[i]^b.mShare[i])!=expected[i])
            throw std::runtime_error("Stable output mismatch at "+std::to_string(i)+": got "+std::to_string(a.mShare[i]^b.mShare[i])+", expected "+std::to_string(expected[i]));
    }
    struct Counters{u64 sent=0,received=0;};
    Counters count(coproto::Socket& s){return {s.bytesSent(),s.bytesReceived()};}
    Counters difference(Counters a,Counters b){return {a.sent-b.sent,a.received-b.received};}
    double elapsed(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
    struct Measurement{double setup=0,offline=0,online=0,wall=0;Counters off[2],on[2];};
    void schedule(const Options& o,const PiMedian& p,const CorGenerator* cor=nullptr)
    {
        std::cout<<",\"implementation_version\":2,\"leaf\":"<<quote(o.method)<<",\"n\":"<<o.n
            <<",\"comparison_bits\":"<<p.comparisonBits()
            <<",\"leaf_comparison_bits\":"<<p.leafComparisonBits()
            <<",\"max_depth\":"<<o.maxDepth
            <<",\"key_bits\":"<<o.bits<<",\"base_case\":"<<o.base<<",\"batch_size\":"<<o.batch
            <<",\"concurrency\":"<<o.concurrency<<",\"comparisons\":"<<p.comparisons()
            <<",\"padded_size\":"<<p.paddedSize()<<",\"expanded_size\":"<<p.expandedSize()
            <<",\"online_gmw_ands_padded\":"<<p.paddedAnds()<<",\"online_gmw_rounds_sum\":"<<p.gmwRounds()
            <<",\"online_round_bound\":"<<p.onlineRoundBound()<<",\"online_payload_bytes\":"<<p.onlinePayloadBytes()
            <<",\"levels\":[";
        bool first=true;
        for(auto& l:p.levels())
        {
            if(!first)std::cout<<',';
            first=false;
            std::cout<<"{\"batches\":"<<l.batches<<",\"half_size\":"<<l.halfSize<<",\"child_size\":"<<l.childSize
                <<",\"medians\":"<<l.medians<<",\"cube_block\":"<<l.cubeBlock<<",\"comparisons\":"<<l.comparisons
                <<",\"gmw_rounds_sum\":"<<l.gmwRounds<<",\"padded_ands\":"<<l.paddedAnds
                <<",\"online_round_bound\":"<<l.onlineRoundBound<<",\"shuffle_payload_bytes\":"<<l.shufflePayloadBytes
                <<",\"opening_payload_bytes\":"<<l.openingPayloadBytes<<'}';
        }
        std::cout<<']';
        if(cor)
        {
            auto& g=*cor->mGenState;
            std::cout<<",\"offline_requests_per_party\":{\"binary_ole\":"<<g.mNumOle
                <<",\"random_ot\":"<<g.mNumOt<<",\"f4_bit_ot\":"<<g.mNumF4BitOt
                <<",\"trit_ot\":"<<g.mNumTritOt<<",\"batches\":"<<g.mBatches.size()<<'}';
        }
    }
    void report(const Options& o,const PiMedian& p,const Measurement& t)
    {
        std::cout<<std::fixed<<std::setprecision(3)<<"{\"type\":\"development_run\",\"protocol\":\"pi_median\"";
        schedule(o,p);
        std::cout<<",\"transport\":"<<quote(o.party<0?"local_async":"tcp")<<",\"party\":"<<o.party
            <<",\"real_crypto\":true,\"verified\":true,\"public_synthetic_inputs\":true"
            <<",\"pattern\":"<<quote(o.pattern)<<",\"public_seed\":"<<quote(o.seed)
            <<",\"setup_ms\":"<<t.setup<<",\"offline_ms\":"<<t.offline
            <<",\"online_ms\":"<<t.online<<",\"wall_ms\":"<<t.wall;
#ifdef __linux__
        rusage usage{};if(!getrusage(RUSAGE_SELF,&usage))std::cout<<",\"peak_rss_kib\":"<<usage.ru_maxrss;
#endif
        u64 off=0,on=0;
        for(int party=o.party<0?0:o.party;party<(o.party<0?2:o.party+1);++party)
        {
            std::cout<<",\"offline_p"<<party<<"_sent_bytes\":"<<t.off[party].sent
                <<",\"offline_p"<<party<<"_received_bytes\":"<<t.off[party].received
                <<",\"online_p"<<party<<"_sent_bytes\":"<<t.on[party].sent
                <<",\"online_p"<<party<<"_received_bytes\":"<<t.on[party].received;
            off+=t.off[party].sent;on+=t.on[party].sent;
        }
        if(o.party<0)std::cout<<",\"offline_total_sent_bytes\":"<<off<<",\"online_total_sent_bytes\":"<<on;
        std::cout<<",\"stages\":[";bool first=true;
        for(auto& s:p.stages())
        {
            if(!first)std::cout<<',';
            first=false;
            std::cout<<"{\"name\":"<<quote(s.name)<<",\"gmw_ands_padded\":"<<s.paddedAnds
                <<",\"gmw_rounds_sum\":"<<s.gmwRounds<<",\"sent_bytes\":"<<s.sentBytes
                <<",\"received_bytes\":"<<s.receivedBytes<<",\"milliseconds\":"<<s.milliseconds<<'}';
        }
        std::cout<<"]}"<<std::endl;
    }
    void execute(const Options& o,const BinMatrix& plainX,const BinMatrix& plainY,
        const std::vector<u32>& expected,bool randomShares)
    {
        auto sockets=coproto::LocalAsyncSocket::makePair();
        PRNG prng[2]={PRNG(oc::sysRandomSeed()),PRNG(oc::sysRandomSeed())};
        BinMatrix x[2],y[2];x[0]=plainX;x[1].resize(o.m,o.bits);y[0].resize(o.n,o.bits);y[1]=plainY;
        if(randomShares)
        {
            prng[0].get(x[1].data(),x[1].size());prng[0].get(y[0].data(),y[0].size());x[1].trim();y[0].trim();
            for(u64 i=0;i<plainX.size();++i)x[0](i)=plainX(i)^x[1](i);
            for(u64 i=0;i<plainY.size();++i)y[1](i)=plainY(i)^y[0](i);
        }
        CorGenerator cor[2];PiMedian p[2];AdditivePerm output[2];Measurement t;
        auto start=Clock::now();
        for(u64 party=0;party<2;++party)
        {cor[party].init(sockets[party].fork(),prng[party],party,o.concurrency,o.batch,false);p[party].init(o.n,o.bits,cor[party],parameters(o));p[party].preprocess();}
        auto offline=Clock::now();Counters before[2]={count(sockets[0]),count(sockets[1])};
        complete(cor[0].start(),cor[1].start(),p[0].prepare(sockets[0],prng[0]),p[1].prepare(sockets[1],prng[1]));
        complete(sockets[0].flush(),sockets[1].flush());
        auto online=Clock::now();Counters ready[2]={count(sockets[0]),count(sockets[1])};
        if(randomShares)
        {
            BinMatrix bad(o.m+1,o.bits);
            rejects([&]{complete(p[0].merge(bad,y[0],output[0],sockets[0],prng[0]));});
        }
        complete(p[0].merge(x[0],y[0],output[0],sockets[0],prng[0]),p[1].merge(x[1],y[1],output[1],sockets[1],prng[1]));
        complete(sockets[0].flush(),sockets[1].flush());auto end=Clock::now();
        for(u64 party=0;party<2;++party){t.off[party]=difference(ready[party],before[party]);t.on[party]=difference(count(sockets[party]),ready[party]);}
        t.setup=elapsed(start,offline);t.offline=elapsed(offline,online);t.online=elapsed(online,end);t.wall=elapsed(start,end);
        verify(output[0],output[1],expected);
        if(randomShares)
        {
            rejects([&]{complete(p[0].merge(x[0],y[0],output[0],sockets[0],prng[0]));});
            rejects([&]{complete(p[0].prepare(sockets[0],prng[0]));});
            rejects([&]{p[0].preprocess();});rejects([&]{p[0].init(o.n,o.bits,cor[0],parameters(o));});
        }
        if(t.on[0].sent+t.on[1].sent<p[0].onlinePayloadBytes())throw std::runtime_error("Payload estimate exceeds measured bytes");
        if(!randomShares)report(o,p[0],t);
    }
    void local(const Options& o,bool randomShares=false)
    {
        auto data=dataset(o);
        execute(o,encode(data.x,o.bits),encode(data.y,o.bits),data.expected,randomShares);
    }
    void wide(const Options& o)
    {
        // Exercise every input bit, including comparisons decided above bit 64.
        std::mt19937_64 rng(seed(o.seed));
        std::vector<std::vector<u8>> x(o.m),y(o.n);
        auto less=[](const auto& a,const auto& b)
        {return std::lexicographical_compare(a.rbegin(),a.rend(),b.rbegin(),b.rend());};
        for(auto* run:{&x,&y})
        {
            for(auto& key:*run)
            {
                key.resize((o.bits+7)/8);for(auto& byte:key)byte=rng();
                if(o.bits%8)key.back()&=(1u<<(o.bits%8))-1;
            }
            run->back()=run->front();std::sort(run->begin(),run->end(),less);
        }
        y[0]=x[0];std::sort(y.begin(),y.end(),less);
        BinMatrix a(o.m,o.bits),b(o.n,o.bits);
        for(u64 i=0;i<o.m;++i)std::copy(x[i].begin(),x[i].end(),a[i].begin());
        for(u64 i=0;i<o.n;++i)std::copy(y[i].begin(),y[i].end(),b[i].begin());
        std::vector<u32> expected;u64 i=0,j=0;
        while(i<o.m||j<o.n)
            if(j==o.n||(i<o.m&&!less(y[j],x[i])))expected.push_back(i++);
            else expected.push_back(o.m+j++);
        execute(o,a,b,expected,true);
    }
    void network(const Options& o)
    {
#ifdef COPROTO_ENABLE_BOOST
        auto data=dataset(o);auto socket=coproto::asioConnect(o.address,o.party==0);
        // Exact bounded configuration exchange; reject mismatches before any
        // correlation generation or input-dependent protocol messages.
        std::array<u64,48> config{},peer{};
        if(o.children.size()>16||o.cubes.size()>16)throw std::invalid_argument("Too many level overrides");
        config[0]=0x4d4544490002ull;config[1]=o.n;config[2]=o.bits;config[3]=o.base;config[4]=o.batch;
        config[5]=o.concurrency;config[6]=std::find(methods.begin(),methods.end(),o.method)-methods.begin();config[7]=seed(o.seed);
        config[8]=std::find(patterns.begin(),patterns.end(),o.pattern)-patterns.begin();config[9]=o.maxRows;
        config[10]=o.children.size();config[11]=o.cubes.size();config[47]=o.party;
        config[44]=o.fullIndex;
        config[45]=o.maxDepth;
        std::copy(o.children.begin(),o.children.end(),config.begin()+12);std::copy(o.cubes.begin(),o.cubes.end(),config.begin()+28);
        complete(socket.send(coproto::copy(config)),socket.recv(peer));peer.back()^=1;
        if(config!=peer)throw std::runtime_error("Public configurations differ");
        BinMatrix x(o.m,o.bits),y(o.n,o.bits);if(!o.party)x=encode(data.x,o.bits);else y=encode(data.y,o.bits);
        PRNG prng(oc::sysRandomSeed());CorGenerator cor;PiMedian p;AdditivePerm out,other;Measurement t;
        auto start=Clock::now();cor.init(socket.fork(),prng,o.party,o.concurrency,o.batch,false);
        p.init(o.n,o.bits,cor,parameters(o));p.preprocess();auto offline=Clock::now();auto before=count(socket);
        complete(cor.start(),p.prepare(socket,prng));complete(socket.flush());auto offlineEnd=Clock::now();auto prepared=count(socket);
        std::array<u64,1> barrier={1},peerBarrier{};
        complete(socket.send(coproto::copy(barrier)),socket.recv(peerBarrier));complete(socket.flush());
        if(barrier!=peerBarrier)throw std::runtime_error("Offline synchronization failed");
        auto online=Clock::now();auto ready=count(socket);
        complete(p.merge(x,y,out,socket,prng));complete(socket.flush());auto end=Clock::now();
        t.off[o.party]=difference(prepared,before);t.on[o.party]=difference(count(socket),ready);
        t.setup=elapsed(start,offline);t.offline=elapsed(offline,offlineEnd);t.online=elapsed(online,end);t.wall=elapsed(start,end);
        // Intentional output opening for synthetic verification, outside phase measurements.
        barrier[0]=2;complete(socket.send(coproto::copy(barrier)),socket.recv(peerBarrier));
        if(barrier!=peerBarrier)throw std::runtime_error("Online synchronization failed");
        other.mShare.resize(out.size());complete(socket.send(coproto::copy(out.mShare)),socket.recv(other.mShare));complete(socket.flush());
        verify(out,other,data.expected);report(o,p,t);
#else
        throw std::runtime_error("TCP support requires Boost");
#endif
    }
    void selfTest()
    {
        {
            auto sockets=coproto::LocalAsyncSocket::makePair();PRNG prng(oc::sysRandomSeed());CorGenerator cor,mock;
            cor.init(sockets[0].fork(),prng,0,2,1<<14,false);mock.init(sockets[0].fork(),prng,0,2,1<<14,true);
            rejects([&]{PiMedian p;p.init(16,32,mock);});cor.mGenState->mDebug=true;
            rejects([&]{PiMedian p;p.init(16,32,cor);});cor.mGenState->mDebug=false;
            rejects([&]{PiMedian p;p.init(0,32,cor);});rejects([&]{PiMedian p;p.init(16,0,cor);});
            rejects([&]{PiMedian p;p.init(16,257,cor);});
            for(int which=0;which<7;++which)
            {
                PiMedianOptions p;
                if(which==0)p.baseCase=3;else if(which==1)p.childSizes={3};else if(which==2)p.childSizes={64};
                else if(which==3)p.cubeBlocks={3};else if(which==4)p.cubeBlocks={128};
                else if(which==5)p.maxExpandedRows=16;else p.childSizes={8,4};
                auto before=cor.mGenState->mNumOle;
                rejects([&]{PiMedian protocol;protocol.init(64,32,cor,p);});
                if(before!=cor.mGenState->mNumOle)throw std::runtime_error("Invalid schedule registered requests");
            }
        }
        u64 count=0;
        for(auto method:methods)for(auto pattern:patterns)for(u64 n:{1,3,8,17,32,65,128})
        {
            Options o;o.method=method;o.pattern=pattern;o.m=o.n=n;o.base=8;o.maxDepth=0;o.batch=1<<14;
            o.bits=std::array<u64,4>{1,7,32,64}[count%4];o.fullIndex=count%3==0;local(o,true);++count;
        }
        for(auto method:methods)for(auto pattern:{"equal","duplicates","reverse-disjoint","random"})
            for(auto block:{1,4,64})
        {
            Options o;o.method=method;o.pattern=pattern;o.m=o.n=64;o.base=4;o.maxDepth=0;o.children={16,4};o.cubes={u64(block),16};
            o.bits=32;o.batch=1<<14;local(o,true);++count;
        }
        for(auto method:methods)for(auto bits:{65,127,256})
        {Options o;o.method=method;o.pattern="wide_random";o.m=o.n=33;o.base=8;o.maxDepth=0;o.bits=bits;o.batch=1<<14;wide(o);++count;}
        for(auto pattern:{"equal","duplicates","interleaved","reverse-disjoint"})
        {
            Options o;o.method="allpairs";o.pattern=pattern;o.m=o.n=1024;o.base=8;o.maxDepth=0;o.bits=32;o.batch=1<<16;
            local(o,true);++count;
        }
        // One-element leaves, explicit automatic overrides, and the unpacked
        // fallback for a large all-pairs leaf with only one SIMD instance.
        for(auto method:methods)for(int variant=0;variant<4;++variant)
        {
            Options o;o.method=method;o.pattern="duplicates";o.m=o.n=variant==3?65:16;
            o.base=variant==3?128:1;o.maxDepth=0;o.batch=1<<14;o.bits=32;
            if(variant==0)o.children={1};else if(variant==1)o.children={8,4,2,1};
            else if(variant==2){o.children={0,0};o.cubes={0,0};o.base=2;}
            local(o,true);++count;
        }
        // Exercise the largest packed leaf circuit without a scaling run.
        {Options o;o.method="allpairs";o.pattern="duplicates";o.m=o.n=4096;o.base=64;o.children={64};o.cubes={8};
         o.bits=1;o.batch=1<<16;local(o,true);++count;}
        // Practical two-level default, including its larger Batcher leaves.
        for(auto pattern:{"random","duplicates","reverse-disjoint"})
        {Options o;o.m=o.n=1024;o.pattern=pattern;o.batch=1<<16;local(o,true);++count;}
        std::cout<<"{\"type\":\"self_test\",\"test\":\"pi_median_random_shares\",\"cases\":"<<count<<",\"passed\":true}"<<std::endl;
    }

}
int main(int argc,char** argv)
{
    try
    {
        auto o=parse(argc,argv);
        if(o.help)
        {
            std::cout<<"pimedian --n N [--bits 32] [--base-case 16] [--max-depth 2] [--leaf batcher|bitonic|allpairs]\n"
                <<"  [--children 256,32,8] [--cube-blocks 16,8,4] [--max-expanded-rows 268435456]\n"
                <<"  [--full-index] (diagnostic: force global-index comparisons throughout)\n"
                <<"  [--batch-size 1048576] [--concurrency 2] [--seed 1]\n"
                <<"  [--pattern random|equal|disjoint|reverse-disjoint|interleaved|max|duplicates]\n"
                <<"  [--party 0|1 --address 127.0.0.1:12123] [--plan] [--self-test]\n"
                <<"Per-level zero/missing overrides select defaults. n is the size of EACH input list.\n"
                <<"Default: at most two alignment levels, then Batcher leaves. --max-depth 0 removes the depth cap.\n"
                <<"--plan registers a public schedule only; it runs no crypto or benchmarks.\n"
                <<"Local/TCP executions use synthetic public inputs and fresh private randomness.\n"
                <<"Output verification occurs after phase accounting. Self-test uses random XOR shares.\n";
        }
        else if(o.plan)
        {
            auto sockets=coproto::LocalAsyncSocket::makePair();PRNG prng(oc::sysRandomSeed());CorGenerator cor;PiMedian p;
            cor.init(sockets[0].fork(),prng,0,o.concurrency,o.batch,false);p.init(o.n,o.bits,cor,parameters(o));
            std::cout<<"{\"type\":\"public_schedule\"";schedule(o,p,&cor);std::cout<<"}"<<std::endl;
        }
        else if(o.selfTest)selfTest();else if(o.party<0)local(o);else network(o);
        return 0;
    }
    catch(const std::exception& e){std::cerr<<"pimedian: "<<e.what()<<std::endl;return 1;}
}
