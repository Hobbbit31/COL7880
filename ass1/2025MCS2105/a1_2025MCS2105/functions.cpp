#include "functions.h"
#include <unordered_map>
#include <omp.h>
#include <numeric>
#include <memory>
#include <algorithm>

using namespace std;

struct OrderBookEntry {
    uint32_t stockID;
    uint8_t orderQty;
    uint8_t orderValue;
    bool orderType; 
};

struct StockInfo {
    int64_t sumOrderValue;
    uint32_t stockID;      
    int orderCount;        
    uint8_t lastSellValue; 
    uint8_t lastBuyValue;  
    uint8_t minSellValue;  
    uint8_t maxBuyValue;   
    bool hasSellForMin;    
    bool hasBuyForMax;     
    StockInfo() : sumOrderValue(0), stockID(0), orderCount(0),
                  lastSellValue(0), lastBuyValue(0), 
                  minSellValue(255), maxBuyValue(0),
                  hasSellForMin(false), hasBuyForMax(false) {}
    int getSpread() const {
        return abs((int)lastSellValue - (int)lastBuyValue);
    }
};

// --- Helper Functions ---

inline uint64_t unstuffBits(uint64_t packet) {
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

inline OrderBookEntry decodePacket(uint64_t packet) {
    return {
        static_cast<uint32_t>(packet & 0xFFFFFFFF),
        static_cast<uint8_t>((packet >> 33) & 0xFF),
        static_cast<uint8_t>((packet >> 41) & 0xFF),
        ((packet >> 32) & 0x1) == 1
    };
}



//sequential fnuctions
int64_t totalAmountTradedC(const std::vector<uint64_t> &orderBook) {
    int64_t total = 0;
    
    for (uint64_t packet : orderBook) {
        // uint64_t unstuffed = unstuffBits(packet);
        OrderBookEntry entry = decodePacket(unstuffBits(packet));

        total += (int64_t)entry.orderQty * (int64_t)entry.orderValue;
    }
    
    return total;
}

void updateDisplayC(const std::vector<uint64_t> &orderBook, int32_t freq) {
    std::map<uint32_t, StockInfo> stockMap;
    int snapCount = 0;
    
    for (size_t i = 0; i < orderBook.size(); i++) {
        // uint64_t unstuffed = unstuffBits(orderBook[i]);
        OrderBookEntry entry = decodePacket(unstuffBits(orderBook[i]));

        StockInfo& stock = stockMap[entry.stockID];
        stock.stockID = entry.stockID;
        
        if (entry.orderType) {
            stock.lastSellValue = entry.orderValue;
            stock.hasSellForMin = true;
            if (entry.orderValue < stock.minSellValue) {
                stock.minSellValue = entry.orderValue;
            }
        } else {
            stock.lastBuyValue = entry.orderValue;
            stock.hasBuyForMax = true;
            if (entry.orderValue > stock.maxBuyValue) {
                stock.maxBuyValue = entry.orderValue;
            }
        }
        
        if ((i + 1) % freq == 0 || i == orderBook.size() - 1) {
            std::vector<StockInfo> stocks;
            for (auto& pair : stockMap) {
                stocks.push_back(pair.second);
            }

            std::sort(stocks.begin(), stocks.end(), [](const StockInfo& a, const StockInfo& b) {
                int spreadA = a.getSpread();
                int spreadB = b.getSpread();
                if (spreadA != spreadB) {
                    return spreadA > spreadB;
                }
                return a.stockID > b.stockID;
            });
            
            std::string filename = "snap_" + std::to_string(snapCount) + ".txt";
            std::ofstream outFile(filename);
            
            for (const auto& stock : stocks) {
                int spread = stock.getSpread();
                outFile << stock.stockID << " " 
                        << (int)stock.lastSellValue << " " 
                        << (int)stock.lastBuyValue << " " 
                        << spread << "\n";
            }
            
            outFile.close();
            snapCount++;

            if((orderBook.size()) % freq == 0 && (i + 1) == orderBook.size()) {
                //make a duplicate file
                std::string dupFilename = "snap_" + std::to_string(snapCount) + ".txt";
                std::ofstream dupOutFile(dupFilename);
                for (const auto& stock : stocks) {
                    int spread = stock.getSpread();
                    dupOutFile << stock.stockID << " " 
                            << (int)stock.lastSellValue << " " 
                            << (int)stock.lastBuyValue << " " 
                            << spread << "\n";
                }
                dupOutFile.close();
            }
        }
    }
}

void printOrderStatsC(const std::vector<uint64_t> &orderBook) {
    std::map<uint32_t, StockInfo> stockMap;
    
    for (uint64_t packet : orderBook) {
        uint64_t unstuffed = unstuffBits(packet);
        OrderBookEntry entry = decodePacket(unstuffed);

        StockInfo& stock = stockMap[entry.stockID];
        stock.stockID = entry.stockID;
        
        if (entry.orderType) {
            stock.hasSellForMin = true;
            if (entry.orderValue < stock.minSellValue) {
                stock.minSellValue = entry.orderValue;
            }
        } else {
            stock.hasBuyForMax = true;
            if (entry.orderValue > stock.maxBuyValue) {
                stock.maxBuyValue = entry.orderValue;
            }
        }
        
        stock.sumOrderValue += (int64_t)entry.orderValue;
        stock.orderCount++;
    }

    std::ofstream outFile("stats.txt");
    outFile << std::fixed << std::setprecision(4);
    
    for (auto& pair : stockMap) {
        const StockInfo& stock = pair.second;
            
        double avgOrderValue = 0.0;
        if (stock.orderCount > 0) {
            avgOrderValue = static_cast<double>(stock.sumOrderValue) /  static_cast<double>(stock.orderCount);
        }
            
        uint8_t minSell = stock.hasSellForMin ? stock.minSellValue : 0;
        uint8_t maxBuy  = stock.hasBuyForMax  ? stock.maxBuyValue  : 0;
    
        outFile << stock.stockID << " "
                << static_cast<int>(minSell) << " "
                << static_cast<int>(maxBuy) << " "
                << std::fixed << std::setprecision(3)
                << avgOrderValue << "\n";
    }
    
    outFile.close();
}


//parallel functions

// --- Update Display  ---

void updateDisplay(const std::vector<uint64_t> &orderBook, int32_t freq) {
    size_t n = orderBook.size();
    if (n == 0) return;
    if(orderBook.size() <= 10000) {
        updateDisplayC(orderBook, freq);
        return;
    }

    // Phase 1: Parallel Pre-decoding with Guided Scheduling and Safe Unrolling
    std::vector<OrderBookEntry> decodedEntries(n);

    #pragma omp parallel for schedule(guided)
    for (size_t i = 0; i < (n / 8) * 8; i += 8) {
        decodedEntries[i]     = decodePacket(unstuffBits(orderBook[i]));
        decodedEntries[i + 1] = decodePacket(unstuffBits(orderBook[i + 1]));
        decodedEntries[i + 2] = decodePacket(unstuffBits(orderBook[i + 2]));
        decodedEntries[i + 3] = decodePacket(unstuffBits(orderBook[i + 3]));
        decodedEntries[i + 4] = decodePacket(unstuffBits(orderBook[i + 4]));
        decodedEntries[i + 5] = decodePacket(unstuffBits(orderBook[i + 5]));
        decodedEntries[i + 6] = decodePacket(unstuffBits(orderBook[i + 6]));
        decodedEntries[i + 7] = decodePacket(unstuffBits(orderBook[i + 7]));
    }

    for (size_t i = (n / 8) * 8; i < n; ++i) {
        decodedEntries[i] = decodePacket(unstuffBits(orderBook[i]));
    }

    // Phase 2: Serial Map Updates & Task-based Snapshots
    #pragma omp parallel
    {
        #pragma omp single
        {
            std::unordered_map<uint32_t, StockInfo> stockMap;
            int snapCount = 0;

            for (size_t i = 0; i < n; i++) {
                const auto& entry = decodedEntries[i];
                
                StockInfo& stock = stockMap[entry.stockID];
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
                
                if ((i + 1) % freq == 0 || i == n - 1) {
                    auto snap = std::make_shared<std::vector<StockInfo>>();
                    snap->reserve(stockMap.size());
                    for (auto const& [id, info] : stockMap) snap->push_back(info);

                    bool needsDuplicate = (n % freq == 0 && i == n - 1);

                    #pragma omp task firstprivate(snap, snapCount, needsDuplicate)
                    {
                        auto writeTask = [&](int count) {
                            std::sort(snap->begin(), snap->end(), [](const StockInfo& a, const StockInfo& b) {
                                int spreadA = std::abs((int)a.lastSellValue - (int)a.lastBuyValue);
                                int spreadB = std::abs((int)b.lastSellValue - (int)b.lastBuyValue);
                                if (spreadA != spreadB) return spreadA > spreadB;
                                return a.stockID > b.stockID;
                            });
                            
                            std::string filename = "snap_" + std::to_string(count) + ".txt";
                            std::ofstream outFile(filename);
                            for (const auto& s : *snap) {
                                outFile << s.stockID << " " << (int)s.lastSellValue << " " << (int)s.lastBuyValue << " " << std::abs((int)s.lastSellValue - (int)s.lastBuyValue) << "\n";
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

// --- Total Reduction ---

int64_t totalAmountTraded(const std::vector<uint64_t> &orderBook) {
    int64_t total = 0;
    size_t n = orderBook.size();
    if(orderBook.size() <= 5000) {
        return totalAmountTradedC(orderBook);
    }


     // #pragma omp parallel
    // {
    //     // Using two local accumulators can help the CPU avoid dependency stalls
    //     int64_t acc1 = 0;
    //     int64_t acc2 = 0 , acc3=0, acc4=0;


    //     // Unrolling by 4 for better ILP
    //     #pragma omp for schedule(guided) nowait
    //     for (size_t i = 0; i < (n / 4) * 4; i += 4) {
    //         // Block 1 & 2
    //         // uint64_t u0 = ;
    //         OrderBookEntry_Par e0 = decodePacket_Par(unstuffBits_Par(orderBook[i]));
    //         acc1 += (int64_t)e0.orderQty * e0.orderValue; 

    //         // uint64_t u1 = unstuffBits_Par(orderBook[i+1]);
    //         OrderBookEntry_Par e1 = decodePacket_Par(unstuffBits_Par(orderBook[i+1]));
    //         acc2 += (int64_t)e1.orderQty * e1.orderValue; 

    //         // Block 3 & 4
    //         // uint64_t u2 = unstuffBits_Par(orderBook[i+2]);
    //         OrderBookEntry_Par e2 = decodePacket_Par(unstuffBits_Par(orderBook[i+2]));
    //         acc3 += (int64_t)e2.orderQty * e2.orderValue; 

    //         // uint64_t u3 = unstuffBits_Par(orderBook[i+3]);
    //         OrderBookEntry_Par e3 = decodePacket_Par(unstuffBits_Par(orderBook[i+3]));
    //         acc4 += (int64_t)e3.orderQty * e3.orderValue; 
    //     }
        
    //     #pragma omp atomic
    //     total += (acc1 + acc2 + acc3 + acc4);
    // }
    // #pragma omp parallel
    // {
    //     // Using two local accumulators can help the CPU avoid dependency stalls
    //     int64_t acc1 = 0;
    //     int64_t acc2 = 0 , acc3=0, acc4=0 , acc5=0, acc6=0, acc7=0, acc8=0;


    //     // Unrolling by 4 for better ILP
    //     #pragma omp for schedule(guided) nowait
    //     for (size_t i = 0; i < (n / 8) * 8; i += 8) {
    //         // Block 1 & 2
            
    //         OrderBookEntry e0 = decodePacket(unstuffBits(orderBook[i]));
    //         acc1 += (int64_t)e0.orderQty * e0.orderValue; 

    //         OrderBookEntry e1 = decodePacket(unstuffBits(orderBook[i+1]));
    //         acc2 += (int64_t)e1.orderQty * e1.orderValue; 

    //         // Block 3 & 4
    //         OrderBookEntry e2 = decodePacket(unstuffBits(orderBook[i+2]));
    //         acc3 += (int64_t)e2.orderQty * e2.orderValue; 

    //         OrderBookEntry e3 = decodePacket(unstuffBits(orderBook[i+3]));
    //         acc4 += (int64_t)e3.orderQty * e3.orderValue; 

    //         // Block 5 & 6
    //         OrderBookEntry e4 = decodePacket(unstuffBits(orderBook[i+4]));
    //         acc5 += (int64_t)e4.orderQty * e4.orderValue; 

    //         OrderBookEntry e5 = decodePacket(unstuffBits(orderBook[i+5]));
    //         acc6 += (int64_t)e5.orderQty * e5.orderValue;  

    //         // Block 7 & 8
    //         OrderBookEntry e6 = decodePacket(unstuffBits(orderBook[i+6]));
    //         acc7 += (int64_t)e6.orderQty * e6.orderValue; 

    //         OrderBookEntry e7 = decodePacket(unstuffBits(orderBook[i+7]));
    //         acc8 += (int64_t)e7.orderQty * e7.orderValue;   
    //     }
        
    //     #pragma omp atomic
    //     total += (acc1 + acc2 + acc3 + acc4 + acc5 + acc6 + acc7 + acc8);
    // }

    //  // Cleanup loop for remaining elements (n % 4)
    // for (size_t i = (n / 8) * 8; i < n; ++i) {
    //     OrderBookEntry e = decodePacket(unstuffBits(orderBook[i]));
    //     total += (int64_t)e.orderQty * e.orderValue; 
    // }

    #pragma omp parallel
    {
        #pragma omp for schedule(guided) reduction(+:total)
        for (size_t i = 0; i < n; i++) {
            // uint64_t u = unstuffBits(orderBook[i]);
            OrderBookEntry e = decodePacket(unstuffBits(orderBook[i]));
            total += (int64_t)e.orderQty * e.orderValue;
        }
    }

    return total;
}

// --- Order Stats  ---

void printOrderStats(const std::vector<uint64_t> &orderBook) {
    if(orderBook.size() <= 10000) {
        printOrderStatsC(orderBook);
        return;
    }


    int max_threads = omp_get_max_threads();
    std::vector<std::unordered_map<uint32_t, StockInfo>> thread_maps(max_threads);
    
    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        auto& local_map = thread_maps[tid];
        
        #pragma omp for schedule(static)
        for (size_t i = 0; i < orderBook.size(); i++) {
            // uint64_t unstuffed = unstuffBits(orderBook[i]);
            OrderBookEntry entry = decodePacket(unstuffBits(orderBook[i]));
            
            StockInfo& s = local_map[entry.stockID];
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
    
    std::unordered_map<uint32_t, bool> global_keys_map;
    for (const auto& lm : thread_maps) {
        for (auto const& [id, val] : lm) global_keys_map[id] = true;
    }
    
    std::vector<uint32_t> unique_ids;
    for (auto const& [id, _] : global_keys_map) unique_ids.push_back(id);

    std::vector<StockInfo> final_stats(unique_ids.size());
    #pragma omp parallel for schedule(guided)
    for (size_t i = 0; i < unique_ids.size(); i++) {
        uint32_t id = unique_ids[i];
        StockInfo merged;
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

    std::sort(final_stats.begin(), final_stats.end(), [](const StockInfo& a, const StockInfo& b) {
        return a.stockID < b.stockID;
    });
    
    std::ofstream outFile("stats.txt");
    outFile << std::fixed << std::setprecision(3);
    for (const auto& s : final_stats) {
        double avg = (s.orderCount > 0) ? (double)s.sumOrderValue / s.orderCount : 0.0;
        outFile << s.stockID << " " << (int)(s.hasSellForMin ? s.minSellValue : 0) << " " << (int)(s.hasBuyForMax ? s.maxBuyValue : 0) << " " << avg << "\n";
    }
}