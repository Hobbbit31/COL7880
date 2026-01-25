# Order Book Processing - Test Guide

## Compilation

### macOS (with Homebrew libomp)
```bash
clang++ -std=c++17 -Xpreprocessor -fopenmp -I/opt/homebrew/opt/libomp/include \
  -L/opt/homebrew/opt/libomp/lib -lomp -O2 test.cpp functions.cpp -o test_assignment
```

### Linux / HPC Systems
```bash
g++ -std=c++17 -fopenmp -O2 test.cpp functions.cpp -o test_assignment
```

---

## Quick Test Commands

| Command | Description |
|---------|-------------|
| `./test_assignment debug` | Test encoding/decoding edge cases |
| `./test_assignment correctness -n 1000000` | Large-scale correctness (1M orders) |
| `./test_assignment performance -n 5000000` | Performance benchmark (5M orders) |
| `./test_assignment scalability -n 1000000` | Test speedup with 1→N threads |
| `./test_assignment all -n 1000000` | Run all tests |

### Command Options
```
-n <num>    Number of orders (default: 10000000)
-s <num>    Number of stocks (default: 10000)
-f <num>    Snapshot frequency (default: 100000)
-h          Show help
```

### Thread Control
```bash
# Via environment variable
export OMP_NUM_THREADS=4
./test_assignment performance -n 5000000

# Or inline
OMP_NUM_THREADS=4 ./test_assignment performance -n 5000000
```

---

## Manual Verification

### 1. totalAmountTraded()

This function calculates: `Σ (orderQty × orderValue)` for all orders.

**Manual Test:**
```bash
# Run the debug test and check output
./test_assignment debug
```

**Expected Output:**
```
StockID=1, Type=sell, Qty=10,  Value=100 → 10×100  = 1000   ✓
StockID=1, Type=buy,  Qty=5,   Value=60  → 5×60   = 300    ✓
StockID=1, Type=sell, Qty=255, Value=255 → 255×255 = 65025  ✓
```

**Verify with Calculator:**
- Simple case: 10 × 100 = 1000
- Edge case: 255 × 255 = 65025

---

### 2. printOrderStats()

This function creates `stats.txt` with format:
```
<stockID> <minSellValue> <maxBuyValue> <avgOrderValue>
```
Sorted by stockID in **increasing** order.

**Manual Test:**
```bash
# Create a simple test
./test_assignment correctness -n 100 -s 5 -f 50

# View the output
cat test/stats.txt
```

**Verify:**
1. Stock IDs should be in ascending order (1, 2, 3, ...)
2. minSellValue = minimum value among all SELL orders for that stock
3. maxBuyValue = maximum value among all BUY orders for that stock
4. avgOrderValue = average of ALL order values (both buy and sell)

**Example stats.txt:**
```
1 45 230 127.5000
2 12 255 133.2500
3 78 198 142.0000
```

**Check:**
- Line 1: Stock 1, minSell=45, maxBuy=230, avg=127.5
- Verify avgOrderValue has 4 decimal places (or 3 as per your implementation)

---

### 3. updateDisplay()

This function creates snapshot files `snap_0.txt`, `snap_1.txt`, etc.

**Snapshot Timing:**
- Snapshot taken after every `freq` orders
- Also after the last order
- Number of snapshots = `freq` divides `n` evenly ? `n/freq` : `n/freq + 1`

**Format per snapshot:**
```
<stockID> <lastSellValue> <lastBuyValue> <spread>
```
Sorted by **spread descending**, ties broken by **stockID descending**.

**Manual Test:**
```bash
# Small test: 100 orders, snapshot every 25
./test_assignment correctness -n 100 -s 10 -f 25

# Check number of snapshots
ls test/snap_*.txt

# View a snapshot
cat test/snap_0.txt
```

**Verify:**
1. **Spread Calculation:** `spread = |lastSellValue - lastBuyValue|`
2. **Ordering:** Higher spread first; if equal spread, higher stockID first
3. **Snapshot Count:** For n=100, freq=25 → snapshots at indices 24, 49, 74, 99 → 4 files

**Example snap_1.txt:**
```
7 180 50 130
3 200 80 120
9 150 40 110
5 100 100 0
```

