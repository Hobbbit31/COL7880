all:
	g++ -std=c++17 -fopenmp tests.cpp functions_sequential.cpp functions.cpp -o testgen

clean:
	rm -f testgen stats* snap*
	rm -f output/seq/snap_*.txt
	rm -f output/par/snap_*.txt
	rm -f output/seq/stats.txt
	rm -f output/par/stats.txt
	rm -f testSeq/snap_*.txt
	rm -f testPar/snap_*.txt
	rm -f testSeq/stats.txt
	rm -f testPar/stats.txt