#include "parFunctions.h"
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <fstream>
#include <string>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <omp.h>
#include <numeric>
#include <memory>

using namespace std;

struct OrderBookEntry_Par {
    uint32_t stockID;
    bool orderType; 
    uint8_t orderQty;
    uint8_t orderValue;
};

struct StockInfo_Par {
    int64_t sumOrderValue;
    uint32_t stockID;      
    int orderCount;        
    uint8_t lastSellValue; 
    uint8_t lastBuyValue;  
    uint8_t minSellValue;  
    uint8_t maxBuyValue;   
    bool hasSellForMin;    
    bool hasBuyForMax;     
    
    StockInfo_Par() : sumOrderValue(0), stockID(0), orderCount(0),
                      lastSellValue(0), lastBuyValue(0), 
                      minSellValue(255), maxBuyValue(0),
                      hasSellForMin(false), hasBuyForMax(false) {}
};

// --- Helper Functions ---

inline uint64_t unstuffBits_Par(uint64_t packet) {
    uint64_t result = 0;
    int outputBitPos = 0;
    int consecutiveOnes = 0;
    for(int i = 0; i < 64 && outputBitPos < 49; ++i) {
        bool bit = (packet >> i) & 0x1;
        if (bit) {
            consecutiveOnes++;
            result |= (1ULL << outputBitPos++);
            if (consecutiveOnes == 5) { consecutiveOnes = 0; i++; }
        } else {
            consecutiveOnes = 0;
            outputBitPos++; 
        }
    }
    return result;
}

inline OrderBookEntry_Par decodePacket_Par(uint64_t packet) {
    return {
        static_cast<uint32_t>(packet & 0xFFFFFFFF),
        ((packet >> 32) & 0x1) == 1,
        static_cast<uint8_t>((packet >> 33) & 0xFF),
        static_cast<uint8_t>((packet >> 41) & 0xFF)
    };
}

// --- Part 1: Update Display (Optimized Tasking) ---

void updateDisplay_Par(const std::vector<uint64_t> &orderBook, int32_t freq) {
    #pragma omp parallel
    {
        #pragma omp single
        {
            std::unordered_map<uint32_t, StockInfo_Par> stockMap;
            int snapCount = 0;
            
            for (size_t i = 0; i < orderBook.size(); i++) {
                uint64_t unstuffed = unstuffBits_Par(orderBook[i]);
                OrderBookEntry_Par entry = decodePacket_Par(unstuffed);
                
                StockInfo_Par& stock = stockMap[entry.stockID];
                if (stock.orderCount == 0) stock.stockID = entry.stockID;
                stock.orderCount++;
                
                if (entry.orderType) { // Sell
                    stock.lastSellValue = entry.orderValue;
                    stock.hasSellForMin = true;
                    if (entry.orderValue < stock.minSellValue) stock.minSellValue = entry.orderValue;
                } else { // Buy
                    stock.lastBuyValue = entry.orderValue;
                    stock.hasBuyForMax = true;
                    if (entry.orderValue > stock.maxBuyValue) stock.maxBuyValue = entry.orderValue;
                }
                
                if ((i + 1) % freq == 0 || i == orderBook.size() - 1) {
                    // Manual snapshot: faster than letting OMP copy the map structure
                    auto snap = std::make_shared<std::vector<StockInfo_Par>>();
                    snap->reserve(stockMap.size());
                    for (auto const& [id, info] : stockMap) {
                        snap->push_back(info);
                    }

                    #pragma omp task firstprivate(snap, snapCount)
                    {
                        std::sort(snap->begin(), snap->end(), 
                            [](const StockInfo_Par& a, const StockInfo_Par& b) {
                                int spreadA = std::abs((int)a.lastSellValue - (int)a.lastBuyValue);
                                int spreadB = std::abs((int)b.lastSellValue - (int)b.lastBuyValue);
                                if (spreadA != spreadB) return spreadA > spreadB;
                                return a.stockID > b.stockID;
                            }
                        );
                        
                        std::ofstream outFile("output/par/snap_" + std::to_string(snapCount) + ".txt");
                        for (const auto& s : *snap) {
                            outFile << s.stockID << " " << (int)s.lastSellValue << " " 
                                    << (int)s.lastBuyValue << " " 
                                    << std::abs((int)s.lastSellValue - (int)s.lastBuyValue) << "\n";
                        }
                    }
                    snapCount++;
                }
            }
        } 
    }
}

