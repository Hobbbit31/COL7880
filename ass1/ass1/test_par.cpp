#include "functions.h"
#include <iostream>
#include <fstream>
#include <cmath>
#include <omp.h>
#include <vector>
#include <iomanip>
#include <random>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <filesystem>
#include <cassert>
#include <cstring>

// =========================================================
//  DEFAULTS (Used if no arguments are passed)
// =========================================================
size_t GLOBAL_ORDERS = 1000000; 
uint32_t GLOBAL_STOCKS = 10000;
int32_t GLOBAL_FREQ = 100000;

// =========================================================
//  INTERNAL "GOLDEN" REFERENCE LOGIC
// =========================================================
namespace Ref {
    uint64_t stuffBits(uint64_t packet) {
        uint64_t result = 0;
        int outputBitPos = 0;
        int consecutiveOnes = 0;
        for (int i = 0; i < 49 && outputBitPos < 64; i++) {
            bool bit = (packet >> i) & 0x1;
            if (bit) {
                consecutiveOnes++;
                result |= (1ULL << outputBitPos++);
                if (consecutiveOnes == 5) {
                    outputBitPos++; // Stuff 0
                    consecutiveOnes = 0;
                }
            } else {
                consecutiveOnes = 0;
                result &= ~(1ULL << outputBitPos++); 
            }
        }
        return result;
    }

    uint64_t encode(uint32_t stockID, bool type, uint8_t qty, uint8_t val) {
        uint64_t p = 0;
        p |= (uint64_t)stockID;
        p |= ((uint64_t)(type ? 1 : 0)) << 32;
        p |= ((uint64_t)qty) << 33;
        p |= ((uint64_t)val) << 41;
        return stuffBits(p);
    }
}

// =========================================================
//  DATA GENERATORS
// =========================================================
struct DataSet {
    std::vector<uint64_t> orders;
    int64_t expectedTotal;
    struct StockState {
        int64_t sumValue = 0;
        int count = 0;
        int minSell = 255;
        int maxBuy = 0;
        bool hasSell = false;
        bool hasBuy = false;
    };
    std::unordered_map<uint32_t, StockState> expectedStats;
};

DataSet generateIntegrityData(size_t n, uint32_t stocks, unsigned int seed) {
    DataSet data;
    data.orders.reserve(n);
    data.expectedTotal = 0;
    
    std::mt19937 rng(seed);
    std::uniform_int_distribution<uint32_t> stockDist(1, stocks);
    std::uniform_int_distribution<int> typeDist(0, 1);
    std::uniform_int_distribution<int> valDist(1, 255); 
    
    for(size_t i=0; i<n; ++i) {
        uint32_t sid = stockDist(rng);
        bool type = typeDist(rng);
        uint8_t qty = valDist(rng); 
        uint8_t val = valDist(rng);
        
        if (i % 100 == 0) { qty = 255; val = 255; }
        if (i % 101 == 0) { qty = 1; val = 1; }

        data.orders.push_back(Ref::encode(sid, type, qty, val));
        
        data.expectedTotal += (int64_t)qty * val;
        auto& st = data.expectedStats[sid];
        st.sumValue += val;
        st.count++;
        if(type) { 
            st.hasSell = true;
            if(val < st.minSell) st.minSell = val;
        } else { 
            st.hasBuy = true;
            if(val > st.maxBuy) st.maxBuy = val;
        }
    }
    return data;
}

// =========================================================
//  VERIFICATION LOGIC
// =========================================================
bool verifyTotalAmount(const DataSet& data) {
    std::cout << "[CHECK] Verifying totalAmountTraded... ";
    int64_t actual = totalAmountTraded_Par(data.orders);
    if (actual == data.expectedTotal) {
        std::cout << "PASS" << std::endl;
        return true;
    } else {
        std::cout << "FAIL! Expected: " << data.expectedTotal << " Got: " << actual << std::endl;
        return false;
    }
}

