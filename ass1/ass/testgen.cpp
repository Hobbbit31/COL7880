#include <bits/stdc++.h>
using namespace std;

static uint64_t stuffBits(uint64_t packet49) {
    uint64_t out = 0;
    int outPos = 0;
    int ones = 0;

    for (int i = 0; i < 49; i++) {
        uint64_t bit = (packet49 >> i) & 1ULL;
        out |= (bit << outPos);
        outPos++;

        if (bit) ones++;
        else ones = 0;

        if (ones == 5) {
            out |= (0ULL << outPos);
            outPos++;
            ones = 0;
        }

        if (outPos >= 64) break;
    }

    return out;
}

static uint64_t makePacket49(uint32_t stockID, bool orderType, uint8_t qty, uint8_t value) {
    uint64_t p = 0;
    p |= (uint64_t)stockID;
    p |= ((uint64_t)(orderType ? 1 : 0) << 32);
    p |= ((uint64_t)qty << 33);
    p |= ((uint64_t)value << 41);
    return p;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        cerr << "Usage: ./generator <numberoforders> <outputfile.txt>\n";
        return 1;
    }

    long long n = stoll(argv[1]);
    if (n <= 0) {
        cerr << "numberoforders must be positive.\n";
        return 1;
    }

    ofstream out(argv[2]);
    if (!out) {
        cerr << "Failed to open output file.\n";
        return 1;
    }

    const int MAX_UNIQUE = 100000;
    int uniqueCount = (int)min<long long>(n, MAX_UNIQUE);

    vector<uint32_t> stockIDs(uniqueCount);
    for (int i = 0; i < uniqueCount; i++) stockIDs[i] = (uint32_t)i;

    random_device rd;
    mt19937_64 rng(rd());

    uniform_int_distribution<int> typeDist(0, 1);
    uniform_int_distribution<int> qtyDist(1, 255);
    uniform_int_distribution<int> valDist(1, 255);
    uniform_int_distribution<int> idDist(0, uniqueCount - 1);

    for (long long i = 0; i < n; i++) {
        uint32_t stockID = stockIDs[idDist(rng)];
        bool orderType = typeDist(rng);
        uint8_t qty = (uint8_t)qtyDist(rng);
        uint8_t value = (uint8_t)valDist(rng);

        uint64_t raw49 = makePacket49(stockID, orderType, qty, value);
        uint64_t stuffed64 = stuffBits(raw49);

        out << uppercase << hex << setw(16) << setfill('0') << stuffed64 << "\n";
    }

    return 0;
}