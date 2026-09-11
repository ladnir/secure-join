// Exact comparison counts from the same protocol constructors as paper_benchmark.
// This performs public schedule construction only: no preprocessing or online run.
#include "secure-join/Sort/PiLogStar.h"
#include "secure-join/Sort/PiMedian.h"
#include "coproto/Socket/LocalAsyncSock.h"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace secJoin;

u64 integer(const std::string& value) {
    std::size_t end = 0;
    if (value.empty() || value[0] == '-') throw std::invalid_argument("Expected unsigned integer");
    auto n = std::stoull(value, &end);
    if (end != value.size()) throw std::invalid_argument("Invalid integer");
    return n;
}
std::vector<u64> numbers(const std::string& value) {
    std::vector<u64> result;
    std::size_t start = 0;
    do {
        auto end = value.find(',', start);
        result.push_back(integer(value.substr(start, end - start)));
        if (end == std::string::npos) return result;
        start = end + 1;
    } while (true);
}
int main(int argc, char** argv) {
    try {
        std::string method, leaf = "batcher";
        u64 n = 0, bits = 32, block = 4, batch = 1 << 20, concurrency = 2;
        PiMedianOptions medianOptions;
        for (int i = 1; i < argc; ++i) {
            std::string key = argv[i];
            if (i + 1 == argc) throw std::invalid_argument("Missing argument");
            std::string value = argv[++i];
            if (key == "--method") method = value;
            else if (key == "--n") n = integer(value);
            else if (key == "--bits") bits = integer(value);
            else if (key == "--block") block = integer(value);
            else if (key == "--batch-size") batch = integer(value);
            else if (key == "--concurrency") concurrency = integer(value);
            else if (key == "--base-case") medianOptions.baseCase = integer(value);
            else if (key == "--max-depth") medianOptions.maxDepth = integer(value);
            else if (key == "--children") medianOptions.childSizes = numbers(value);
            else if (key == "--cube-blocks") medianOptions.cubeBlocks = numbers(value);
            else if (key == "--leaf") leaf = value;
            else throw std::invalid_argument("Unknown option: " + key);
        }
        auto sockets = coproto::LocalAsyncSocket::makePair();
        PRNG prng(oc::sysRandomSeed());
        CorGenerator cor;
        cor.init(sockets[0].fork(), prng, 0, concurrency, batch, false);
        u64 comparisons, ands, rounds;
        if (method == "logstar") {
            PiLogStar protocol;
            protocol.init(n, bits, cor, {block, block, true, true});
            comparisons = protocol.comparisons();
            ands = protocol.paddedAnds(); rounds = protocol.onlineRoundBound();
        } else if (method == "median") {
            if (leaf != "batcher" && leaf != "allpairs") throw std::invalid_argument("Unknown leaf");
            medianOptions.leaf = leaf == "allpairs" ? PiMedianLeaf::AllPairs : PiMedianLeaf::Batcher;
            PiMedian protocol;
            protocol.init(n, bits, cor, medianOptions);
            comparisons = protocol.comparisons();
            ands = protocol.paddedAnds(); rounds = protocol.onlineRoundBound();
        } else throw std::invalid_argument("Use --method logstar or --method median");
        auto& requests = *cor.mGenState;
        std::cout << "{\"type\":\"implementation_comparison_count\",\"method\":\"" << method
            << "\",\"n\":" << n << ",\"key_bits\":" << bits << ",\"comparisons\":" << comparisons
            << ",\"padded_ands\":" << ands << ",\"online_round_bound\":" << rounds
            << ",\"offline_requests_per_party\":{\"binary_ole\":" << requests.mNumOle
            << ",\"random_ot\":" << requests.mNumOt << ",\"f4_bit_ot\":" << requests.mNumF4BitOt
            << ",\"trit_ot\":" << requests.mNumTritOt << ",\"batches\":" << requests.mBatches.size()
            << "}}" << std::endl;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