**Check:**
- Spreads: 130 > 120 > 110 > 0 ✓
- Stock 7 has spread 130 (|180-50|=130) ✓

---

## Scalability Testing

After parallelizing, test speedup by controlling threads:

### Method 1: Environment Variable
```bash
# Test with 1 thread (baseline)
OMP_NUM_THREADS=1 ./test_assignment scalability -n 2000000 -s 5000

# Test with 4 threads
OMP_NUM_THREADS=4 ./test_assignment scalability -n 2000000 -s 5000

# Test with 8 threads
OMP_NUM_THREADS=8 ./test_assignment scalability -n 2000000 -s 5000
```

### Method 2: Set in functions.cpp
Add `omp_set_num_threads(N)` at the start of your parallel functions.

**Expected Output (before parallelization):**
```
============================================
         AVERAGE TIMES (5 runs)
============================================
totalAmountTraded: 200.00 ms   (same with any thread count)
printOrderStats:   300.00 ms
updateDisplay:     250.00 ms
============================================
```

**Expected Output (after parallelization with 4 threads):**
```
============================================
         AVERAGE TIMES (5 runs)
============================================
totalAmountTraded: 55.00 ms    (~3.6x speedup)
printOrderStats:   85.00 ms    (~3.5x speedup)
updateDisplay:     70.00 ms    (~3.5x speedup)
============================================
```

---

## Manual Small Test (6 orders)

Create exactly the test case from the assignment:

```bash
# This is what the original small test does internally:
# Stock 1: sell qty=10 value=100, buy qty=5 value=60   → spread=40
# Stock 2: sell qty=8  value=80,  buy qty=12 value=70  → spread=10
# Stock 3: sell qty=6  value=50,  buy qty=4 value=20   → spread=30
```

**Expected totalAmountTraded:**
```
10×100 + 5×60 + 8×80 + 12×70 + 6×50 + 4×20
= 1000 + 300 + 640 + 840 + 300 + 80
= 3160
```

**Expected stats.txt (sorted by stockID ascending):**
```
1 100 60 80.0000
2 80 70 75.0000
3 50 20 35.0000
```

**Expected snap_1.txt (final snapshot, sorted by spread desc):**
```
1 100 60 40
3 50 20 30
2 80 70 10
```

---

## Checking for Race Conditions

Run the same test multiple times and compare outputs:

```bash
for i in {1..5}; do
  ./test_assignment correctness -n 500000 -s 1000 -f 50000
  cp test/stats.txt test/stats_run$i.txt
done

# Compare all runs - should be identical
md5sum test/stats_run*.txt
```

If outputs differ between runs, you have a race condition.

---

## File Outputs Summary

All output files are created in the `test/` folder:

| File | Created By | Contents |
|------|------------|----------|
| `test/stats.txt` | `printOrderStats()` | Stock statistics (minSell, maxBuy, avg) |
| `test/snap_0.txt` | `updateDisplay()` | First snapshot |
| `test/snap_1.txt` | `updateDisplay()` | Second snapshot |
| `test/snap_N.txt` | `updateDisplay()` | N-th snapshot |

```bash
# View output files
ls test/
cat test/stats.txt
cat test/snap_0.txt
```

---

## Troubleshooting

### "omp.h not found"
Install OpenMP:
```bash
# macOS
brew install libomp

# Ubuntu/Debian
sudo apt install libomp-dev
```

### Tests fail with encoding/decoding errors
Check that `unstuffBits()` in `functions.cpp` does NOT have a `continue` statement after handling 5 consecutive 1s.

### Wrong number of snapshots
Formula: `n % freq == 0 ? n/freq : n/freq + 1`

### Speedup is ~1.0x with multiple threads
Your code is still sequential. Add OpenMP pragmas in `functions.cpp`:
```cpp
#pragma omp parallel for reduction(+:total)
for (size_t i = 0; i < orderBook.size(); i++) {
    // ...
}
```

Test with different thread counts:
```bash
OMP_NUM_THREADS=1 ./test_assignment scalability -n 1000000
OMP_NUM_THREADS=4 ./test_assignment scalability -n 1000000
```
Compare average times - should see ~4x speedup with 4 threads for well-parallelized code.
