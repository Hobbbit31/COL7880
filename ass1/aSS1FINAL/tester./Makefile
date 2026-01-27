all:
	g++ -std=c++17 -fopenmp tests.cpp functions_sequential.cpp functions.cpp -o testgen

clean:
	rm -f testgen stats* snap*
	rm -rf testSeq testPar
	mkdir testSeq
	mkdir testPar