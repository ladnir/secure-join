// Common single-thread runner for the paper's six merge protocols.
// Round audits use synchronous message waves, independently of timed TCP runs.
#include "secure-join/Sort/PiLogStar.h"
#include "secure-join/Sort/PiMedian.h"
#include "secure-join/Sort/RootMerge.h"
#include "secure-join/Sort/QuickSort.h"
#include "coproto/Socket/LocalAsyncSock.h"
#include "coproto/Socket/BufferingSocket.h"
#include "coproto/Socket/AsioSocket.h"
#include "macoro/sync_wait.h"
#include "macoro/when_all.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <sys/resource.h>

using namespace secJoin;
using Clock = std::chrono::steady_clock;
struct Options {
    std::string method="logstar", address="127.0.0.1:12123", leaf="batcher", pattern="random";
    u64 m=256,n=256,bits=32,block=4,base=16,depth=2,batch=1<<20,concurrency=2,seed=1,terminal=8,pivots=1,reserve=0;
    std::vector<u64> children,cubes;
    int party=-1; bool audit=false;
};
u64 integer(const std::string& v) {
    std::size_t end=0; if(v.empty()||v[0]=='-')throw std::invalid_argument("Expected unsigned integer");
    auto x=std::stoull(v,&end);if(end!=v.size())throw std::invalid_argument("Invalid integer");return x;
}
std::vector<u64> numbers(const std::string& v) {
    std::vector<u64> r;std::size_t a=0;
    do {auto b=v.find(',',a);r.push_back(integer(v.substr(a,b-a)));if(b==std::string::npos)break;a=b+1;}while(true);return r;
}
Options parse(int argc,char** argv) {
    Options o;
    for(int i=1;i<argc;++i) {
        std::string k=argv[i]; if(k=="--audit-rounds"){o.audit=true;continue;}
        if(i+1==argc)throw std::invalid_argument("Missing argument");
        std::string v=argv[++i];
        if(k=="--method")o.method=v;else if(k=="--address")o.address=v;else if(k=="--leaf")o.leaf=v;else if(k=="--pattern")o.pattern=v;
        else if(k=="--m")o.m=integer(v);else if(k=="--n")o.n=integer(v);else if(k=="--bits")o.bits=integer(v);
        else if(k=="--block")o.block=integer(v);else if(k=="--base-case")o.base=integer(v);else if(k=="--max-depth")o.depth=integer(v);
        else if(k=="--children")o.children=numbers(v);else if(k=="--cube-blocks")o.cubes=numbers(v);
        else if(k=="--batch-size")o.batch=integer(v);else if(k=="--concurrency")o.concurrency=integer(v);else if(k=="--seed")o.seed=integer(v);
        else if(k=="--terminal")o.terminal=integer(v);else if(k=="--pivots")o.pivots=integer(v);else if(k=="--reserve")o.reserve=integer(v);
        else if(k=="--party"){if(v!="0"&&v!="1")throw std::invalid_argument("Party must be 0/1");o.party=integer(v);}
        else throw std::invalid_argument("Unknown option: "+k);
    }
    if(!o.m||o.m>o.n||o.n>(1ull<<24)||o.bits<1||o.bits>64||o.batch<1024||o.batch>(1ull<<26)||o.batch%256||!o.concurrency)
        throw std::invalid_argument("Invalid dimensions or correlation configuration");
    if(o.audit&&o.party!=-1)throw std::invalid_argument("Round audits run locally");
    if(o.pattern!="random"&&o.pattern!="duplicates"&&o.pattern!="equal"&&o.pattern!="max")throw std::invalid_argument("Unknown pattern");
    return o;
}
struct Protocol {
    std::unique_ptr<PiLogStar> logstar;std::unique_ptr<PiMedian> median;
    std::unique_ptr<RootMerge> root;std::unique_ptr<QuickSort> quick;
    void init(const Options& o,CorGenerator& c) {
        if(o.method=="logstar") {if(o.m!=o.n)throw std::invalid_argument("Equal lengths required");logstar=std::make_unique<PiLogStar>();logstar->init(o.n,o.bits,c,{o.block,o.block,true,true});logstar->preprocess();}
        else if(o.method=="median") {if(o.m!=o.n)throw std::invalid_argument("Equal lengths required");median=std::make_unique<PiMedian>();PiMedianOptions p;
            p.baseCase=o.base;p.maxDepth=o.depth;p.childSizes=o.children;p.cubeBlocks=o.cubes;
            if(o.leaf!="batcher"&&o.leaf!="allpairs")throw std::invalid_argument("Unknown leaf");
            p.leaf=o.leaf=="allpairs"?PiMedianLeaf::AllPairs:PiMedianLeaf::Batcher;median->init(o.n,o.bits,c,p);median->preprocess();}
        else if(o.method=="quick") {quick=std::make_unique<QuickSort>();quick->init(o.m+o.n,o.bits,c,{o.terminal,o.reserve,o.pivots});quick->preprocess();}
        else {if(o.method!="cube"&&o.method!="sqrt"&&o.method!="batcher")throw std::invalid_argument("Unknown method");
            root=std::make_unique<RootMerge>(o.method=="cube"?RootMergeKind::CubeRoot:o.method=="sqrt"?RootMergeKind::SquareRoot:RootMergeKind::Batcher);
            root->init(o.m,o.n,o.bits,c,o.method=="batcher"?0:o.block);root->preprocess();}
    }
    macoro::task<> prepare(coproto::Socket& s,PRNG& p) {
        if(logstar)co_await logstar->prepare(s,p);else if(median)co_await median->prepare(s,p);
        else if(root)co_await root->prepare(s,p);else co_await quick->prepare(s,p);
    }
    macoro::task<> merge(const BinMatrix& x,const BinMatrix& y,AdditivePerm& out,coproto::Socket& s,PRNG& p) {
        if(logstar)co_await logstar->merge(x,y,out,s,p);else if(median)co_await median->merge(x,y,out,s,p);
        else if(root)co_await root->merge(x,y,out,s,p);else co_await quick->merge(x,y,out,s);
    }
    u64 rounds()const {return logstar?logstar->onlineRoundBound():median?median->onlineRoundBound():root?root->onlineRoundBound():quick->stats().onlineRoundsWithoutRefills();}
    u64 ands()const {return logstar?logstar->paddedAnds():median?median->paddedAnds():root?root->paddedAnds():quick->stats().paddedAnds;}
};
struct Data {BinMatrix x,y;std::vector<u32> expected;};
Data data(const Options& o) {
    std::mt19937_64 rng(o.seed);std::vector<u64> x(o.m),y(o.n);u64 mask=o.bits==64?~u64(0):(1ull<<o.bits)-1;
    for(auto* list:{&x,&y}){for(auto& v:*list)v=o.pattern=="max"?mask:o.pattern=="equal"?mask/2:o.pattern=="duplicates"?(rng()%8)&mask:rng()&mask;std::sort(list->begin(),list->end());}
    Data d;d.x.resize(o.m,o.bits);d.y.resize(o.n,o.bits);
    for(u64 i=0;i<o.m;++i)for(u64 j=0;j<d.x.bytesPerEntry();++j)d.x(i,j)=x[i]>>(8*j);
    for(u64 i=0;i<o.n;++i)for(u64 j=0;j<d.y.bytesPerEntry();++j)d.y(i,j)=y[i]>>(8*j);
    u64 i=0,j=0;while(i<o.m||j<o.n)if(j==o.n||(i<o.m&&x[i]<=y[j]))d.expected.push_back(i++);else d.expected.push_back(o.m+j++);return d;
}
void verify(const AdditivePerm& a,const AdditivePerm& b,const Data& d) {
    if(a.size()!=d.expected.size()||b.size()!=a.size())throw std::runtime_error("Wrong output length");
    for(u64 i=0;i<a.size();++i)if((a.mShare[i]^b.mShare[i])!=d.expected[i])throw std::runtime_error("Incorrect stable merge");
}
template<class T>macoro::task<> checked(T t) {
    try {co_await std::move(t);}
    catch(const std::exception& e){std::cerr<<"Protocol task failed: "<<e.what()<<std::endl;std::_Exit(2);}
}
template<class... T>macoro::task<> finish(coproto::Socket& s,T... ts) {
    auto r=co_await macoro::when_all_ready(checked(std::move(ts))...);std::apply([](auto&... q){(q.result(),...);},r);co_await s.flush();
}
double ms(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
struct Measurement {double setup=0,offline=0,online=0;u64 offBytes=0,onBytes=0,offRounds=0,onRounds=0;};
void report(const Options& o,const Protocol& p,const Measurement& t) {
    rusage r{};getrusage(RUSAGE_SELF,&r);
    std::cout<<std::fixed<<std::setprecision(3)<<"{\"type\":\""<<(o.audit?"round_audit":"benchmark")<<"\",\"method\":\""<<o.method
        <<"\",\"m\":"<<o.m<<",\"n\":"<<o.n<<",\"key_bits\":"<<o.bits<<",\"party\":"<<o.party
        <<",\"real_crypto\":true,\"verified\":true,\"os_threads\":1,\"transport_revision\":\"separate-streams\",\"seed\":"<<o.seed<<",\"batch_size\":"<<o.batch
        <<",\"correlation_concurrency\":"<<o.concurrency<<",\"setup_ms\":"<<t.setup<<",\"offline_ms\":"<<t.offline<<",\"online_ms\":"<<t.online
        <<",\"offline_sent_bytes\":"<<t.offBytes<<",\"online_sent_bytes\":"<<t.onBytes<<",\"peak_rss_kib\":"<<r.ru_maxrss
        <<",\"online_round_bound\":"<<p.rounds()<<",\"padded_ands\":"<<p.ands();
    if(o.audit)std::cout<<",\"offline_rounds\":"<<t.offRounds<<",\"online_rounds_measured\":"<<t.onRounds;
    if(p.quick)std::cout<<",\"refills\":"<<p.quick->stats().refills<<",\"comparisons\":"<<p.quick->stats().comparisons;
    std::cout<<"}"<<std::endl;
}
// All four outbound vectors are captured before any is delivered. Every wave
// therefore contains only messages independent of receives in that same wave.
template<class T>u64 auditDrive(std::array<coproto::BufferingSocket,2>& s,std::array<coproto::BufferingSocket,2>& cor,T task) {
    auto running=macoro::make_blocking(std::move(task));u64 rounds=0;
    for(;;) {
        auto a=s[0].getOutbound(),b=s[1].getOutbound(),c=cor[0].getOutbound(),d=cor[1].getOutbound();
        if(!a||!b||!c||!d)throw std::runtime_error("Closed audit transport");
        if(a->empty()&&b->empty()&&c->empty()&&d->empty()) {
            if(!running.m_handle.promise().m_is_set) {
                std::cerr<<"Audit made no progress after "<<rounds<<" rounds"<<std::endl;
                std::_Exit(2);
            }
            break;
        }
        ++rounds;if(!a->empty())s[1].processInbound(*a);if(!b->empty())s[0].processInbound(*b);
        if(!c->empty())cor[1].processInbound(*c);
        if(!d->empty())cor[0].processInbound(*d);
    }
    auto r=running.get();std::apply([](auto&... q){(q.result(),...);},r);return rounds;
}
template<class S>void local(const Options& o,S sockets,S corSockets) {
    auto d=data(o);PRNG prng[2]={PRNG(oc::sysRandomSeed()),PRNG(oc::sysRandomSeed())};
    BinMatrix x[2]={d.x,BinMatrix(o.m,o.bits)},y[2]={d.y,BinMatrix(o.n,o.bits)};
    // Fresh random XOR shares, independent of the public synthetic seed.
    prng[0].get(x[1].data(),x[1].size());prng[0].get(y[1].data(),y[1].size());x[1].trim();y[1].trim();
    for(u64 i=0;i<x[0].size();++i)x[0](i)^=x[1](i);
    for(u64 i=0;i<y[0].size();++i)y[0](i)^=y[1](i);
    CorGenerator c[2];Protocol p[2];AdditivePerm out[2];Measurement t;auto start=Clock::now();
    for(int i=0;i<2;++i){c[i].init(corSockets[i].fork(),prng[i],i,o.concurrency,o.batch,false);p[i].init(o,c[i]);}
    auto sent=[&]{return sockets[0].bytesSent()+sockets[1].bytesSent()+corSockets[0].bytesSent()+corSockets[1].bytesSent();};
    auto off=Clock::now();u64 before=sent();
    auto prepare=macoro::when_all_ready(checked(c[0].start()),checked(c[1].start()),checked(p[0].prepare(sockets[0],prng[0])),checked(p[1].prepare(sockets[1],prng[1])));
    if constexpr(std::is_same_v<S,std::array<coproto::BufferingSocket,2>>)t.offRounds=auditDrive(sockets,corSockets,std::move(prepare));
    else {auto r=macoro::sync_wait(std::move(prepare));std::apply([](auto&... q){(q.result(),...);},r);}
    {auto r=macoro::sync_wait(macoro::when_all_ready(sockets[0].flush(),sockets[1].flush(),corSockets[0].flush(),corSockets[1].flush()));std::apply([](auto&... q){(q.result(),...);},r);}
    auto on=Clock::now();u64 prepared=sent();
    auto merge=macoro::when_all_ready(checked(p[0].merge(x[0],y[0],out[0],sockets[0],prng[0])),checked(p[1].merge(x[1],y[1],out[1],sockets[1],prng[1])));
    if constexpr(std::is_same_v<S,std::array<coproto::BufferingSocket,2>>)t.onRounds=auditDrive(sockets,corSockets,std::move(merge));
    else {auto r=macoro::sync_wait(std::move(merge));std::get<0>(r).result();std::get<1>(r).result();}
    {auto r=macoro::sync_wait(macoro::when_all_ready(sockets[0].flush(),sockets[1].flush(),corSockets[0].flush(),corSockets[1].flush()));std::apply([](auto&... q){(q.result(),...);},r);}
    auto end=Clock::now();t.setup=ms(start,off);t.offline=ms(off,on);t.online=ms(on,end);t.offBytes=prepared-before;t.onBytes=sent()-prepared;
    verify(out[0],out[1],d);report(o,p[0],t);
}
macoro::task<coproto::Socket> connect(const Options& o,boost::asio::io_context& io,u64 offset) {
    auto colon=o.address.rfind(':');if(colon==std::string::npos)throw std::invalid_argument("Expected host:port");
    auto port=integer(o.address.substr(colon+1))+offset;if(port>65535)throw std::invalid_argument("Port overflow");
    auto address=o.address.substr(0,colon+1)+std::to_string(port);
    if(o.party==0)co_return co_await coproto::AsioAcceptor(address,io,1);
    else co_return co_await coproto::AsioConnect(address,io);
}
macoro::task<> network(const Options& o,boost::asio::io_context& io,const char*& phase) {
    // All connections share the same tc-shaped interface and one io_context.
    // Separate streams prevent a not-yet-posted protocol receive from blocking
    // correlation messages needed to reach that receive. Harness control is
    // also isolated, so it cannot block either measured data stream.
    phase="connect";auto control=co_await connect(o,io,0);
    auto s=co_await connect(o,io,1);auto cor=co_await connect(o,io,2);
    std::vector<u64> cfg={o.m,o.n,o.bits,o.block,o.base,o.depth,o.batch,o.concurrency,o.seed,o.terminal,o.pivots,o.reserve};
    for(auto v:o.children)cfg.push_back(v);
    cfg.push_back(~u64(0));
    for(auto v:o.cubes)cfg.push_back(v);
    for(auto ch:o.method+"/"+o.leaf+"/"+o.pattern)cfg.push_back(static_cast<unsigned char>(ch));
    std::vector<u64> peer(cfg.size());u64 length=cfg.size(),otherLength=0;
    co_await finish(control,control.send(coproto::copy(length)),control.recv(otherLength));if(length!=otherLength)throw std::runtime_error("Configuration length mismatch");
    co_await finish(control,control.send(coproto::copy(cfg)),control.recv(peer));if(cfg!=peer)throw std::runtime_error("Configuration mismatch");
    auto d=data(o);BinMatrix x(o.m,o.bits),y(o.n,o.bits);if(o.party==0)x=d.x;else y=d.y;
    PRNG rng(oc::sysRandomSeed());CorGenerator c;Protocol p;AdditivePerm out,other;Measurement t;auto start=Clock::now();
    c.init(cor.fork(),rng,o.party,o.concurrency,o.batch,false);p.init(o,c);
    auto sent=[&]{return s.bytesSent()+cor.bytesSent();};
    phase="preprocessing";auto off=Clock::now();u64 before=sent();
    co_await finish(s,c.start(),p.prepare(s,rng));co_await cor.flush();
    auto offEnd=Clock::now();t.offBytes=sent()-before;
    phase="offline barrier";u64 barrier=1,peerBarrier=0;
    co_await finish(control,control.send(coproto::copy(barrier)),control.recv(peerBarrier));if(peerBarrier!=barrier)throw std::runtime_error("Barrier mismatch");
    phase="online";auto on=Clock::now();before=sent();co_await finish(s,p.merge(x,y,out,s,rng));co_await cor.flush();auto end=Clock::now();t.onBytes=sent()-before;
    t.setup=ms(start,off);t.offline=ms(off,offEnd);t.online=ms(on,end);
    phase="verification";barrier=2;co_await finish(control,control.send(coproto::copy(barrier)),control.recv(peerBarrier));if(peerBarrier!=barrier)throw std::runtime_error("Barrier mismatch");
    other.mShare.resize(out.size());co_await finish(control,control.send(coproto::copy(out.mShare)),control.recv(other.mShare));verify(out,other,d);report(o,p,t);phase="complete";
}
int main(int argc,char** argv) {
    try {
        auto o=parse(argc,argv);
        if(o.party<0) {if(o.audit)local(o,std::array<coproto::BufferingSocket,2>{},std::array<coproto::BufferingSocket,2>{});else local(o,coproto::LocalAsyncSocket::makePair(),coproto::LocalAsyncSocket::makePair());}
        else {
            // The calling thread executes every protocol and I/O callback.
            // No global coproto context (which starts worker threads) is used.
            boost::asio::io_context io;const char* phase="startup";auto running=macoro::make_blocking(network(o,io,phase));
            io.run();
            if(!running.m_handle.promise().m_is_set){std::cerr<<"I/O stalled during "<<phase<<std::endl;std::_Exit(2);}
            running.get();
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
