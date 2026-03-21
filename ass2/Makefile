CXX = g++
CXXFLAGS = -O0 -fopenmp
NVCC = nvcc
NVCCFLAGS = -O2

all: a2

a2: main.cpp
	$(CXX) $(CXXFLAGS) -o a2 main.cpp -lm

a2_cuda: main_cuda.cu
	$(NVCC) $(NVCCFLAGS) -o a2_cuda main_cuda.cu -lm

a2_cuda_opt: main_cuda_opt.cu
	$(NVCC) $(NVCCFLAGS) -o a2_cuda_opt main_cuda_opt.cu -lm

clean:
	rm -f a2 a2_cuda knn.txt approx_knn.txt kmeans.txt *.txt
