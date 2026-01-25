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
#include <filesystem> // Added for cleaner directory handling

// Configuration for large-scale testing
const size_t NUM_ORDERS = 10000000;      // 10 million orders
const uint32_t NUM_STOCKS = 10000;       // 10,000 unique stocks
const int32_t SNAPSHOT_FREQ = 100000;    // Snapshot every 100K orders

// Stuff bits: insert 0 after every 5 consecutive 1's
uint64_t stuffBits(uint64_t packet) {
    uint64_t result = 0;
    int outputBitPos = 0;
    int consecutiveOnes = 0;

    for (int i = 0; i < 49 && outputBitPos < 64; i++) {
        bool bit = (packet >> i) & 0x1;
        if (bit) {
            consecutiveOnes++;
            result |= (1ULL << outputBitPos);
            outputBitPos++;
            if (consecutiveOnes == 5) {
                outputBitPos++;
                consecutiveOnes = 0;
            }
        } else {
            consecutiveOnes = 0;
            outputBitPos++;
        }
    }
    return result;
}

// Encode order book entry into stuffed packet
uint64_t encodePacket(uint32_t stockID, bool orderType, uint8_t orderQty, uint8_t orderValue) {
    uint64_t packet = 0;
    packet |= (uint64_t)stockID;
    packet |= ((uint64_t)(orderType ? 1 : 0)) << 32;
    packet |= ((uint64_t)orderQty) << 33;
    packet |= ((uint64_t)orderValue) << 41;
    return stuffBits(packet);
}

// Structure to track expected values during generation
struct ExpectedStockStats {
    uint32_t stockID;
    uint8_t minSellValue = 255;
    uint8_t maxBuyValue = 0;
    uint8_t lastSellValue = 0;
    uint8_t lastBuyValue = 0;
    int64_t sumOrderValue = 0;
    int orderCount = 0;
    bool hasSell = false;
    bool hasBuy = false;
};

// Generate large dataset with tracked expected values
struct GeneratedData {
    std::vector<uint64_t> orderBook;
    int64_t expectedTotalAmount;
    std::unordered_map<uint32_t, ExpectedStockStats> expectedStats;
};

GeneratedData generateLargeOrderBookWithExpected(size_t numOrders, uint32_t numStocks, unsigned int seed = 42) {
    GeneratedData data;
    data.orderBook.reserve(numOrders);
    data.expectedTotalAmount = 0;

    std::mt19937 rng(seed);
    std::uniform_int_distribution<uint32_t> stockDist(1, numStocks);
    std::uniform_int_distribution<int> typeDist(0, 1);
    std::uniform_int_distribution<int> qtyDist(1, 255);
    std::uniform_int_distribution<int> valueDist(1, 255);

    std::cout << "Generating " << numOrders << " orders for " << numStocks << " stocks..." << std::endl;

    for (size_t i = 0; i < numOrders; i++) {
        uint32_t stockID = stockDist(rng);
        bool orderType = typeDist(rng) == 1;  // true = sell, false = buy
        uint8_t orderQty = static_cast<uint8_t>(qtyDist(rng));
        uint8_t orderValue = static_cast<uint8_t>(valueDist(rng));

        data.orderBook.push_back(encodePacket(stockID, orderType, orderQty, orderValue));

        // Track expected total amount
        data.expectedTotalAmount += (int64_t)orderQty * (int64_t)orderValue;

        // Track expected stats per stock
        ExpectedStockStats& stats = data.expectedStats[stockID];
        stats.stockID = stockID;
        stats.sumOrderValue += orderValue;
        stats.orderCount++;

        if (orderType) {  // Sell order
            stats.hasSell = true;
            stats.lastSellValue = orderValue;
            if (orderValue < stats.minSellValue) {
                stats.minSellValue = orderValue;
            }
        } else {  // Buy order
            stats.hasBuy = true;
            stats.lastBuyValue = orderValue;
            if (orderValue > stats.maxBuyValue) {
                stats.maxBuyValue = orderValue;
            }
        }
    }

    std::cout << "Generated " << data.orderBook.size() << " orders successfully." << std::endl;
    std::cout << "Tracking " << data.expectedStats.size() << " unique stocks.\n" << std::endl;
    return data;
}

