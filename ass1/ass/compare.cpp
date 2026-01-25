#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>

using namespace std;
namespace fs = std::filesystem;

bool compareFiles(const fs::path& file1, const fs::path& file2) {
    ifstream f1(file1, ios::binary);
    ifstream f2(file2, ios::binary);

    if (!f1.is_open() || !f2.is_open()) return false;

    f1.seekg(0, ios::end);
    f2.seekg(0, ios::end);

    if (f1.tellg() != f2.tellg()) return false;

    f1.seekg(0, ios::beg);
    f2.seekg(0, ios::beg);

    char c1, c2;
    while (f1.get(c1) && f2.get(c2)) {
        if (c1 != c2) return false;
    }

    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "Usage: " << argv[0] << " <folder1> <folder2>\n";
        return 1;
    }

    fs::path folder1 = argv[1];
    fs::path folder2 = argv[2];

    if (!fs::exists(folder1) || !fs::is_directory(folder1)) {
        cerr << "Error: Folder1 does not exist or is not a directory.\n";
        return 1;
    }

    if (!fs::exists(folder2) || !fs::is_directory(folder2)) {
        cerr << "Error: Folder2 does not exist or is not a directory.\n";
        return 1;
    }

    int matched = 0, mismatched = 0, missing = 0;

    for (const auto& entry : fs::directory_iterator(folder1)) {
        if (!entry.is_regular_file()) continue;

        fs::path file1 = entry.path();
        if (file1.extension() != ".txt") continue;

        fs::path file2 = folder2 / file1.filename();

        if (!fs::exists(file2)) {
            cout << "[MISSING] " << file1.filename().string() << " not found in folder2\n";
            missing++;
            continue;
        }

        bool same = compareFiles(file1, file2);

        if (same) {
            cout << "[MATCH]    " << file1.filename().string() << "\n";
            matched++;
        } else {
            cout << "[DIFF]     " << file1.filename().string() << "\n";
            mismatched++;
        }
    }

    cout << "\n===== SUMMARY =====\n";
    cout << "Matched   : " << matched << "\n";
    cout << "Different : " << mismatched << "\n";
    cout << "Missing   : " << missing << "\n";

    return 0;
}