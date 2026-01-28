hobbbit31@Smaug:~/Desktop/tester.$ ./compare.sh
==============================================
  TEST CASES BENCHMARK
==============================================
Orders:         10000
Stocks:         100
Snapshot Freq:  100
==============================================
Compiling...Done (with -O3 optimization).
----------------------------------------------
Running SEQUENTIAL version...

SEQUENTIAL BASELINE (1.0x)
Function             | Time (ms)      
-------------------- | ---------------
totalAmountTraded    | 0.8181         
printOrderStats      | 1.4475         
updateDisplay        | 5.4470         
-------------------- | ---------------
TOTAL                | 7.7126         

PARALLEL PERFORMANCE
Threads    | TotalAmount               | OrderStats                | UpdateDisp                | Status    
---------- | ------------------------- | ------------------------- | ------------------------- | ----------
4T         | 1.68x (0.4858ms)          | 1.21x (1.1957ms)          | 1.08x (5.0097ms)          | PASS
8T         | 1.85x (0.4418ms)          | 1.00x (1.4402ms)          | .71x (7.6510ms)           | PASS
10T        | 1.31x (0.6202ms)          | 1.01x (1.4236ms)          | .65x (8.3316ms)           | PASS
12T        | 1.68x (0.4863ms)          | 1.00x (1.4335ms)          | .70x (7.7168ms)           | PASS
16T        | .09x (8.7279ms)           | 1.01x (1.4241ms)          | .68x (7.9596ms)           | PASS

Full detailed report saved to: results/comparison_20260128_135436.txt





hobbbit31@Smaug:~/Desktop/tester.$ ./compare.sh
==============================================
  TEST CASES BENCHMARK
==============================================
Orders:         10000
Stocks:         10
Snapshot Freq:  10
==============================================
Compiling...Done (with -O3 optimization).
----------------------------------------------
Running SEQUENTIAL version...

SEQUENTIAL BASELINE (1.0x)
Function             | Time (ms)      
-------------------- | ---------------
totalAmountTraded    | 1.4557         
printOrderStats      | 1.0305         
updateDisplay        | 33.1417        
-------------------- | ---------------
TOTAL                | 35.6279        

PARALLEL PERFORMANCE
Threads    | TotalAmount               | OrderStats                | UpdateDisp                | Status    
---------- | ------------------------- | ------------------------- | ------------------------- | ----------
4T         | 2.42x (0.6012ms)          | 1.08x (0.9527ms)          | .81x (40.5137ms)          | PASS
8T         | 3.82x (0.3810ms)          | .87x (1.1836ms)           | .69x (47.8232ms)          | PASS
10T        | .16x (8.7919ms)           | .79x (1.2882ms)           | .62x (52.9674ms)          | PASS
12T        | 3.36x (0.4330ms)          | .82x (1.2463ms)           | .70x (47.0790ms)          | PASS
16T        | 2.38x (0.6103ms)          | .82x (1.2537ms)           | .69x (47.9020ms)          | PASS

Full detailed report saved to: results/comparison_20260128_135656.txt




hobbbit31@Smaug:~/Desktop/tester.$ ./compare.sh
==============================================
  TEST CASES BENCHMARK
==============================================
Orders:         1000000
Stocks:         1000
Snapshot Freq:  1000
==============================================
Compiling...Done (with -O3 optimization).
----------------------------------------------
Running SEQUENTIAL version...

SEQUENTIAL BASELINE (1.0x)
Function             | Time (ms)      
-------------------- | ---------------
totalAmountTraded    | 92.2743        
printOrderStats      | 134.4289       
updateDisplay        | 361.1970       
-------------------- | ---------------
TOTAL                | 587.9002       

PARALLEL PERFORMANCE
Threads    | TotalAmount               | OrderStats                | UpdateDisp                | Status    
---------- | ------------------------- | ------------------------- | ------------------------- | ----------
4T         | 4.01x (22.9799ms)         | 4.51x (29.7920ms)         | 3.91x (92.3372ms)         | PASS
8T         | 7.82x (11.7981ms)         | 10.15x (13.2358ms)        | 6.62x (54.4916ms)         | PASS
10T        | 8.24x (11.1869ms)         | 8.99x (14.9427ms)         | 6.52x (55.3575ms)         | PASS
12T        | 8.96x (10.2967ms)         | 10.89x (12.3360ms)        | 7.42x (48.6509ms)         | PASS
16T        | 11.11x (8.2985ms)         | 13.18x (10.1964ms)        | 7.43x (48.5790ms)         | PASS

Full detailed report saved to: results/comparison_20260128_135752.txt





hobbbit31@Smaug:~/Desktop/tester.$ ./compare.sh
==============================================
  TEST CASES BENCHMARK
==============================================
Orders:         10000000
Stocks:         10000
Snapshot Freq:  10000
==============================================
Compiling...Done (with -O3 optimization).
----------------------------------------------
Running SEQUENTIAL version...

SEQUENTIAL BASELINE (1.0x)
Function             | Time (ms)      
-------------------- | ---------------
totalAmountTraded    | 941.7985       
printOrderStats      | 1775.8575      
updateDisplay        | 4239.8253      
-------------------- | ---------------
TOTAL                | 6957.4813      

