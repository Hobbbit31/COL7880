#ifndef GRAPH_H
#define GRAPH_H

#include <vector>
#include <string>
#include <fstream>
#include <iostream>

using namespace std;

struct Graph {
    int N, E, B; // vertices, edges, budget
    vector<int> profit, cost;
    vector<char> adj; // 1D adj matrix (char to save space)

    Graph() : N(0), E(0), B(0) {}

    bool isAdj(int u, int v) const {
        return adj[(long long)u * N + v] == 1;
    }

    // returns pointer to row u in adj matrix (avoids recomputing index)
    const char *adjRow(int u) const {
        return adj.data() + (long long)u * N;
    }

    void setEdge(int u, int v) {
        adj[(long long)u * N + v] = 1;
        adj[(long long)v * N + u] = 1;
    }

    // non-root ranks call this after getting N,E,B via broadcast
    void allocate() {
        profit.resize(N, 0);
        cost.resize(N, 0);
        adj.assign((long long)N * N, 0);
    }

    // reads input file: N E B, then profits/costs, then edges
    bool readFromFile(const string &filename) {
        ifstream fin(filename);
        if (!fin.is_open()) {
            cout << "Cannot open file: " << filename << endl;
            return false;
        }

        fin >> N >> E >> B;
        profit.resize(N);
        cost.resize(N);
        adj.assign((long long)N * N, 0);

        for (int i = 0; i < N; i++)
            fin >> profit[i] >> cost[i];

        for (int i = 0; i < E; i++) {
            int u, v;
            fin >> u >> v;
            setEdge(u, v);
        }

        fin.close();
        return true;
    }
};

#endif
