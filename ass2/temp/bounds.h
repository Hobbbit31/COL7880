// bounds.h
// This file has two bounding functions used in Branch and Bound
// 1. Coloring Bound (structural bound) - uses greedy graph coloring
// 2. Knapsack Bound (resource bound) - uses fractional knapsack

#ifndef BOUNDS_H
#define BOUNDS_H

#include <vector>
#include <algorithm>
#include "graph.h"

using namespace std;

// Coloring Bound:
// We color the candidate vertices using greedy coloring.
// In each color class, only one vertex can be in the clique (since they are independent).
// So upper bound = sum of maximum profit in each color class.
double coloringBound(const vector<int> &candidates, const Graph &g) {
    if (candidates.empty()) return 0.0;

    // sort candidates by profit in decreasing order
    vector<int> sorted_cands = candidates;
    sort(sorted_cands.begin(), sorted_cands.end(), [&](int a, int b) {
        return g.profit[a] > g.profit[b];
    });

    // each color class is a list of vertices that are NOT connected to each other
    vector<vector<int>> colors;

    // assign each vertex to a color class
    for (int i = 0; i < (int)sorted_cands.size(); i++) {
        int v = sorted_cands[i];
        bool placed = false;

        // try to put vertex v in an existing color class
        for (int c = 0; c < (int)colors.size(); c++) {
            bool hasConflict = false;

            // check if v is connected to any vertex in this color class
            for (int j = 0; j < (int)colors[c].size(); j++) {
                if (g.isAdj(v, colors[c][j])) {
                    hasConflict = true;
                    break;
                }
            }

            // if no conflict, add v to this color class
            if (!hasConflict) {
                colors[c].push_back(v);
                placed = true;
                break;
            }
        }

        // if v conflicts with all existing classes, create a new one
        if (!placed) {
            vector<int> newClass;
            newClass.push_back(v);
            colors.push_back(newClass);
        }
    }

    // upper bound = sum of max profit in each color class
    double bound = 0.0;
    for (int c = 0; c < (int)colors.size(); c++) {
        int maxProfit = 0;
        for (int j = 0; j < (int)colors[c].size(); j++) {
            int v = colors[c][j];
            if (g.profit[v] > maxProfit) {
                maxProfit = g.profit[v];
            }
        }
        bound += maxProfit;
    }

    return bound;
}

// Knapsack Bound:
// Treat each candidate vertex as a knapsack item with profit and cost.
// Sort by profit/cost ratio and greedily fill the remaining budget.
// Allow fractional items to get an upper bound.
double knapsackBound(const vector<int> &candidates, const Graph &g, int remainingBudget) {
    if (candidates.empty()) return 0.0;

    double bound = 0.0;
    int budgetLeft = remainingBudget;

    // store ratio, profit, cost for each candidate
    vector<double> ratio;
    vector<int> prof;
    vector<int> cst;

    for (int i = 0; i < (int)candidates.size(); i++) {
        int v = candidates[i];
        if (g.cost[v] == 0) {
            // zero cost items are free, add them directly
            bound += g.profit[v];
        } else {
            ratio.push_back((double)g.profit[v] / g.cost[v]);
            prof.push_back(g.profit[v]);
            cst.push_back(g.cost[v]);
        }
    }

    if (budgetLeft <= 0) return bound;

    // sort items by ratio in decreasing order (using index sorting)
    vector<int> order;
    for (int i = 0; i < (int)ratio.size(); i++) {
        order.push_back(i);
    }
    sort(order.begin(), order.end(), [&](int a, int b) {
        return ratio[a] > ratio[b];
    });

    // greedily pick items
    for (int i = 0; i < (int)order.size(); i++) {
        int idx = order[i];
        if (cst[idx] <= budgetLeft) {
            // item fits completely
            bound += prof[idx];
            budgetLeft -= cst[idx];
        } else {
            // take fraction of this item and stop
            bound += ratio[idx] * budgetLeft;
            break;
        }
    }

    return bound;
}

#endif
