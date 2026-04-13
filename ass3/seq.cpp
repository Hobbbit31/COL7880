#include <iostream>
#include <vector>
#include <algorithm>
#include <numeric>
#include <fstream>
#include <chrono>

using namespace std;
using namespace std::chrono;

struct Vertex {
    int id;
    long long profit;
    long long cost;
};

int N, E;
long long B;
vector<Vertex> vertices;
vector<vector<int>> adj;
long long maxProfit = 0;
vector<int> bestClique;

// Structural Bound (Greedy Graph Coloring)
long long structuralBound(const vector<int>& cand) {
    if (cand.empty()) return 0;

    vector<int> sortedCand = cand;
    sort(sortedCand.begin(), sortedCand.end(), [](int a, int b) {
        return vertices[a].profit > vertices[b].profit;
    });

    vector<vector<int>> colorClasses;
    for (int v : sortedCand) {
        bool placed = false;
        for (auto& color_class : colorClasses) {
            bool canAdd = true;
            for (int u : color_class) {
                if (adj[v][u]) { canAdd = false; break; }
            }
            if (canAdd) { color_class.push_back(v); placed = true; break; }
        }
        if (!placed) colorClasses.push_back({v});
    }

    long long bound = 0;
    for (const auto& color_class : colorClasses) {
        long long maxP = 0;
        for (int v : color_class) maxP = max(maxP, vertices[v].profit);
        bound += maxP;
    }
    return bound;
}

// Resource Bound (Fractional Knapsack)
double knapsackBound(const vector<int>& cand, long long remBudget) {
    if (cand.empty() || remBudget <= 0) return 0;

    vector<int> sortedCand = cand;
    sort(sortedCand.begin(), sortedCand.end(), [](int a, int b) {
        double r1 = (double)vertices[a].profit / vertices[a].cost;
        double r2 = (double)vertices[b].profit / vertices[b].cost;
        return r1 > r2;
    });

    double bound = 0;
    long long curBudget = remBudget;
    for (int v : sortedCand) {
        if (vertices[v].cost <= curBudget) {
            bound += vertices[v].profit;
            curBudget -= vertices[v].cost;
        } else {
            bound += (double)vertices[v].profit * curBudget / vertices[v].cost;
            break;
        }
    }
    return bound;
}

void findClique(vector<int> cand, vector<int> candCurr, long long P_curr, long long W_curr) {
    if (cand.empty()) {
        if (P_curr > maxProfit) {
            maxProfit = P_curr;
            bestClique = candCurr;
        }
        return;
    }

    // Structural Bound
    if (P_curr + structuralBound(cand) <= maxProfit) return;

    // Resource Bound
    if (P_curr + knapsackBound(cand, B - W_curr) <= maxProfit) return;

    while (!cand.empty()) {
        int v = cand.back();
        cand.pop_back();

        if (W_curr + vertices[v].cost <= B) {
            if (P_curr + vertices[v].profit > maxProfit) {
                maxProfit = P_curr + vertices[v].profit;
                bestClique = candCurr;
                bestClique.push_back(v);
            }

            vector<int> C_next;
            for (int u : cand) {
                if (adj[v][u]) C_next.push_back(u);
            }

            vector<int> candCurr_next = candCurr;
            candCurr_next.push_back(v);
            findClique(C_next, candCurr_next, P_curr + vertices[v].profit, W_curr + vertices[v].cost);
        }
    }
}

int main(int argc, char* argv[]) {
    auto start = high_resolution_clock::now();

    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <input_file> [output_file]" << endl;
        return 1;
    }

    ifstream infile(argv[1]);
    if (!infile) {
        cerr << "Error opening file: " << argv[1] << endl;
        return 1;
    }

    if (!(infile >> N >> E >> B)) return 0;

    vertices.resize(N);
    for (int i = 0; i < N; ++i) {
        vertices[i].id = i;
        infile >> vertices[i].profit >> vertices[i].cost;
    }

    adj.assign(N, vector<int>(N, 0));
    for (int i = 0; i < E; ++i) {
        int u, v;
        infile >> u >> v;
        adj[u][v] = adj[v][u] = 1;
    }

    vector<int> initial_cand(N);
    iota(initial_cand.begin(), initial_cand.end(), 0);
    sort(initial_cand.begin(), initial_cand.end(), [](int a, int b) {
        if (vertices[a].profit != vertices[b].profit)
            return vertices[a].profit < vertices[b].profit;
        return vertices[a].cost > vertices[b].cost;
    });

    findClique(initial_cand, {}, 0, 0);

    ostream* out = &cout;
    ofstream outfile_res;
    if (argc >= 3) {
        outfile_res.open(argv[2]);
        if (outfile_res) out = &outfile_res;
    }

    *out << maxProfit << endl;
    sort(bestClique.begin(), bestClique.end());
    for (int i = 0; i < (int)bestClique.size(); ++i) {
        *out << bestClique[i] << (i == (int)bestClique.size() - 1 ? "" : " ");
    }
    *out << endl;

    auto stop = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(stop - start)/1000.0;
    cerr << "Execution time: " << duration.count() << " milliseconds" << endl;

    return 0;
}