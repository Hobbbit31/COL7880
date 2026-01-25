#include <iostream>
#include <fstream>
#include <random>
#include <iomanip>
#include <string>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cout << "Usage: " << argv[0] << " <N> <output_file>\n";
        return 1;
    }

    int N;
    try {
        N = std::stoi(argv[1]);
    } catch (...) {
        std::cout << "Error: N must be an integer\n";
        return 1;
    }

    std::string filename = argv[2];

    std::ofstream file(filename);
    if (!file) {
        std::cout << "Error: could not open file " << filename << "\n";
        return 1;
    }

    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist(0, UINT64_MAX);

    for (int i = 0; i < N; i++) {
        uint64_t x = dist(gen);
        file << std::uppercase << std::hex
             << std::setw(16) << std::setfill('0')
             << x << "\n";
    }

    file.close();
    std::cout << "Generated " << N << " random 64-bit hex values into " << filename << "\n";
    return 0;
}