// Generate large random dataset (without tracking - for performance tests)
std::vector<uint64_t> generateLargeOrderBook(size_t numOrders, uint32_t numStocks, unsigned int seed = 42) {
    std::vector<uint64_t> orderBook;
    orderBook.reserve(numOrders);

    std::mt19937 rng(seed);
    std::uniform_int_distribution<uint32_t> stockDist(1, numStocks);
    std::uniform_int_distribution<int> typeDist(0, 1);
    std::uniform_int_distribution<int> qtyDist(1, 255);
    std::uniform_int_distribution<int> valueDist(1, 255);

    std::cout << "Generating " << numOrders << " orders for " << numStocks << " stocks..." << std::endl;

    for (size_t i = 0; i < numOrders; i++) {
        uint32_t stockID = stockDist(rng);
        bool orderType = typeDist(rng) == 1;
        uint8_t orderQty = static_cast<uint8_t>(qtyDist(rng));
        uint8_t orderValue = static_cast<uint8_t>(valueDist(rng));

        orderBook.push_back(encodePacket(stockID, orderType, orderQty, orderValue));
    }

    std::cout << "Generated " << orderBook.size() << " orders successfully.\n" << std::endl;
    return orderBook;
}

// Create test output directory
void createTestDir() {
    // Ensure "output" directory exists
    if (!std::filesystem::exists("output")) {
        std::filesystem::create_directory("output");
    }
}

// Cleanup old snapshot files
void cleanupSnapshots(int maxSnaps = 200) {
    createTestDir();
    // Clean snapshots in output/ folder
    for (int i = 0; i < maxSnaps; i++) {
        std::string filename = "output/snap_" + std::to_string(i) + ".txt";
        std::remove(filename.c_str());
    }
    // Clean stats.txt in current directory
    std::remove("stats.txt");
}

// Debug: Verify encoding/decoding roundtrip for edge cases
void debugEncodingDecoding() {
    std::cout << "============================================" << std::endl;
    std::cout << "     DEBUG: Encoding/Decoding Verification  " << std::endl;
    std::cout << "============================================\n" << std::endl;

    // Test cases: stockID, orderType, orderQty, orderValue
    struct TestCase {
        uint32_t stockID;
        bool orderType;
        uint8_t orderQty;
        uint8_t orderValue;
    };

    std::vector<TestCase> testCases = {
        {1, true, 10, 100},          // Simple case
        {1, false, 5, 60},           // Simple case
        {1, true, 255, 255},         // Max qty and value
        {0xFFFFFFFF, true, 255, 255}, // All max values
        {0x1F1F1F1F, true, 255, 255}, // Alternating bit patterns
        {123, false, 200, 250},      // High values
        {456, true, 1, 1},           // Min values
    };

    std::cout << "StockID      | Type | Qty | Value | Decoded Qty | Decoded Val | Status" << std::endl;
    std::cout << "-------------|------|-----|-------|-------------|-------------|-------" << std::endl;

    int errors = 0;
    for (const auto& tc : testCases) {
        std::vector<uint64_t> singleOrder;
        singleOrder.push_back(encodePacket(tc.stockID, tc.orderType, tc.orderQty, tc.orderValue));

        int64_t result = totalAmountTraded(singleOrder);
        int64_t expected = (int64_t)tc.orderQty * (int64_t)tc.orderValue;

        bool pass = (result == expected);
        if (!pass) errors++;

        std::cout << std::setw(12) << tc.stockID << " | "
                  << std::setw(4) << (tc.orderType ? "sell" : "buy") << " | "
                  << std::setw(3) << (int)tc.orderQty << " | "
                  << std::setw(5) << (int)tc.orderValue << " | "
                  << std::setw(11) << "?" << " | "
                  << std::setw(11) << "?" << " | "
                  << (pass ? "[PASS]" : "[FAIL]") << " (expected " << expected << ", got " << result << ")"
                  << std::endl;
    }

    std::cout << "\nTotal errors: " << errors << "/" << testCases.size() << std::endl;
    std::cout << "============================================\n" << std::endl;
}