// --- Part 2.1: Total Reduction ---

int64_t totalAmountTraded_Par(const std::vector<uint64_t> &orderBook) {
    int64_t total = 0;
    #pragma omp parallel for reduction(+:total) schedule(guided)
    for (size_t i = 0; i < orderBook.size(); i++) {
        uint64_t unstuffed = unstuffBits_Par(orderBook[i]);
        OrderBookEntry_Par entry = decodePacket_Par(unstuffed);
        total += (int64_t)entry.orderQty * (int64_t)entry.orderValue;
    }
    return total;
}

// --- Part 2.2: Order Stats (Parallel Merge) ---



void printOrderStats_Par(const std::vector<uint64_t> &orderBook) {
    int max_threads = omp_get_max_threads();
    std::vector<std::unordered_map<uint32_t, StockInfo_Par>> thread_maps(max_threads);
    
    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        auto& local_map = thread_maps[tid];
        
        #pragma omp for schedule(static)
        for (size_t i = 0; i < orderBook.size(); i++) {
            uint64_t unstuffed = unstuffBits_Par(orderBook[i]);
            OrderBookEntry_Par entry = decodePacket_Par(unstuffed);
            
            StockInfo_Par& s = local_map[entry.stockID];
            if (s.orderCount == 0) s.stockID = entry.stockID;
            
            if (entry.orderType) {
                if (!s.hasSellForMin || entry.orderValue < s.minSellValue) s.minSellValue = entry.orderValue;
                s.hasSellForMin = true;
            } else {
                if (entry.orderValue > s.maxBuyValue) s.maxBuyValue = entry.orderValue;
                s.hasBuyForMax = true;
            }
            s.sumOrderValue += (int64_t)entry.orderValue;
            s.orderCount++;
        }
    }
    
    // Step 1: Identify all unique keys in parallel
    std::unordered_map<uint32_t, bool> global_keys_map;
    for (const auto& lm : thread_maps) {
        for (auto const& [id, val] : lm) global_keys_map[id] = true;
    }
    
    std::vector<uint32_t> unique_ids;
    for (auto const& [id, _] : global_keys_map) unique_ids.push_back(id);

    // Step 2: Parallel Merge - each thread merges a slice of the Stock IDs
    std::vector<StockInfo_Par> final_stats(unique_ids.size());
    #pragma omp parallel for schedule(dynamic)
    for (size_t i = 0; i < unique_ids.size(); i++) {
        uint32_t id = unique_ids[i];
        StockInfo_Par merged;
        merged.stockID = id;
        
        for (int t = 0; t < max_threads; t++) {
            auto it = thread_maps[t].find(id);
            if (it != thread_maps[t].end()) {
                const auto& local = it->second;
                merged.sumOrderValue += local.sumOrderValue;
                merged.orderCount += local.orderCount;
                if (local.hasSellForMin) {
                    if (!merged.hasSellForMin || local.minSellValue < merged.minSellValue)
                        merged.minSellValue = local.minSellValue;
                    merged.hasSellForMin = true;
                }
                if (local.hasBuyForMax) {
                    if (local.maxBuyValue > merged.maxBuyValue)
                        merged.maxBuyValue = local.maxBuyValue;
                    merged.hasBuyForMax = true;
                }
            }
        }
        final_stats[i] = merged;
    }

    // Step 3: Sort by StockID for final output
    std::sort(final_stats.begin(), final_stats.end(), [](const StockInfo_Par& a, const StockInfo_Par& b) {
        return a.stockID < b.stockID;
    });
    
    std::ofstream outFile("output/par/stats.txt");
    outFile << std::fixed << std::setprecision(4);
    for (const auto& s : final_stats) {
        double avg = (s.orderCount > 0) ? (double)s.sumOrderValue / s.orderCount : 0.0;
        outFile << s.stockID << " " << (int)(s.hasSellForMin ? s.minSellValue : 0) << " "
                << (int)(s.hasBuyForMax ? s.maxBuyValue : 0) << " " << avg << "\n";
    }
}