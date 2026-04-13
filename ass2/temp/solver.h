// solver.h
// This file has the main Branch and Bound recursive function
// It explores all possible cliques and prunes using coloring and knapsack bounds

#ifndef SOLVER_H
#define SOLVER_H

#include <vector>
#include <algorithm>
#include "graph.h"
#include "bounds.h"

using namespace std;

// global variables to store the best solution found so far
int bestProfit = 0;
vector<int> bestClique;

// findClique - recursive branch and bound function
// candidates: vertices that can still be added to the clique
// currentProfit: total profit of vertices in current clique
// currentCost: total cost of vertices in current clique
// currentClique: the clique we are building
// g: the graph
void findClique(const vector<int> &candidates, int currentProfit, int currentCost,
                vector<int> &currentClique, const Graph &g) {

    // Pruning Step 1: check coloring bound
    // if even the best case (from coloring) can't beat our best, stop
    double cBound = coloringBound(candidates, g);
    if (currentProfit + cBound <= bestProfit) {
        return;  // prune this branch
    }

    // Pruning Step 2: check knapsack bound
    // if even the best case (from knapsack) can't beat our best, stop
    int remainingBudget = g.B - currentCost;
    double kBound = knapsackBound(candidates, g, remainingBudget);
    if (currentProfit + kBound <= bestProfit) {
        return;  // prune this branch
    }

    // Branching: try adding each candidate vertex to the clique
    vector<int> cands = candidates;

    while (!cands.empty()) {
        // pick the last vertex (we sorted by profit earlier, so high profit comes last)
        int v = cands.back();
        cands.pop_back();

        // check if adding v is within budget
        if (currentCost + g.cost[v] <= g.B) {
            int newProfit = currentProfit + g.profit[v];

            // update best solution if this is better
            if (newProfit > bestProfit) {
                bestProfit = newProfit;
                bestClique = currentClique;
                bestClique.push_back(v);
            }

            // find next candidates: vertices in cands that are neighbors of v
            // (because a clique requires all vertices to be connected)
            vector<int> nextCands;
            for (int i = 0; i < (int)cands.size(); i++) {
                if (g.isAdj(v, cands[i])) {
                    nextCands.push_back(cands[i]);
                }
            }

            // recurse: add v to clique and explore further
            currentClique.push_back(v);
            findClique(nextCands, newProfit, currentCost + g.cost[v], currentClique, g);
            currentClique.pop_back();  // backtrack
        }
    }
}

#endif
