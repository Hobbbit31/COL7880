#include "functions.h"
// #include <execution>
#include <unordered_map>
#include <iostream>
#include <omp.h>
#include <numeric>
#include <memory>

using namespace std;

struct OrderBookEntry_Par {
    uint32_t stockID;
    uint8_t orderQty;
    uint8_t orderValue;
    bool orderType; 
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
        static_cast<uint8_t>((packet >> 33) & 0xFF),
        static_cast<uint8_t>((packet >> 41) & 0xFF),
        ((packet >> 32) & 0x1) == 1
    };
}

// void updateDisplay_Par(const std::vector<uint64_t> &orderBook, int32_t freq) {
//     size_t n = orderBook.size();
//     if (n == 0) return;

//     // Phase 1: Parallel Pre-decoding using Guided Scheduling [cite: 44, 71]
//     // Guided helps because bit-unstuffing time varies per packet.
//     std::vector<OrderBookEntry_Par> decodedEntries(n);
//     #pragma omp parallel for schedule(guided)
//     for (size_t i = 0; i < n; ++i) {
//         decodedEntries[i] = decodePacket_Par(unstuffBits_Par(orderBook[i]));
//     }

//     // Phase 2: State Updates and Task-based I/O
//     #pragma omp parallel
//     {
//         #pragma omp single
//         {
//             std::unordered_map<uint32_t, StockInfo_Par> stockMap;
//             int snapCount = 0;

//             for (size_t i = 0; i < n; i++) {
//                 const auto& entry = decodedEntries[i];
//                 StockInfo_Par& stock = stockMap[entry.stockID];
//                 stock.stockID = entry.stockID;
                
//                 if (entry.orderType) { // Sell
//                     stock.lastSellValue = entry.orderValue;
//                 } else { // Buy
//                     stock.lastBuyValue = entry.orderValue;
//                 }

//                 // Snapshot trigger [cite: 35, 36]
//                 if ((i + 1) % freq == 0 || i == n - 1) {
//                     auto snap = std::make_shared<std::vector<StockInfo_Par>>();
//                     snap->reserve(stockMap.size());
//                     for (auto const& [id, info] : stockMap) snap->push_back(info);

//                     // Requirement: Generate 1 + floor(n/freq) files 
//                     bool needsExtra = (i == n - 1 && n % freq == 0);

//                     #pragma omp task firstprivate(snap, snapCount, needsExtra)
//                     {
//                         auto writeFunc = [&](int count) {
//                             // Sort by spread descending, tie-break by stockID descending [cite: 38]
//                             std::sort(snap->begin(), snap->end(), [](const StockInfo_Par& a, const StockInfo_Par& b) {
//                                 int spreadA = std::abs((int)a.lastSellValue - (int)a.lastBuyValue);
//                                 int spreadB = std::abs((int)b.lastSellValue - (int)b.lastBuyValue);
//                                 if (spreadA != spreadB) return spreadA > spreadB;
//                                 return a.stockID > b.stockID;
//                             });
                            
//                             std::ofstream outFile("output/par/snap_" + std::to_string(count) + ".txt"); 
//                             for (const auto& s : *snap) {
//                                 outFile << s.stockID << " " << (int)s.lastSellValue << " " 
//                                         << (int)s.lastBuyValue << " " 
//                                         << std::abs((int)s.lastSellValue - (int)s.lastBuyValue) << "\n"; 
//                             }
//                         };
//                         writeFunc(snapCount);
//                         if (needsExtra) writeFunc(snapCount + 1); // Mandatory extra snap 
//                     }
//                     snapCount++;
//                 }
//             }
//         } 
//     }
// }