// Large-scale correctness test with verification
void runCorrectnessTest(size_t numOrders, uint32_t numStocks, int32_t snapshotFreq) {
    cleanupSnapshots();

    std::cout << "============================================" << std::endl;
    std::cout << "      LARGE-SCALE CORRECTNESS TEST          " << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << "Orders:     " << numOrders << std::endl;
    std::cout << "Stocks:     " << numStocks << std::endl;
    std::cout << "Snap Freq:  " << snapshotFreq << std::endl;
    std::cout << "Threads:    " << omp_get_max_threads() << std::endl;
    std::cout << "============================================\n" << std::endl;

    GeneratedData data = generateLargeOrderBookWithExpected(numOrders, numStocks);

    int totalTests = 0;
    int passedTests = 0;
    double start_time, end_time;

    // ==================== TEST 1: totalAmountTraded ====================
    std::cout << "1. Testing totalAmountTraded()" << std::endl;
    std::cout << "   Expected total: " << data.expectedTotalAmount << std::endl;

    start_time = omp_get_wtime();
    int64_t actualTotal = totalAmountTraded(data.orderBook);
    end_time = omp_get_wtime();

    std::cout << "   Actual total:   " << actualTotal << std::endl;
    bool test1Pass = (actualTotal == data.expectedTotalAmount);
    std::cout << "   Result: " << (test1Pass ? "[PASS]" : "[FAIL]") << std::endl;
    std::cout << "   Time: " << std::fixed << std::setprecision(2) << (end_time - start_time) * 1000 << " ms\n" << std::endl;
    totalTests++;
    if (test1Pass) passedTests++;

    // ==================== TEST 2: printOrderStats ====================
    std::cout << "2. Testing printOrderStats()" << std::endl;

    start_time = omp_get_wtime();
    printOrderStats(data.orderBook);
    end_time = omp_get_wtime();

    std::cout << "   Time: " << std::fixed << std::setprecision(2) << (end_time - start_time) * 1000 << " ms" << std::endl;

    // Check stats.txt in CURRENT directory
    std::ifstream statsFile("stats.txt");
    int statsErrors = 0;
    int statsLinesChecked = 0;

    if (statsFile.is_open()) {
        std::string line;
        uint32_t prevStockID = 0;
        bool orderError = false;

        while (std::getline(statsFile, line)) {
            std::istringstream iss(line);
            uint32_t stockID;
            int minSell, maxBuy;
            double avgValue;

            if (iss >> stockID >> minSell >> maxBuy >> avgValue) {
                statsLinesChecked++;

                if (stockID <= prevStockID && prevStockID != 0) {
                    if (!orderError) {
                        std::cout << "   ERROR: Stock IDs not in increasing order at stockID " << stockID << std::endl;
                        orderError = true;
                        statsErrors++;
                    }
                }
                prevStockID = stockID;

                auto it = data.expectedStats.find(stockID);
                if (it != data.expectedStats.end()) {
                    const ExpectedStockStats& expected = it->second;

                    int expectedMinSell = expected.hasSell ? expected.minSellValue : 0;
                    int expectedMaxBuy = expected.hasBuy ? expected.maxBuyValue : 0;
                    double expectedAvg = (double)expected.sumOrderValue / expected.orderCount;

                    if (minSell != expectedMinSell) statsErrors++;
                    if (maxBuy != expectedMaxBuy) statsErrors++;
                    if (std::abs(avgValue - expectedAvg) > 0.001) statsErrors++;
                } else {
                    statsErrors++;
                }
            }
        }
        statsFile.close();

        if ((size_t)statsLinesChecked != data.expectedStats.size()) statsErrors++;
    } else {
        std::cout << "   ERROR: Could not open stats.txt (Check if it was created in root)" << std::endl;
        statsErrors++;
    }

    bool test2Pass = (statsErrors == 0);
    std::cout << "   Verified " << statsLinesChecked << " stock entries" << std::endl;
    std::cout << "   Result: " << (test2Pass ? "[PASS]" : "[FAIL]") << "\n" << std::endl;
    totalTests++;
    if (test2Pass) passedTests++;

    // ==================== TEST 3: updateDisplay ====================
    std::cout << "3. Testing updateDisplay()" << std::endl;
    int expectedSnapshots = (numOrders % snapshotFreq == 0) ? (numOrders / snapshotFreq) : (numOrders / snapshotFreq + 1);
    std::cout << "   Expected snapshots: " << expectedSnapshots << std::endl;

    start_time = omp_get_wtime();
    updateDisplay(data.orderBook, snapshotFreq);
    end_time = omp_get_wtime();

    std::cout << "   Time: " << std::fixed << std::setprecision(2) << (end_time - start_time) * 1000 << " ms" << std::endl;

    int snapshotErrors = 0;
    int actualSnapshots = 0;

    // CHECK SNAPSHOTS IN output/ DIRECTORY
    for (int i = 0; i < expectedSnapshots + 5; i++) {
        std::string filename = "output/snap_" + std::to_string(i) + ".txt";
        std::ifstream snapFile(filename);

        if (snapFile.is_open()) {
            actualSnapshots++;
            std::string line;
            int prevSpread = INT32_MAX;
            uint32_t prevStockID = UINT32_MAX;

            while (std::getline(snapFile, line)) {
                std::istringstream iss(line);
                uint32_t stockID;
                int lastSell, lastBuy, spread;

                if (iss >> stockID >> lastSell >> lastBuy >> spread) {
                    int expectedSpread = std::abs(lastSell - lastBuy);
                    if (spread != expectedSpread) snapshotErrors++;
                    if (spread > prevSpread) snapshotErrors++;
                    else if (spread == prevSpread && stockID > prevStockID) snapshotErrors++;

                    prevSpread = spread;
                    prevStockID = stockID;
                }
            }
            snapFile.close();
        } else if (i < expectedSnapshots) {
            std::cout << "   ERROR: Missing snapshot file " << filename << std::endl;
            snapshotErrors++;
        }
    }

    if (actualSnapshots != expectedSnapshots) snapshotErrors++;

    bool test3Pass = (snapshotErrors == 0);
    std::cout << "   Verified " << actualSnapshots << " snapshot files in output/ folder" << std::endl;
    std::cout << "   Result: " << (test3Pass ? "[PASS]" : "[FAIL]") << "\n" << std::endl;
    totalTests++;
    if (test3Pass) passedTests++;

    // ==================== SUMMARY ====================
    std::cout << "============================================" << std::endl;
    std::cout << "            CORRECTNESS SUMMARY             " << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << "Passed: " << passedTests << "/" << totalTests << " tests" << std::endl;
    std::cout << "Overall: " << (passedTests == totalTests ? "[ALL PASSED]" : "[SOME FAILED]") << std::endl;
    std::cout << "============================================\n" << std::endl;
}

