// bounds.h - upper bound functions for pruning

#ifndef BOUNDS_H
#define BOUNDS_H

#include <vector>
#include <algorithm>
#include "graph.h"

using namespace std;

// greedy coloring bound - independent set vertices share a color,
// so at most one per color can be in the clique => sum of max profit per color
double coloringBound(const vector<int> &candidates, const Graph &g) {
    if (candidates.empty()) return 0.0;

    // sort candidates by profit descending for greedy coloring (as required by pseudocode)
    static vector<int> sorted_cands;
    sorted_cands.assign(candidates.begin(), candidates.end());
    sort(sorted_cands.begin(), sorted_cands.end(), [&](int a, int b) {
        return g.profit[a] > g.profit[b];
    });

    static vector<vector<int>> colorMembers;
    static vector<int> maxProfitOfColor;
    int numColors = 0;
    maxProfitOfColor.clear();

    for (int i = 0; i < (int)sorted_cands.size(); i++) {
        int v = sorted_cands[i];
        bool placed = false;
        const char *adjRow = g.adjRow(v);

        for (int c = 0; c < numColors; c++) {
            bool hasConflict = false;
            auto &members = colorMembers[c];
            for (int j = 0; j < (int)members.size(); j++) {
                if (adjRow[members[j]] == 1) {
                    hasConflict = true;
                    break;
                }
            }
            if (!hasConflict) {
                members.push_back(v);
                if (g.profit[v] > maxProfitOfColor[c])
                    maxProfitOfColor[c] = g.profit[v];
                placed = true;
                break;
            }
        }

        if (!placed) {
            if (numColors < (int)colorMembers.size()) {
                colorMembers[numColors].clear();
                colorMembers[numColors].push_back(v);
            } else {
                colorMembers.push_back({v});
            }
            maxProfitOfColor.push_back(g.profit[v]);
            numColors++;
        }
    }

    double bound = 0.0;
    for (int c = 0; c < numColors; c++)
        bound += maxProfitOfColor[c];

    for (int c = 0; c < numColors; c++) colorMembers[c].clear();

    return bound;
}

// fractional knapsack bound - treats vertices as items, allows fractional
// picking to get an upper bound on profit within remaining budget
double knapsackBound(const vector<int> &candidates, const Graph &g, int remainingBudget) {
    if (candidates.empty()) return 0.0;

    double bound = 0.0;
    int budgetLeft = remainingBudget;

    // candidates are sorted by ratio ascending, iterate in reverse = ratio descending
    for (int i = (int)candidates.size() - 1; i >= 0; i--) {
        int v = candidates[i];
        if (g.cost[v] == 0) {
            bound += g.profit[v];
            continue;
        }
        if (budgetLeft <= 0) break;
        if (g.cost[v] <= budgetLeft) {
            bound += g.profit[v];
            budgetLeft -= g.cost[v];
        } else {
            bound += (double)g.profit[v] / g.cost[v] * budgetLeft;
            break;
        }
    }
    return bound;
}

#endif
