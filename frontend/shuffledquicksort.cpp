#include "secure-join/Sort/QuickSort.h"
#include "coproto/Socket/LocalAsyncSock.h"
#include "coproto/Socket/AsioSocket.h"
#include "macoro/sync_wait.h"
#include "macoro/when_all.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <tuple>

namespace
{
    using namespace secJoin;
    using Clock=std::chrono::steady_clock;
    const std::array<std::string,6> patterns={"random","equal","duplicates","sorted","reverse","max"};
    struct Options
    {
        u64 n=128,bits=32,terminal=8,reserve=0,pivots=1,batch=1<<16,concurrency=2,seed=1,left=~u64(0);
        int party=-1;
        std::string pattern="random",address="127.0.0.1:12124";
        bool plan=false,selfTest=false,help=false;
    };
    QuickSortOptions parameters(const Options& o){return {o.terminal,o.reserve,o.pivots};}
    u64 number(const std::string& s)
    {
        if(s.empty()||s[0]=='-')throw std::invalid_argument("Expected a nonnegative integer");
        std::size_t end=0;auto v=std::stoull(s,&end);
        if(end!=s.size())throw std::invalid_argument("Invalid integer: "+s);
        return v;
    }
    Options parse(int argc,char** argv)
    {
        Options o;
        for(int i=1;i<argc;++i)
        {
            std::string arg=argv[i];
            if(arg=="--plan"){o.plan=true;continue;}
            if(arg=="--self-test"){o.selfTest=true;continue;}
            if(arg=="--help"||arg=="-h"){o.help=true;continue;}
            if(i+1==argc)throw std::invalid_argument("Missing value for "+arg);
            std::string v=argv[++i];
            if(arg=="--n")o.n=number(v);else if(arg=="--bits")o.bits=number(v);
            else if(arg=="--terminal")o.terminal=number(v);else if(arg=="--reserve")o.reserve=number(v);
            else if(arg=="--pivots")o.pivots=number(v);
            else if(arg=="--batch-size")o.batch=number(v);else if(arg=="--concurrency")o.concurrency=number(v);
            else if(arg=="--seed")o.seed=number(v);else if(arg=="--left")o.left=number(v);
            else if(arg=="--pattern")o.pattern=v;else if(arg=="--address")o.address=v;
            else if(arg=="--party"){if(v!="0"&&v!="1")throw std::invalid_argument("party must be 0 or 1");o.party=v=="1";}
            else throw std::invalid_argument("Unknown option: "+arg);
        }
        QuickSort::plan(o.n,o.bits,parameters(o));
        if(o.left!=~u64(0)&&o.left>o.n)throw std::invalid_argument("left must be <= total n");
        if(std::find(patterns.begin(),patterns.end(),o.pattern)==patterns.end())throw std::invalid_argument("Unknown pattern");
        if(o.batch<1024||o.batch>(1<<26)||o.batch%256||!o.concurrency||o.concurrency>1024)
            throw std::invalid_argument("Invalid correlation batch/concurrency");
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
    void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
    double elapsed(Clock::time_point a,Clock::time_point b)
    {return std::chrono::duration<double,std::milli>(b-a).count();}
    struct Data{BinMatrix keys;std::vector<u32> expected;};
    Data dataset(const Options& o)
    {
        std::mt19937_64 rng(o.seed);std::vector<std::vector<u8>> values(o.n);
        auto less=[](const auto& a,const auto& b)
        {return std::lexicographical_compare(a.rbegin(),a.rend(),b.rbegin(),b.rend());};
        for(auto& row:values)
        {
            row.resize((o.bits+7)/8);
            for(auto& byte:row)byte=o.pattern=="max"?255:(o.pattern=="equal"||o.pattern=="duplicates"?0:rng());
            if(o.pattern=="duplicates")row[0]=rng()%4;
            if(o.bits%8)row.back()&=(1u<<(o.bits%8))-1;
        }
        if(o.pattern=="sorted"||o.pattern=="reverse")
        {std::sort(values.begin(),values.end(),less);if(o.pattern=="reverse")std::reverse(values.begin(),values.end());}
        if(o.left!=~u64(0))
        {std::sort(values.begin(),values.begin()+o.left,less);std::sort(values.begin()+o.left,values.end(),less);}
        Data d;d.keys.resize(o.n,o.bits);d.expected.resize(o.n);
        for(u64 i=0;i<o.n;++i)std::copy(values[i].begin(),values[i].end(),d.keys[i].begin());
        std::iota(d.expected.begin(),d.expected.end(),0);
        std::stable_sort(d.expected.begin(),d.expected.end(),[&](u32 a,u32 b){return less(values[a],values[b]);});return d;
    }
    macoro::task<> apply(QuickSort& p,const BinMatrix& keys,AdditivePerm& output,coproto::Socket& sock,u64 left)
    {
        if(left==~u64(0)){co_await p.sort(keys,output,sock);co_return;}
        BinMatrix x(left,keys.bitsPerEntry()),y(keys.rows()-left,keys.bitsPerEntry());
        if(x.size())std::copy(keys.data(),keys.data()+x.size(),x.data());
        if(y.size())std::copy(keys.data()+x.size(),keys.data()+keys.size(),y.data());
        co_await p.merge(x,y,output,sock);
    }
    void verify(const AdditivePerm& a,const AdditivePerm& b,const std::vector<u32>& expected)
    {
        require(a.size()==expected.size()&&b.size()==expected.size(),"Wrong output size");
        for(u64 i=0;i<a.size();++i)require((a.mShare[i]^b.mShare[i])==expected[i],"Stable gather permutation mismatch");
    }
    void publicFields(const Options& o,const QuickSortPlan& p)
    {
        std::cout<<",\"protocol\":\"shuffled_quicksort\",\"implementation_version\":1,\"n\":"<<o.n
            <<",\"key_bits\":"<<o.bits<<",\"comparison_bits\":"<<p.comparisonBits
            <<",\"terminal_size\":"<<p.terminalSize<<",\"expected_comparisons\":"<<p.expectedComparisons
            <<",\"pivots\":"<<p.pivotCount<<",\"leading_comparison_estimate\":"<<p.leadingComparisonEstimate
            <<",\"ands_per_comparison\":"<<p.andsPerComparison<<",\"rounds_per_comparison\":"<<p.roundsPerComparison
            <<",\"reserved_comparison_lanes\":"<<p.reservedComparisonLanes<<",\"reserved_binary_ole\":"<<p.reservedBinaryOle
            <<",\"shuffle_payload_bytes\":"<<p.shufflePayloadBytes<<",\"batch_size\":"<<o.batch<<",\"concurrency\":"<<o.concurrency;
    }
    void report(const Options& o,const QuickSort& p,double off,double on,u64 offBytes,u64 onBytes)
    {
        const auto& s=p.stats();std::cout<<std::fixed<<std::setprecision(3)<<"{\"type\":\"development_run\"";
        publicFields(o,p.plan());
        std::cout<<",\"real_crypto\":true,\"verified\":true,\"public_synthetic_inputs\":true,\"party\":"<<o.party
            <<",\"pattern\":\""<<o.pattern<<"\",\"public_seed\":"<<o.seed
            <<",\"comparisons\":"<<s.comparisons<<",\"comparison_batches\":"<<s.comparisonBatches
            <<",\"padded_ands\":"<<s.paddedAnds<<",\"online_rounds_without_refills\":"<<s.onlineRoundsWithoutRefills()
            <<",\"online_payload_bytes_without_refills\":"<<s.onlinePayloadBytes
            <<",\"refills\":"<<s.refills<<",\"refill_sent_bytes_this_party\":"<<s.refillSentBytes
            <<",\"refill_received_bytes_this_party\":"<<s.refillReceivedBytes<<",\"refill_ms_this_party\":"<<s.refillMilliseconds
            <<",\"extra_reserved_binary_ole\":"<<s.extraReservedBinaryOle
            <<",\"offline_ms\":"<<off<<",\"online_ms_including_refills\":"<<on
            <<",\"offline_sent_bytes\":"<<offBytes<<",\"online_sent_bytes_including_refills\":"<<onBytes
            <<",\"byte_counter_scope\":\""<<(o.party<0?"both_parties":"this_party")<<"\"}"<<std::endl;
    }
    void local(const Options& o,bool test=false,bool oddPool=false)
    {
        auto d=dataset(o);auto sockets=coproto::LocalAsyncSocket::makePair();
        PRNG prng[2]={PRNG(oc::sysRandomSeed()),PRNG(oc::sysRandomSeed())};
        BinMatrix keys[2];keys[0]=d.keys;keys[1].resize(o.n,o.bits);
        if(keys[1].size())prng[0].get(keys[1].data(),keys[1].size());
        // Intentionally leave unused high bits dirty; the protocol must ignore
        // them, including at widths immediately above/below powers of two.
        for(u64 i=0;i<keys[0].size();++i)keys[0](i)^=keys[1](i);
        CorGenerator cor[2];QuickSort p[2];AdditivePerm out[2];BinOleRequest unused[2];
        for(u64 role=0;role<2;++role)
        {
            cor[role].init(sockets[role].fork(),prng[role],role,o.concurrency,o.batch,false);
            if(oddPool){unused[role]=cor[role].binOleRequest(128);unused[role].start();}
            p[role].init(o.n,o.bits,cor[role],parameters(o));p[role].preprocess();
        }
        if(test)
        {
            rejects([&]{complete(p[0].sort(keys[0],out[0],sockets[0]));});
            rejects([&]{p[0].preprocess();});
        }
        auto begin=Clock::now();auto before=sockets[0].bytesSent()+sockets[1].bytesSent();
        complete(cor[0].start(),cor[1].start(),p[0].prepare(sockets[0],prng[0]),p[1].prepare(sockets[1],prng[1]));
        complete(sockets[0].flush(),sockets[1].flush());
        auto ready=Clock::now();auto prepared=sockets[0].bytesSent()+sockets[1].bytesSent();
        if(test)
        {
            BinMatrix bad(o.n+1,o.bits);
            rejects([&]{complete(p[0].sort(bad,out[0],sockets[0]));});
        }
        complete(apply(p[0],keys[0],out[0],sockets[0],o.left),apply(p[1],keys[1],out[1],sockets[1],o.left));
        complete(sockets[0].flush(),sockets[1].flush());auto end=Clock::now();
        auto online=sockets[0].bytesSent()+sockets[1].bytesSent()-prepared;
        verify(out[0],out[1],d.expected);
        auto& a=p[0].stats();auto& b=p[1].stats();
        require(a.comparisons==b.comparisons&&a.paddedAnds==b.paddedAnds&&a.refills==b.refills,"Party accounting differs");
        require(online>=a.onlinePayloadBytes+a.refillSentBytes+b.refillSentBytes,"Application payload exceeds transport count");
        require(a.onlinePayloadBytes==p[0].plan().shufflePayloadBytes+a.paddedAnds/2+a.openingPayloadBytes,"Incorrect payload formula");
        if(o.n>1&&o.reserve==1&&o.n>128)require(a.refills>0,"Forced refill was not exercised");
        if(test)
        {
            rejects([&]{complete(p[0].sort(keys[0],out[0],sockets[0]));});
            rejects([&]{complete(p[0].prepare(sockets[0],prng[0]));});
        }
        else report(o,p[0],elapsed(begin,ready),elapsed(ready,end),prepared-before,online);
    }
    void network(const Options& o)
    {
#ifdef COPROTO_ENABLE_BOOST
        auto socket=coproto::asioConnect(o.address,o.party==0);
        std::array<u64,12> config={0x515549434b0001ull,o.n,o.bits,o.terminal,o.reserve,o.batch,o.concurrency,o.seed,
            u64(std::find(patterns.begin(),patterns.end(),o.pattern)-patterns.begin()),o.left,o.pivots,u64(o.party)},peer{};
        complete(socket.send(coproto::copy(config)),socket.recv(peer));peer.back()^=1;
        require(config==peer,"Public configurations differ");
        auto d=dataset(o);BinMatrix keys(o.n,o.bits);if(!o.party)keys=d.keys;
        PRNG prng(oc::sysRandomSeed());CorGenerator cor;QuickSort p;AdditivePerm out,other;
        cor.init(socket.fork(),prng,o.party,o.concurrency,o.batch,false);p.init(o.n,o.bits,cor,parameters(o));p.preprocess();
        auto begin=Clock::now();auto before=socket.bytesSent();
        complete(cor.start(),p.prepare(socket,prng));complete(socket.flush());
        auto offEnd=Clock::now();auto prepared=socket.bytesSent();
        std::array<u64,1> barrier={1},otherBarrier{};
        complete(socket.send(coproto::copy(barrier)),socket.recv(otherBarrier));complete(socket.flush());
        require(barrier==otherBarrier,"Offline barrier mismatch");
        auto ready=Clock::now();auto onlineBefore=socket.bytesSent();
        complete(apply(p,keys,out,socket,o.left));complete(socket.flush());
        auto end=Clock::now();auto online=socket.bytesSent()-onlineBefore;
        // Only synthetic test verification opens outputs, after accounting.
        barrier[0]=2;complete(socket.send(coproto::copy(barrier)),socket.recv(otherBarrier));
        require(barrier==otherBarrier,"Online barrier mismatch");
        other.mShare.resize(o.n);
        if(o.n)complete(socket.send(coproto::copy(out.mShare)),socket.recv(other.mShare));
        verify(out,other,d.expected);report(o,p,elapsed(begin,offEnd),elapsed(ready,end),prepared-before,online);
#else
        throw std::runtime_error("TCP support requires Boost");
#endif
    }
    void selfTest()
    {
        {
            QuickSort p;rejects([&]{p.preprocess();});rejects([&]{p.stats();});
            rejects([]{QuickSort::plan(10,0);});rejects([]{QuickSort::plan(10,4097);});
            rejects([]{QuickSort::plan(~u32(0),32);});rejects([]{QuickSort::plan(10,32,{1,0});});
            rejects([]{QuickSort::plan(10,32,{33,0});});rejects([]{QuickSort::plan(10,32,{8,~u64(0)});});
            auto sockets=coproto::LocalAsyncSocket::makePair();PRNG prng(oc::sysRandomSeed());CorGenerator cor;
            rejects([&]{p.init(10,32,cor);});
            cor.init(sockets[0].fork(),prng,0,2,16384,true);rejects([&]{p.init(10,32,cor);});
            cor.mGenState->mMock=false;cor.mGenState->mDebug=true;rejects([&]{p.init(10,32,cor);});
        }
        // Independently compute expectation by the full recurrence, including
        // all-pairs boundary cases, and compare to the public closed form.
        for(u64 t:{2,3,8,16,32})
        {
            double sum=0;
            for(u64 n=0;n<300;++n)
            {
                double e=n<=t?double(n)*(n? n-1:0)/2:n-1+2*sum/n;
                require(std::abs(QuickSort::plan(n,32,{t,0}).expectedComparisons-e)<1e-7,"Wrong comparison expectation");sum+=e;
            }
        }
        u64 count=0;
        for(auto pattern:patterns)for(u64 n:{0,1,2,3,7,8,9,17,33,65,129})
        {
            Options o;o.n=n;o.pattern=pattern;o.batch=16384;
            o.bits=std::array<u64,8>{1,7,8,31,32,64,127,128}[count%8];
            o.terminal=std::array<u64,4>{2,8,16,32}[count%4];o.seed=++count;local(o,true);
        }
        for(u64 bits:{2,9,33,63,65,129,256})
        {Options o;o.n=35;o.bits=bits;o.batch=16384;o.seed=++count;local(o,true);}
        for(u64 left:{0,1,7,64,129})
        {Options o;o.n=129;o.left=left;o.pattern="duplicates";o.batch=16384;local(o,true);++count;}
        // Force repeated reserve exhaustion and uneven backend request offsets.
        for(bool odd:{false,true})
        {Options o;o.n=257;o.bits=7;o.terminal=8;o.reserve=1;o.batch=1024;local(o,true,odd);++count;}
        {Options o;o.n=33;o.bits=128;o.batch=1024;local(o,true,true);++count;}
        for(auto pattern:patterns)for(u64 n:{3,4,9,65,257})
        {Options o;o.n=n;o.bits=count%2?7:128;o.pivots=3;o.terminal=count%2?2:8;o.pattern=pattern;o.batch=16384;local(o,true);++count;}
        std::cout<<"{\"type\":\"self_test\",\"protocol\":\"shuffled_quicksort\",\"real_crypto_cases\":"<<count
            <<",\"passed\":true}"<<std::endl;
    }
}

int main(int argc,char** argv)
{
    try
    {
        auto o=parse(argc,argv);
        if(o.help)
            std::cout<<"shuffledquicksort --n TOTAL_ROWS [--bits 32] [--terminal 8] [--reserve 0] [--pivots 1|3]\n"
                <<"  [--left LEFT_ROWS] [--pattern random|equal|duplicates|sorted|reverse|max]\n"
                <<"  [--batch-size 65536] [--concurrency 2] [--seed 1]\n"
                <<"  [--party 0|1 --address 127.0.0.1:12124] [--plan] [--self-test]\n"
                <<"n is TOTAL rows. --left tests merging sorted runs of sizes left and n-left.\n"
                <<"--plan computes public costs only; actual depth depends on a private shuffle.\n"
                <<"Development runs use synthetic data, private crypto randomness and output verification.\n";
        else if(o.plan)
        {auto p=QuickSort::plan(o.n,o.bits,parameters(o));std::cout<<std::setprecision(15)<<"{\"type\":\"public_plan\"";publicFields(o,p);std::cout<<"}"<<std::endl;}
        else if(o.selfTest)selfTest();else if(o.party<0)local(o);else network(o);
        return 0;
    }
    catch(const std::exception& e){std::cerr<<"shuffledquicksort: "<<e.what()<<std::endl;return 1;}
}