// Large-scale performance test
void runPerformanceTest(size_t numOrders, uint32_t numStocks, int32_t snapshotFreq) {
    cleanupSnapshots();

    std::cout << "============================================" << std::endl;
    std::cout << "         PERFORMANCE TEST (Large)           " << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << "Orders:     " << numOrders << std::endl;
    std::cout << "Stocks:     " << numStocks << std::endl;
    std::cout << "Snap Freq:  " << snapshotFreq << std::endl;
    std::cout << "Threads:    " << omp_get_max_threads() << std::endl;
    std::cout << "============================================\n" << std::endl;

    std::vector<uint64_t> orderBook = generateLargeOrderBook(numOrders, numStocks);
    double start_time, end_time, total_time;
    const int NUM_RUNS = 3;

    // Test 1: totalAmountTraded
    std::cout << "1. Testing totalAmountTraded() - Large Scale" << std::endl;
    totalAmountTraded(orderBook); // Warmup
    total_time = 0;
    for (int run = 0; run < NUM_RUNS; run++) {
        start_time = omp_get_wtime();
        totalAmountTraded(orderBook);
        end_time = omp_get_wtime();
        total_time += (end_time - start_time);
    }
    std::cout << "   Average time: " << std::fixed << std::setprecision(4)
              << (total_time / NUM_RUNS) * 1000.0 << " ms\n" << std::endl;

    // Test 2: printOrderStats
    std::cout << "2. Testing printOrderStats() - Large Scale" << std::endl;
    printOrderStats(orderBook); // Warmup
    total_time = 0;
    for (int run = 0; run < NUM_RUNS; run++) {
        start_time = omp_get_wtime();
        printOrderStats(orderBook);
        end_time = omp_get_wtime();
        total_time += (end_time - start_time);
    }
    std::cout << "   Average time: " << std::fixed << std::setprecision(4)
              << (total_time / NUM_RUNS) * 1000.0 << " ms\n" << std::endl;

    // Test 3: updateDisplay
    std::cout << "3. Testing updateDisplay() - Large Scale" << std::endl;
    updateDisplay(orderBook, snapshotFreq); // Warmup
    total_time = 0;
    for (int run = 0; run < NUM_RUNS; run++) {
        start_time = omp_get_wtime();
        updateDisplay(orderBook, snapshotFreq);
        end_time = omp_get_wtime();
        total_time += (end_time - start_time);
    }
    std::cout << "   Average time: " << std::fixed << std::setprecision(4)
              << (total_time / NUM_RUNS) * 1000.0 << " ms\n" << std::endl;
    
    std::cout << "============================================" << std::endl;
}

