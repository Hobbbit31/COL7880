// solver.h - branch and bound for max weight clique

#ifndef SOLVER_H
#define SOLVER_H

#include <vector>
#include <algorithm>
#include "graph.h"
#include "bounds.h"

using namespace std;

int bestProfit = 0;
vector<int> bestClique;

// recursive B&B - tries adding each candidate to the clique
void findClique(const vector<int> &candidates, int currentProfit, int currentCost,
                vector<int> &currentClique, const Graph &g) {

    // prune if coloring bound says we cant do better
    double cBound = coloringBound(candidates, g);
    if (currentProfit + cBound <= bestProfit) return;

    // prune if knapsack bound says we cant do better
    int remainingBudget = g.B - currentCost;
    double kBound = knapsackBound(candidates, g, remainingBudget);
    if (currentProfit + kBound <= bestProfit) return;

    vector<int> cands = candidates;

    while (!cands.empty()) {
        int v = cands.back();
        cands.pop_back();

        if (currentCost + g.cost[v] <= g.B) {
            int newProfit = currentProfit + g.profit[v];

            // update best if improved
            if (newProfit > bestProfit) {
                bestProfit = newProfit;
                bestClique = currentClique;
                bestClique.push_back(v);
            }

            // clique needs all vertices connected, so filter to only v's neighbors
            vector<int> nextCands;
            nextCands.reserve(cands.size());
            const char *adjRow = g.adjRow(v);
            for (int i = 0; i < (int)cands.size(); i++) {
                if (adjRow[cands[i]] == 1)
                    nextCands.push_back(cands[i]);
            }

            currentClique.push_back(v);
            findClique(nextCands, newProfit, currentCost + g.cost[v], currentClique, g);
            currentClique.pop_back(); // backtrack
        }
    }
}

#endif