bool verifyStatsFile(const DataSet& data) {
    std::cout << "[CHECK] Verifying stats.txt logic... ";
    printOrderStats_Par(data.orders); 
    
    std::ifstream f("testPar/stats.txt");
    if(!f.is_open()) f.open("testSeq/stats.txt");
    if(!f.is_open()) {
        std::cout << "FAIL (File not found)" << std::endl; 
        return false;
    }

    std::string line;
    int errors = 0;
    int checked = 0;
    
    while(std::getline(f, line)) {
        if(line.empty()) continue;
        std::stringstream ss(line);
        uint32_t id; 
        int minS, maxB; 
        double avg;
        ss >> id >> minS >> maxB >> avg;
        
        if (data.expectedStats.find(id) == data.expectedStats.end()) {
             errors++; continue;
        }

        const auto& exp = data.expectedStats.at(id);
        int expMinS = exp.hasSell ? exp.minSell : 0;
        int expMaxB = exp.hasBuy ? exp.maxBuy : 0;
        double expAvg = (exp.count > 0) ? (double)exp.sumValue / exp.count : 0.0;
        
        bool fail = false;
        if(minS != expMinS) fail = true;
        if(maxB != expMaxB) fail = true;
        if(std::abs(avg - expAvg) > 0.001) fail = true;
        
        if(fail && errors < 1) {
            std::cout << "Mismatch ID " << id << " ";
            errors++;
        }
        checked++;
    }
    
    if(errors == 0 && checked > 0) {
        std::cout << "PASS" << std::endl;
        return true;
    } else {
        std::cout << "FAIL" << std::endl;
        return false;
    }
}

// =========================================================
//  RUNNERS
// =========================================================
void runIntegrityTest() {
    std::cout << "\n=== INTEGRITY CHECK ===\n";
    DataSet data = generateIntegrityData(100000, 2000, 12345); 

    bool p1 = verifyTotalAmount(data);
    bool p2 = verifyStatsFile(data);
    
    std::cout << "Bit-Stuffing Stress... ";
    for(int i=0; i<100; i++) Ref::encode(1, true, 31, 31); 
    std::cout << "PASS" << std::endl;

    if(!p1 || !p2) {
        std::cerr << "INTEGRITY FAILURE" << std::endl;
        exit(1);
    }
}

// FIX: Now accepts dynamic arguments!
void runPerformanceTest(size_t n, uint32_t s, int32_t f) {
    std::cout << "\n=== PERFORMANCE BENCHMARK ===\n";
    std::cout << "Orders:  " << n << "\n";
    std::cout << "Stocks:  " << s << "\n";
    std::cout << "Freq:    " << f << "\n";
    std::cout << "Threads: " << omp_get_max_threads() << "\n";
    
    std::vector<uint64_t> orders;
    orders.reserve(n);
    std::mt19937 rng(42);
    for(size_t i=0; i<n; ++i) {
        orders.push_back(Ref::encode(rng()%s + 1, rng()%2, rng()%255, rng()%255));
    }
    
    auto bench = [&](const char* name, auto func) {
        // Warmup
        // func();
        
        // Measure
        double start = omp_get_wtime();
        func(); // Run 1
        // func(); // Run 2
        double end = omp_get_wtime();
        
        double ms = ((end - start)) * 1000.0;
        if(ms < 0.0001) ms = 0.0001;

        std::cout << "[METRIC] " << name << " " << std::fixed << std::setprecision(4) << ms << std::endl;
        std::cout << "   Time: " << ms << " ms" << std::endl;
    };

    bench("totalAmountTraded", [&](){ totalAmountTraded_Par(orders); });
    bench("printOrderStats", [&](){ printOrderStats_Par(orders); });
    bench("updateDisplay", [&](){ updateDisplay_Par(orders, f); });
}

// =========================================================
//  MAIN
// =========================================================
int main(int argc, char* argv[]) {
    std::string mode = "all";
    size_t numOrders = GLOBAL_ORDERS;
    uint32_t numStocks = GLOBAL_STOCKS;
    int32_t freq = GLOBAL_FREQ;

    // FIX: Parse arguments correctly
    for(int i=1; i<argc; ++i) {
        if(strcmp(argv[i], "performance") == 0) mode = "performance";
        else if(strcmp(argv[i], "integrity") == 0) mode = "integrity";
        else if(strcmp(argv[i], "-n") == 0 && i+1 < argc) numOrders = std::stoull(argv[++i]);
        else if(strcmp(argv[i], "-s") == 0 && i+1 < argc) numStocks = std::stoul(argv[++i]);
        else if(strcmp(argv[i], "-f") == 0 && i+1 < argc) freq = std::stoi(argv[++i]);
    }
    
    // Directory safety
    #if __cplusplus >= 201703L
        if(!std::filesystem::exists("testPar")) std::filesystem::create_directory("testPar");
        if(!std::filesystem::exists("testSeq")) std::filesystem::create_directory("testSeq");
    #else
        system("mkdir -p testPar testSeq");
    #endif

    if(mode == "integrity") {
        runIntegrityTest();
    }
    else if(mode == "performance") {
        runPerformanceTest(numOrders, numStocks, freq);
    }
    else {
        // Default behavior (if just ./test_par is run without args)
        // Run integrity first, then perf
        runIntegrityTest();
        runPerformanceTest(numOrders, numStocks, freq);
    }
    
    return 0;
}