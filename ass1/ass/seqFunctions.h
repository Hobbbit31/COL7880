#include <iostream>
#include <string>
#include <cstdint>
#include <vector>
#include <iomanip>
#include <fstream>
#include <map>
#include <algorithm>
#include <cmath>

void updateDisplay_Seq(const std::vector<uint64_t> &orderBook, int32_t freq);
int64_t totalAmountTraded_Seq(const std::vector<uint64_t> &orderBook);
void printOrderStats_Seq(const std::vector<uint64_t> &orderBook);