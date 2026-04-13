// graph.h
// This file contains the Graph structure to store vertices, edges, profits, costs
// We read the graph from a file and store it using an adjacency matrix

#ifndef GRAPH_H
#define GRAPH_H

#include <vector>
#include <string>
#include <fstream>
#include <iostream>

using namespace std;

struct Graph {
    int N;  // number of vertices
    int E;  // number of edges
    int B;  // budget limit

    vector<int> profit;  // profit of each vertex
    vector<int> cost;    // cost of each vertex

    // adjacency matrix stored as 1D array (using char to save memory)
    // adj[i*N + j] = 1 means there is an edge between vertex i and vertex j
    vector<char> adj;

    // constructor - initialize everything to 0
    Graph() {
        N = 0;
        E = 0;
        B = 0;
    }

    // check if two vertices are connected
    bool isAdj(int u, int v) const {
        return adj[(long long)u * N + v] == 1;
    }

    // add an edge between two vertices (undirected graph)
    void setEdge(int u, int v) {
        adj[(long long)u * N + v] = 1;
        adj[(long long)v * N + u] = 1;
    }

    // allocate memory for arrays (used by non-root MPI ranks after broadcast)
    void allocate() {
        profit.resize(N, 0);
        cost.resize(N, 0);
        adj.assign((long long)N * N, 0);
    }

    // read graph from input file
    // format: first line has N E B
    //         next N lines have profit[i] cost[i]
    //         next E lines have u v (edges)
    bool readFromFile(const string &filename) {
        ifstream fin(filename);
        if (!fin.is_open()) {
            cout << "Cannot open file: " << filename << endl;
            return false;
        }

        // read number of vertices, edges, and budget
        fin >> N >> E >> B;

        // allocate arrays
        profit.resize(N);
        cost.resize(N);
        adj.assign((long long)N * N, 0);

        // read profit and cost for each vertex
        for (int i = 0; i < N; i++) {
            fin >> profit[i] >> cost[i];
        }

        // read edges
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
