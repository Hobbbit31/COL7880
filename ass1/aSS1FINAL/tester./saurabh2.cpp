#include "functions.h"
#include <omp.h>
#include <cmath>
#include <algorithm>
#include<unordered_map>

struct OrderBookEntry {
    uint32_t stockID;
    bool orderType;
    uint8_t orderQty;
    uint8_t orderValue;
};

OrderBookEntry decodePacket(uint64_t packet) {
    OrderBookEntry entry;
    entry.stockID = (packet >> 0) & 0xFFFFFFFF;
    entry.orderType = ((packet >> 32) & 0x1) == 1;
    entry.orderQty = (packet >> 33) & 0xFF;
    entry.orderValue = (packet >> 41) & 0xFF;
    return entry;
}

uint64_t unstuffBits(uint64_t packet) {
    uint64_t result = 0;
    int outputBitPos = 0;
    int consecutiveOnes = 0;
    const int TARGET_BITS = 49;
    
    int i = 0;
    while (i < 64 && outputBitPos < TARGET_BITS) {
        bool bit = (packet >> i) & 0x1;
        
        if (bit) {
            consecutiveOnes++;
            result |= (1ULL << outputBitPos);
            outputBitPos++;
            
            if (consecutiveOnes == 5) {
                consecutiveOnes = 0;
                i += 2; 
                continue;
            }
        } else {
            consecutiveOnes = 0;
            outputBitPos++;
        }
        i++;
    }
    return result;
}

int64_t totalAmountTraded(const std::vector<uint64_t> &orderBook) {
    int64_t total = 0;


    #pragma omp parallel for reduction(+:total)
    for (size_t i = 0; i < orderBook.size(); i++) {
        //unstuff
        uint64_t unstuffed = unstuffBits(orderBook[i]);
        //decode
        OrderBookEntry entry = decodePacket(unstuffed);
        //accumulate
        total += (int64_t)entry.orderQty * (int64_t)entry.orderValue;
    }

    return total;
}

struct StockInfo {
    uint32_t stockID;
    uint8_t lastSellValue;
    uint8_t lastBuyValue;
    uint8_t minSellValue;
    uint8_t maxBuyValue;
    int64_t sumOrderValue;  
    int orderCount;         
    bool hasSellForMin;     
    bool hasBuyForMax;      
    
    StockInfo() : lastSellValue(0), lastBuyValue(0), 
                  minSellValue(255), maxBuyValue(0),
                  sumOrderValue(0), orderCount(0),
                  hasSellForMin(false), hasBuyForMax(false) {}
    
    int getSpread() const {
        return abs((int)lastSellValue - (int)lastBuyValue);
    }
};

struct StockDisplay {
    uint32_t stockID;
    uint8_t lastSell;
    uint8_t lastBuy;
    
    int getSpread() const {
        return std::abs((int)lastSell - (int)lastBuy);
    }
};


void updateDisplay(const std::vector<uint64_t> &orderBook, int32_t freq) {
    //Parallel Decode - pre-decoded entries in vector for faster commits
    std::vector<OrderBookEntry> decodedBook(orderBook.size());

    #pragma omp parallel for schedule(static)
    for (size_t i = 0; i < orderBook.size(); i++) {
        decodedBook[i] = decodePacket(unstuffBits(orderBook[i]));
    }

    //SEQUENTIAL COMMIT 
    //task based snapshot creation
    #pragma omp parallel
    {
        #pragma omp single
        {
            std::unordered_map<uint32_t, StockDisplay> marketState;
            marketState.reserve(200000); // Reserve decent size to avoid rehashes
            
            int snapCount = 0;

            for (size_t i = 0; i < decodedBook.size(); i++) {
                const OrderBookEntry& entry = decodedBook[i];
                
                StockDisplay& stock = marketState[entry.stockID];
                stock.stockID = entry.stockID;
                if (entry.orderType) stock.lastSell = entry.orderValue;
                else                 stock.lastBuy  = entry.orderValue;

                //if trigger conditions met ( flags )
                bool isFreqTrigger = ((i + 1) % freq == 0);
                bool isLastTrigger = (i == decodedBook.size() - 1);

                if (isFreqTrigger || isLastTrigger) {
                    // Deep copy the state for the background task
                    std::vector<StockDisplay> snapshotData;
                    snapshotData.reserve(marketState.size());
                    for (const auto& pair : marketState) {
                        snapshotData.push_back(pair.second);
                    }

                    // Task 1: Primary Snapshot
                    // 'firstprivate' ensures the task gets its own safe copy of snapshotData and the current snapCount
                    #pragma omp task firstprivate(snapshotData, snapCount)
                    {
                        // 1. Sort (Expensive)
                        std::sort(snapshotData.begin(), snapshotData.end(), 
                            [](const StockDisplay& a, const StockDisplay& b) {
                                int sA = a.getSpread();
                                int sB = b.getSpread();
                                if (sA != sB) return sA > sB;
                                return a.stockID > b.stockID;
                        });

                        // 2. Write (Expensive I/O)
                        std::string fname = "test/snap_" + std::to_string(snapCount) + ".txt";
                        std::ofstream outFile(fname);
                        for (const auto& s : snapshotData) {
                            outFile << s.stockID << " " 
                                    << (int)s.lastSell << " " 
                                    << (int)s.lastBuy << " " 
                                    << s.getSpread() << "\n";
                        }
                    }
                    
                    snapCount++; // Increment for the next file

                    // Task 2: Clone Snapshot (The Edge Case)
                    // If we are at the very end AND it's a frequency multiple, we need a second file.
                    if (isFreqTrigger && isLastTrigger) {
                        #pragma omp task firstprivate(snapshotData, snapCount)
                        {
                            // We re-sort because 'snapshotData' was moved/copied unique to this task
                            std::sort(snapshotData.begin(), snapshotData.end(), 
                                [](const StockDisplay& a, const StockDisplay& b) {
                                    int sA = a.getSpread();
                                    int sB = b.getSpread();
                                    if (sA != sB) return sA > sB;
                                    return a.stockID > b.stockID;
                            });

                            std::string fname = "test/snap_" + std::to_string(snapCount) + ".txt";
                            std::ofstream outFile(fname);
                            for (const auto& s : snapshotData) {
                                outFile << s.stockID << " " 
                                        << (int)s.lastSell << " " 
                                        << (int)s.lastBuy << " " 
                                        << s.getSpread() << "\n";
                            }
                        }
                        snapCount++;
                    }
                }
            } 
        } // End of 'single'. Implicit Barrier here ensures all tasks finish before function returns.
    } 
}