void updateDisplay_Par(const std::vector<uint64_t> &orderBook, int32_t freq) {
    size_t n = orderBook.size();
    if (n == 0) return;

    

    // Phase 1: Parallel Pre-decoding
    // Pre-allocating to avoid reallocations
    std::vector<OrderBookEntry_Par> decodedEntries(n);

    // #pragma omp parallel for schedule(guided)
    // for (size_t i = 0; i < n; ++i) {
    //     decodedEntries[i] = decodePacket_Par(unstuffBits_Par(orderBook[i]));
    // }




    // Phase 1: Parallel Pre-decoding with Guided Scheduling and Safe Unrolling
    // #pragma omp parallel for schedule(guided)
    // for (size_t i = 0; i < (n / 4) * 4; i += 4) {
    //     // Unrolling allows the CPU to overlap multiple unstuffing operations
    //     decodedEntries[i]     = decodePacket_Par(unstuffBits_Par(orderBook[i]));
    //     decodedEntries[i + 1] = decodePacket_Par(unstuffBits_Par(orderBook[i + 1]));
    //     decodedEntries[i + 2] = decodePacket_Par(unstuffBits_Par(orderBook[i + 2]));
    //     decodedEntries[i + 3] = decodePacket_Par(unstuffBits_Par(orderBook[i + 3]));
    // }

    // // Remainder loop for safety (handles n % 4 entries)
    // for (size_t i = (n / 4) * 4; i < n; ++i) {
    //     decodedEntries[i] = decodePacket_Par(unstuffBits_Par(orderBook[i]));
    // }

    #pragma omp parallel for schedule(guided)
    for (size_t i = 0; i < (n / 8) * 8; i += 8) {
        // Unrolling allows the CPU to overlap multiple unstuffing operations
        decodedEntries[i]     = decodePacket_Par(unstuffBits_Par(orderBook[i]));
        decodedEntries[i + 1] = decodePacket_Par(unstuffBits_Par(orderBook[i + 1]));
        decodedEntries[i + 2] = decodePacket_Par(unstuffBits_Par(orderBook[i + 2]));
        decodedEntries[i + 3] = decodePacket_Par(unstuffBits_Par(orderBook[i + 3]));
        decodedEntries[i + 4] = decodePacket_Par(unstuffBits_Par(orderBook[i + 4]));
        decodedEntries[i + 5] = decodePacket_Par(unstuffBits_Par(orderBook[i + 5]));
        decodedEntries[i + 6] = decodePacket_Par(unstuffBits_Par(orderBook[i + 6]));
        decodedEntries[i + 7] = decodePacket_Par(unstuffBits_Par(orderBook[i + 7]));
    }

    // Remainder loop for safety (handles n % 4 entries)
    for (size_t i = (n / 8) * 8; i < n; ++i) {
        decodedEntries[i] = decodePacket_Par(unstuffBits_Par(orderBook[i]));
    }




    // Phase 2: Serial Map Updates & Task-based Snapshots
    #pragma omp parallel
    {
        #pragma omp single
        {
            std::unordered_map<uint32_t, StockInfo_Par> stockMap;
            int snapCount = 0;

            for (size_t i = 0; i < n; i++) {
                // Accessing pre-decoded data is now just a memory fetch
                const auto& entry = decodedEntries[i];
                
                StockInfo_Par& stock = stockMap[entry.stockID];
                if (stock.orderCount == 0) stock.stockID = entry.stockID;
                stock.orderCount++;
                
                if (entry.orderType) { // sell(1) 
                    stock.lastSellValue = entry.orderValue;
                    stock.hasSellForMin = true;
                    if (entry.orderValue < stock.minSellValue) stock.minSellValue = entry.orderValue;
                } else { // buy(0) 
                    stock.lastBuyValue = entry.orderValue;
                    stock.hasBuyForMax = true;
                    if (entry.orderValue > stock.maxBuyValue) stock.maxBuyValue = entry.orderValue;
                }
                
                // Snapshot trigger [cite: 36]
                if ((i + 1) % freq == 0 || i == n - 1) {
                    auto snap = std::make_shared<std::vector<StockInfo_Par>>();
                    snap->reserve(stockMap.size());
                    for (auto const& [id, info] : stockMap) snap->push_back(info);

                    // Requirement: 1 + floor(n/freq) files 
                    bool needsDuplicate = (n % freq == 0 && i == n - 1);

                    #pragma omp task firstprivate(snap, snapCount, needsDuplicate)
                    {
                        auto writeTask = [&](int count) {
                            // Sort by decreasing spread, tie-break by stockID 
                            std::sort(snap->begin(), snap->end(), [](const StockInfo_Par& a, const StockInfo_Par& b) {
                                int spreadA = std::abs((int)a.lastSellValue - (int)a.lastBuyValue);
                                int spreadB = std::abs((int)b.lastSellValue - (int)b.lastBuyValue);
                                if (spreadA != spreadB) return spreadA > spreadB;
                                return a.stockID > b.stockID;
                            });
                            
                            std::string filename = "testPar/snap_" + std::to_string(count) + ".txt";
                            std::ofstream outFile(filename);
                            for (const auto& s : *snap) {
                                outFile << s.stockID << " " << (int)s.lastSellValue << " " 
                                        << (int)s.lastBuyValue << " " 
                                        << std::abs((int)s.lastSellValue - (int)s.lastBuyValue) << "\n";
                            }
                        };

                        writeTask(snapCount);
                        if (needsDuplicate) writeTask(snapCount + 1);
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
    size_t n = orderBook.size();

    // #pragma omp parallel
    // {
    //     int64_t local_acc = 0; // Single local accumulator is usually enough for ILP
        
    //     // Guided scheduling helps if unstuffBits_Par execution time varies per packet
    //     #pragma omp for schedule(guided) nowait
    //     for (size_t i = 0; i < (n / 2) * 2; i += 2) {
    //         // Packet 1
    //         uint64_t u1 = unstuffBits_Par(orderBook[i]);
    //         OrderBookEntry_Par e1 = decodePacket_Par(u1);
    //         local_acc += (int64_t)e1.orderQty * e1.orderValue;

    //         // Packet 2
    //         uint64_t u2 = unstuffBits_Par(orderBook[i+1]);
    //         OrderBookEntry_Par e2 = decodePacket_Par(u2);
    //         local_acc += (int64_t)e2.orderQty * e2.orderValue;
    //     }
        
    //     #pragma omp atomic
    //     total += local_acc;
    // }

    // // Handle the remainder if the size is odd
    // if (n % 2 != 0) {
    //     uint64_t u = unstuffBits_Par(orderBook[n-1]);
    //     OrderBookEntry_Par e = decodePacket_Par(u);
    //     total += (int64_t)e.orderQty * e.orderValue;
    // }


    #pragma omp parallel
    {
        // Using two local accumulators can help the CPU avoid dependency stalls
        int64_t acc1 = 0;
        int64_t acc2 = 0 , acc3=0, acc4=0;


        // Unrolling by 4 for better ILP
        #pragma omp for schedule(guided) nowait
        for (size_t i = 0; i < (n / 4) * 4; i += 4) {
            // Block 1 & 2
            uint64_t u0 = unstuffBits_Par(orderBook[i]);
            OrderBookEntry_Par e0 = decodePacket_Par(u0);
            acc1 += (int64_t)e0.orderQty * e0.orderValue; 

            uint64_t u1 = unstuffBits_Par(orderBook[i+1]);
            OrderBookEntry_Par e1 = decodePacket_Par(u1);
            acc2 += (int64_t)e1.orderQty * e1.orderValue; 

            // Block 3 & 4
            uint64_t u2 = unstuffBits_Par(orderBook[i+2]);
            OrderBookEntry_Par e2 = decodePacket_Par(u2);
            acc3 += (int64_t)e2.orderQty * e2.orderValue; 

            uint64_t u3 = unstuffBits_Par(orderBook[i+3]);
            OrderBookEntry_Par e3 = decodePacket_Par(u3);
            acc4 += (int64_t)e3.orderQty * e3.orderValue; 
        }
        
        #pragma omp atomic
        total += (acc1 + acc2 + acc3 + acc4);
    }

    // Cleanup loop for remaining elements (n % 4)
    for (size_t i = (n / 4) * 4; i < n; ++i) {
        uint64_t u = unstuffBits_Par(orderBook[i]);
        OrderBookEntry_Par e = decodePacket_Par(u);
        total += (int64_t)e.orderQty * e.orderValue; 
    }


    // #pragma omp parallel
    // {
    //     // Using two local accumulators can help the CPU avoid dependency stalls
    //     int64_t acc1 = 0;
    //     int64_t acc2 = 0 , acc3=0, acc4=0 , acc5=0, acc6=0, acc7=0, acc8=0;


    //     // Unrolling by 4 for better ILP
    //     #pragma omp for schedule(guided) nowait
    //     for (size_t i = 0; i < (n / 8) * 8; i += 8) {
    //         // Block 1 & 2
            
    //         OrderBookEntry_Par e0 = decodePacket_Par(unstuffBits_Par(orderBook[i]));
    //         acc1 += (int64_t)e0.orderQty * e0.orderValue; 

    //         OrderBookEntry_Par e1 = decodePacket_Par(unstuffBits_Par(orderBook[i+1]));
    //         acc2 += (int64_t)e1.orderQty * e1.orderValue; 

    //         // Block 3 & 4
    //         OrderBookEntry_Par e2 = decodePacket_Par(unstuffBits_Par(orderBook[i+2]));
    //         acc3 += (int64_t)e2.orderQty * e2.orderValue; 

    //         OrderBookEntry_Par e3 = decodePacket_Par(unstuffBits_Par(orderBook[i+3]));
    //         acc4 += (int64_t)e3.orderQty * e3.orderValue; 

    //         // Block 5 & 6
    //         OrderBookEntry_Par e4 = decodePacket_Par(unstuffBits_Par(orderBook[i+4]));
    //         acc5 += (int64_t)e4.orderQty * e4.orderValue; 

    //         OrderBookEntry_Par e5 = decodePacket_Par(unstuffBits_Par(orderBook[i+5]));
    //         acc6 += (int64_t)e5.orderQty * e5.orderValue;  

    //         // Block 7 & 8
    //         OrderBookEntry_Par e6 = decodePacket_Par(unstuffBits_Par(orderBook[i+6]));
    //         acc7 += (int64_t)e6.orderQty * e6.orderValue; 

    //         OrderBookEntry_Par e7 = decodePacket_Par(unstuffBits_Par(orderBook[i+7]));
    //         acc8 += (int64_t)e7.orderQty * e7.orderValue;   
    //     }
        
    //     #pragma omp atomic
    //     total += (acc1 + acc2 + acc3 + acc4 + acc5 + acc6 + acc7 + acc8);
    // }

    // // Cleanup loop for remaining elements (n % 4)
    // for (size_t i = (n / 8) * 8; i < n; ++i) {
    //     OrderBookEntry_Par e = decodePacket_Par(unstuffBits_Par(orderBook[i]));
    //     total += (int64_t)e.orderQty * e.orderValue; 
    // }



    // #pragma omp parallel
    // {
       
        
    //     // Guided scheduling helps if unstuffBits_Par execution time varies per packet
    //     #pragma omp for schedule(guided) reduction(+:total)
    //     for (size_t i = 0; i < n; i++) {
    //         uint64_t u = unstuffBits_Par(orderBook[i]);
    //         OrderBookEntry_Par e = decodePacket_Par(u);
    //         total += (int64_t)e.orderQty * e.orderValue;
    //     }
    // }

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
    #pragma omp parallel for schedule(guided)
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
    
    std::ofstream outFile("testPar/stats.txt");
    outFile << std::fixed << std::setprecision(3);
    for (const auto& s : final_stats) {
        double avg = (s.orderCount > 0) ? (double)s.sumOrderValue / s.orderCount : 0.0;
        outFile << s.stockID << " " << (int)(s.hasSellForMin ? s.minSellValue : 0) << " "
                << (int)(s.hasBuyForMax ? s.maxBuyValue : 0) << " " << avg << "\n";
    }
}