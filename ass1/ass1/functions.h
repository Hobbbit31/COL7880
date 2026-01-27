#include <iostream>
#include <string>
#include <cstdint>
#include <vector>
#include <iomanip>
#include <fstream>
#include <map>
#include <algorithm>
#include <cmath>

void updateDisplay_Par(const std::vector<uint64_t> &orderBook, int32_t freq);
int64_t totalAmountTraded_Par(const std::vector<uint64_t> &orderBook);
void printOrderStats_Par(const std::vector<uint64_t> &orderBook);