// --- Helper Struct for Statistics ---
struct StockStats {
    uint32_t stockID;
    uint8_t minSell;
    uint8_t maxBuy;
    int64_t sumOrderValue;
    int32_t count;
    bool hasSell; // To track if we have seen at least one sell
    bool hasBuy;  // To track if we have seen at least one buy

    StockStats() : stockID(0), minSell(255), maxBuy(0), sumOrderValue(0), count(0), hasSell(false), hasBuy(false) {}

    // Helper to merge another thread's stats into this one
    void merge(const StockStats& other) {
        if (other.hasSell) {
            if (!hasSell || other.minSell < minSell) {
                minSell = other.minSell;
                hasSell = true;
            }
        }
        if (other.hasBuy) {
            if (!hasBuy || other.maxBuy > maxBuy) {
                maxBuy = other.maxBuy;
                hasBuy = true;
            }
        }
        sumOrderValue += other.sumOrderValue;
        count += other.count;
    }
};

// --- Part 2.2: Order Book Statistics ---

void printOrderStats(const std::vector<uint64_t> &orderBook) {
    // FIX 1: Use std::map (Ordered) for the global result to ensure sorted output
    std::map<uint32_t, StockStats> globalStats;

    #pragma omp parallel
    {
        // OPTIMIZATION: Use unordered_map for thread-local storage.
        // We don't care about order during the calculation phase, and it's faster.
        std::unordered_map<uint32_t, StockStats> localStats;

        #pragma omp for nowait
        for (size_t i = 0; i < orderBook.size(); i++) {
            uint64_t unstuffed = unstuffBits(orderBook[i]);
            OrderBookEntry entry = decodePacket(unstuffed);

            StockStats& stats = localStats[entry.stockID];
            stats.stockID = entry.stockID;

            if (entry.orderType) { // Sell
                if (!stats.hasSell || entry.orderValue < stats.minSell) {
                    stats.minSell = entry.orderValue;
                    stats.hasSell = true;
                }
            } else { // Buy
                if (!stats.hasBuy || entry.orderValue > stats.maxBuy) {
                    stats.maxBuy = entry.orderValue;
                    stats.hasBuy = true;
                }
            }
            stats.sumOrderValue += entry.orderValue;
            stats.count++;
        }

        // Critical Merge
        #pragma omp critical
        {
            for (const auto& pair : localStats) {
                // std::map::operator[] automatically handles insertion if key doesn't exist
                // But we need custom merge logic, so find() is correct.
                auto it = globalStats.find(pair.first);
                if (it == globalStats.end()) {
                    globalStats[pair.first] = pair.second;
                } else {
                    it->second.merge(pair.second);
                }
            }
        }
    } 

    // File Output
    std::ofstream outFile("test/stats.txt");
    outFile << std::fixed << std::setprecision(4);

    // Because globalStats is now std::map, this loop iterates in sorted order of stockID
    for (const auto& pair : globalStats) {
        const StockStats& s = pair.second;
        
        double avg = (s.count > 0) ? (double)s.sumOrderValue / s.count : 0.0;
        int finalMinSell = s.hasSell ? s.minSell : 0; 
        int finalMaxBuy = s.hasBuy ? s.maxBuy : 0;

        outFile << s.stockID << " " 
                << finalMinSell << " " 
                << finalMaxBuy << " " 
                << avg << "\n";
    }
    outFile.close();
}