// Scalability test
void runScalabilityTest(size_t numOrders, uint32_t numStocks) {
    cleanupSnapshots();

    std::cout << "\n============================================" << std::endl;
    std::cout << "         SCALABILITY TEST                   " << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << "Threads: " << omp_get_max_threads() << std::endl;
    std::cout << "============================================\n" << std::endl;

    std::vector<uint64_t> orderBook = generateLargeOrderBook(numOrders, numStocks);
    const int NUM_RUNS = 5;
    double times[3][5];

    for (int run = 0; run < NUM_RUNS; run++) {
        double start, end;

        start = omp_get_wtime();
        totalAmountTraded(orderBook);
        end = omp_get_wtime();
        times[0][run] = (end - start) * 1000;

        start = omp_get_wtime();
        printOrderStats(orderBook);
        end = omp_get_wtime();
        times[1][run] = (end - start) * 1000;

        start = omp_get_wtime();
        updateDisplay(orderBook, 100000);
        end = omp_get_wtime();
        times[2][run] = (end - start) * 1000;

        std::cout << "Run " << (run + 1) << " complete." << std::endl;
    }

    double avgs[3] = {0, 0, 0};
    for (int i = 0; i < 3; i++) {
        for (int r = 0; r < NUM_RUNS; r++) avgs[i] += times[i][r];
        avgs[i] /= NUM_RUNS;
    }

    std::cout << "\nAVERAGE TIMES (" << NUM_RUNS << " runs):" << std::endl;
    std::cout << "totalAmountTraded: " << avgs[0] << " ms" << std::endl;
    std::cout << "printOrderStats:   " << avgs[1] << " ms" << std::endl;
    std::cout << "updateDisplay:     " << avgs[2] << " ms" << std::endl;
}

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [mode] [options]\n" << std::endl;
    std::cout << "Modes: correctness, performance, scalability, debug, all" << std::endl;
    std::cout << "Options: -n <orders> -s <stocks> -f <freq>" << std::endl;
}

int main(int argc, char* argv[]) {
    std::string mode = "performance";
    size_t numOrders = NUM_ORDERS;
    uint32_t numStocks = NUM_STOCKS;
    int32_t snapshotFreq = SNAPSHOT_FREQ;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "correctness" || arg == "performance" || arg == "scalability" || arg == "debug" || arg == "all") {
            mode = arg;
        } else if (arg == "-n" && i + 1 < argc) numOrders = std::stoull(argv[++i]);
        else if (arg == "-s" && i + 1 < argc) numStocks = std::stoul(argv[++i]);
        else if (arg == "-f" && i + 1 < argc) snapshotFreq = std::stoi(argv[++i]);
        else if (arg == "-h") { printUsage(argv[0]); return 0; }
    }

    std::cout << "Running mode: " << mode << std::endl;
    if (mode == "debug" || mode == "all") debugEncodingDecoding();
    if (mode == "correctness" || mode == "all") runCorrectnessTest(numOrders, numStocks, snapshotFreq);
    if (mode == "performance" || mode == "all") runPerformanceTest(numOrders, numStocks, snapshotFreq);
    if (mode == "scalability" || mode == "all") runScalabilityTest(numOrders, numStocks);

    return 0;
}