PARALLEL PERFORMANCE
Threads    | TotalAmount               | OrderStats                | UpdateDisp                | Status    
---------- | ------------------------- | ------------------------- | ------------------------- | ----------
4T         | 3.99x (235.5831ms)        | 6.62x (268.2477ms)        | 4.88x (868.2903ms)        | PASS
8T         | 7.76x (121.2211ms)        | 11.94x (148.6871ms)       | 7.70x (550.4582ms)        | PASS
10T        | 7.74x (121.5720ms)        | 11.70x (151.6577ms)       | 8.57x (494.7174ms)        | PASS
12T        | 9.05x (103.9693ms)        | 13.09x (135.6164ms)       | 8.83x (479.7835ms)        | PASS
16T        | 11.57x (81.3470ms)        | 15.47x (114.7528ms)       | 9.95x (425.7202ms)        | PASS

Full detailed report saved to: results/comparison_20260128_135907.txt
hobbbit31@Smaug:~/Desktop/tester.$ 




hobbbit31@Smaug:~/Desktop/tester.$ ./compare.sh
==============================================
  TEST CASES BENCHMARK
==============================================
Orders:         200000000
Stocks:         100000
Snapshot Freq:  1000000
==============================================
Compiling...Done (with -O3 optimization).
----------------------------------------------
Running SEQUENTIAL version...

SEQUENTIAL BASELINE (1.0x)
Function             | Time (ms)      
-------------------- | ---------------
totalAmountTraded    | 20462.3233     
printOrderStats      | 49738.7948     
updateDisplay        | 60523.1351     
-------------------- | ---------------
TOTAL                | 130724.2532    

PARALLEL PERFORMANCE
Threads    | TotalAmount               | OrderStats                | UpdateDisp                | Status    
---------- | ------------------------- | ------------------------- | ------------------------- | ----------
4T         | 4.00x (5110.0180ms)       | 5.41x (9185.2792ms)       | 6.10x (9910.7934ms)       | PASS
8T         | 7.56x (2706.4001ms)       | 7.02x (7078.1891ms)       | 8.14x (7427.6011ms)       | PASS
10T        | 8.55x (2391.0355ms)       | 7.42x (6703.2937ms)       | 8.43x (7178.6619ms)       | PASS
12T        | 9.40x (2175.0796ms)       | 7.97x (6240.0617ms)       | 9.12x (6630.8897ms)       | PASS
16T        | 11.41x (1793.3045ms)      | 9.09x (5468.9073ms)       | 9.15x (6608.7900ms)       | PASS

Full detailed report saved to: results/comparison_20260128_140233.txt




hobbbit31@Smaug:~/Desktop/tester.$ ./compare.sh
==============================================
  TEST CASES BENCHMARK
==============================================
Orders:         10000000
Stocks:         100000
Snapshot Freq:  1000000
==============================================
Compiling...Done (with -O3 optimization).
----------------------------------------------
Running SEQUENTIAL version...

SEQUENTIAL BASELINE (1.0x)
Function             | Time (ms)      
-------------------- | ---------------
totalAmountTraded    | 1053.8052      
printOrderStats      | 3129.2228      
updateDisplay        | 4178.1853      
-------------------- | ---------------
TOTAL                | 8361.2133      

PARALLEL PERFORMANCE
Threads    | TotalAmount               | OrderStats                | UpdateDisp                | Status    
---------- | ------------------------- | ------------------------- | ------------------------- | ----------
4T         | 4.10x (256.9311ms)        | 4.09x (763.3481ms)        | 7.40x (564.3145ms)        | PASS
8T         | 7.65x (137.6501ms)        | 5.47x (571.1846ms)        | 9.20x (453.7995ms)        | PASS
10T        | 9.10x (115.7074ms)        | 6.54x (478.4082ms)        | 9.96x (419.4345ms)        | PASS
12T        | 9.54x (110.4104ms)        | 6.76x (462.8963ms)        | 9.60x (434.8604ms)        | PASS
16T        | 12.26x (85.8868ms)        | 6.63x (471.7911ms)        | 10.63x (392.8740ms)       | PASS

Full detailed report saved to: results/comparison_20260128_141158.txt
hobbbit31@Smaug:~/Desktop/tester.$ 




hobbbit31@Smaug:~/Desktop/tester.$ ./compare.sh
==============================================
  TEST CASES BENCHMARK
==============================================
Orders:         10000000
Stocks:         10000
Snapshot Freq:  10000
==============================================
Compiling...Done (with -O3 optimization).
----------------------------------------------
Running SEQUENTIAL version...

SEQUENTIAL BASELINE (1.0x)
Function             | Time (ms)      
-------------------- | ---------------
totalAmountTraded    | 943.6569       
printOrderStats      | 1724.2539      
updateDisplay        | 4357.2698      
-------------------- | ---------------
TOTAL                | 7025.1806      

PARALLEL PERFORMANCE
Threads    | TotalAmount               | OrderStats                | UpdateDisp                | Status    
---------- | ------------------------- | ------------------------- | ------------------------- | ----------
4T         | 3.90x (241.4142ms)        | 6.16x (279.5447ms)        | 4.89x (890.7427ms)        | PASS
8T         | 7.49x (125.9423ms)        | 8.89x (193.8667ms)        | 7.43x (586.3575ms)        | PASS
10T        | 7.76x (121.5775ms)        | 10.08x (170.9771ms)       | 8.32x (523.3139ms)        | PASS
12T        | 8.99x (104.9670ms)        | 12.51x (137.7536ms)       | 9.17x (474.7918ms)        | PASS
16T        | 11.55x (81.6607ms)        | 14.39x (119.7513ms)       | 10.19x (427.4007ms)       | PASS

Full detailed report saved to: results/comparison_20260128_141325.txt
hobbbit31@Smaug:~/Desktop/tester.$ 