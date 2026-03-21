CXX = g++
CXXFLAGS = -O0 -fopenmp

all: a2

a2: main.cpp
	$(CXX) $(CXXFLAGS) -o a2 main.cpp -lm  

clean:
	rm -f a2 knn.txt approx_knn.txt kmeans.txt *.txt
