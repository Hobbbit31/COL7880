#include <mpi.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <numeric>
#include <string>

using namespace std;

// graph stored as flat N*N adjacency matrix (fast row access)
struct Graph {
    int N, E, B;              // num vertices, edges, budget
    vector<int> profit, cost; // per-vertex profit and cost
    vector<char> adj;         // row-major adjacency matrix

    Graph() : N(0), E(0), B(0) {}

    // get pointer to row u of adj matrix
    const char *adjRow(int u) const {
        return adj.data() + (long long)u * N;
    }

    // mark edge u-v in both directions
    void setEdge(int u, int v) {
        adj[(long long)u * N + v] = 1;
        adj[(long long)v * N + u] = 1;
    }

    // allocate storage once N is known (used by non-root ranks)
    void allocate() {
        profit.resize(N, 0);
        cost.resize(N, 0);
        adj.assign((long long)N * N, 0);
    }

    // parse input file: N E B, then profit/cost per vertex, then edge list
    bool readFromFile(const string &fname) {
        ifstream fin(fname);
        if (!fin.is_open()) {
            cout << "Cannot open: " << fname << endl;
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

// greedy coloring upper bound - vertices sharing a color are independent,
// so at most one per color can join the clique => sum of best profit per color
double colorBound(const vector<int> &candidates, const Graph &g) {
    if (candidates.empty()) return 0.0;

    // sort by profit descending so highest-profit vertex gets first pick of colors
    static vector<int> sorted;
    sorted.assign(candidates.begin(), candidates.end());
    sort(sorted.begin(), sorted.end(), [&](int a, int b) {
        return g.profit[a] > g.profit[b];
    });

    // static buffers to avoid reallocating on every call
    static vector<vector<int>> colorClasses;
    static vector<int> maxProfit;
    int numColors = 0;
    maxProfit.clear();

    for (int i = 0; i < (int)sorted.size(); i++) {
        int v = sorted[i];
        bool placed = false;
        const char *row = g.adjRow(v);
        // try to place v into an existing color class (no edge to any member)
        for (int c = 0; c < numColors; c++) {
            bool conflict = false;
            for (int j = 0; j < (int)colorClasses[c].size(); j++) {
                if (row[colorClasses[c][j]] == 1) { conflict = true; break; }
            }
            if (!conflict) {
                colorClasses[c].push_back(v);
                if (g.profit[v] > maxProfit[c]) maxProfit[c] = g.profit[v];
                placed = true;
                break;
            }
        }
        // v needs a fresh color class
        if (!placed) {
            if (numColors < (int)colorClasses.size()) {
                colorClasses[numColors].clear();
                colorClasses[numColors].push_back(v);
            } else {
                colorClasses.push_back({v});
            }
            maxProfit.push_back(g.profit[v]);
            numColors++;
        }
    }

    // upper bound = sum of the best profit in every color class
    double bound = 0.0;
    for (int c = 0; c < numColors; c++) bound += maxProfit[c];
    for (int c = 0; c < numColors; c++) colorClasses[c].clear();
    return bound;
}

// fractional knapsack upper bound on profit achievable within remaining budget
double knapsackBound(const vector<int> &candidates, const Graph &g, int budgetLeft) {
    if (candidates.empty()) return 0.0;
    double bound = 0.0;
    int remaining = budgetLeft;
    // candidates are pre-sorted ascending by profit/cost ratio, so walk backwards = best ratio first
    for (int i = (int)candidates.size() - 1; i >= 0; i--) {
        int v = candidates[i];
        if (g.cost[v] == 0) { bound += g.profit[v]; continue; } // free vertex, take it
        if (remaining <= 0) break;
        if (g.cost[v] <= remaining) {
            // fits fully
            bound += g.profit[v];
            remaining -= g.cost[v];
        } else {
            // take fractional piece and stop
            bound += (double)g.profit[v] / g.cost[v] * remaining;
            break;
        }
    }
    return bound;
}

// global best (per rank) - updated during recursion
int bestProfit = 0;
vector<int> bestClique;

// recursive B&B: try extending curClique with each candidate
void findClique(const vector<int> &candidates, int curProfit, int curCost, vector<int> &curClique, const Graph &g) {
    // prune: coloring bound says we cannot beat current best
    double cb = colorBound(candidates, g);
    if (curProfit + cb <= bestProfit) return;
    // prune: knapsack bound says the remaining budget isn't enough either
    double kb = knapsackBound(candidates, g, g.B - curCost);
    if (curProfit + kb <= bestProfit) return;

    vector<int> cands = candidates;
    while (!cands.empty()) {
        // pick last candidate (best ratio due to pre-sort)
        int v = cands.back();
        cands.pop_back();
        if (curCost + g.cost[v] > g.B) continue; // doesn't fit in budget

        int newProfit = curProfit + g.profit[v];
        // update best-so-far if adding v gives a better clique
        if (newProfit > bestProfit) {
            bestProfit = newProfit;
            bestClique = curClique;
            bestClique.push_back(v);
        }

        // a clique needs every vertex adjacent to v, so keep only v's neighbors
        vector<int> nextCands;
        nextCands.reserve(cands.size());
        const char *row = g.adjRow(v);
        for (int i = 0; i < (int)cands.size(); i++) {
            if (row[cands[i]] == 1) nextCands.push_back(cands[i]);
        }

        curClique.push_back(v);
        findClique(nextCands, newProfit, curCost + g.cost[v], curClique, g);
        curClique.pop_back(); // backtrack
    }
}

// one independent subproblem = partial clique + remaining candidates
struct Job {
    vector<int> clique;
    vector<int> candidates;
    int profit, cost;
};

// expand B&B tree to maxDepth so each subtree becomes a Job for MPI to distribute
void generateJobs(const vector<int> &candidates, const vector<int> &clique, int profit, int cost, int depth, int maxDepth, const Graph &g, vector<Job> &jobs) {
    // leaf of the expansion tree - save as a job
    if (depth >= maxDepth || candidates.empty()) {
        if (!candidates.empty() || !clique.empty())
            jobs.push_back({clique, candidates, profit, cost});
        return;
    }
    int before = jobs.size();
    vector<int> cands = candidates;
    while (!cands.empty()) {
        int v = cands.back();
        cands.pop_back();
        if (cost + g.cost[v] <= g.B) {
            // restrict candidates to neighbors of v (clique property)
            vector<int> nextCands;
            const char *row = g.adjRow(v);
            for (int i = 0; i < (int)cands.size(); i++)
                if (row[cands[i]] == 1) nextCands.push_back(cands[i]);
            vector<int> newClique = clique;
            newClique.push_back(v);
            generateJobs(nextCands, newClique, profit + g.profit[v], cost + g.cost[v], depth + 1, maxDepth, g, jobs);
        }
    }
    // if the whole subtree got pruned, still emit current clique as a leaf job
    if ((int)jobs.size() == before && !clique.empty())
        jobs.push_back({clique, {}, profit, cost});
}

// pick an expansion depth that yields enough jobs to keep every rank busy
int chooseDepth(const vector<int> &verts, int nprocs, const Graph &g) {
    if (nprocs <= 1) return 1;
    int target = 8 * nprocs; // aim for ~8 jobs per rank
    for (int d = 1; d <= 4; d++) {
        vector<Job> tmp;
        generateJobs(verts, {}, 0, 0, 0, d, g, tmp);
        if ((int)tmp.size() >= target || d == 4) return d;
    }
    return 1;
}

// share the graph from rank 0 to all other ranks
void broadcastGraph(Graph &g, int rank) {
    // first send the scalars so other ranks can allocate
    MPI_Bcast(&g.N, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&g.E, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&g.B, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (rank != 0) g.allocate();
    MPI_Bcast(g.profit.data(), g.N, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(g.cost.data(), g.N, MPI_INT, 0, MPI_COMM_WORLD);
    // adjacency matrix can be huge, so send in chunks to stay under MPI int limit
    long long total = (long long)g.N * g.N;
    long long off = 0;
    while (off < total) {
        int chunk = min((long long)100000000, total - off);
        MPI_Bcast(g.adj.data() + off, chunk, MPI_CHAR, 0, MPI_COMM_WORLD);
        off += chunk;
    }
}

int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);
    // double startTime = MPI_Wtime();

    int rank, nprocs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    if (argc < 3) {
        if (rank == 0) cout << "Usage: mpirun -np <P> ./main <input> <output>" << endl;
        MPI_Finalize();
        return 1;
    }

    // rank 0 reads the input, then tells everyone whether it worked
    Graph g;
    int ok = 1;
    if (rank == 0) {
        if (!g.readFromFile(argv[1])) ok = 0;
    }
    MPI_Bcast(&ok, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (!ok) { MPI_Finalize(); return 1; }

    // share graph with every rank
    broadcastGraph(g, rank);
    if (g.N == 0) {
        // empty graph - write empty output and exit
        if (rank == 0) { ofstream f(argv[2]); f << 0 << "\n\n"; }
        MPI_Finalize();
        return 0;
    }

    // sort vertices by profit/cost ratio ascending - popping from the back later
    // gives us the best-ratio vertex first (helps knapsack bound be tight)
    vector<int> verts(g.N);
    for (int i = 0; i < g.N; i++) verts[i] = i;
    sort(verts.begin(), verts.end(), [&](int a, int b) {
        double ra = g.cost[a] > 0 ? (double)g.profit[a] / g.cost[a] : 1e18;
        double rb = g.cost[b] > 0 ? (double)g.profit[b] / g.cost[b] : 1e18;
        return ra < rb;
    });

    // expand the B&B tree a few levels to get independent jobs for MPI
    int depth = chooseDepth(verts, nprocs, g);
    vector<Job> jobs;
    generateJobs(verts, {}, 0, 0, 0, depth, g, jobs);

    bestProfit = 0;
    bestClique.clear();
    vector<int> finalClique;
    int globalBest = 0;

    if (nprocs <= 3) {
        // ROUND-ROBIN: every rank takes jobs where i % nprocs == rank.
        // Fine for small nprocs; master-worker would waste a whole rank.
        for (int i = 0; i < (int)jobs.size(); i++) {
            if (i % nprocs == rank) {
                Job &job = jobs[i];
                if (job.profit > bestProfit) { bestProfit = job.profit; bestClique = job.clique; }
                if (!job.candidates.empty())
                    findClique(job.candidates, job.profit, job.cost, job.clique, g);
            }
            // periodically share the best-so-far so other ranks can prune harder
            if ((i + 1) % nprocs == 0 || i == (int)jobs.size() - 1) {
                int gb = 0;
                MPI_Allreduce(&bestProfit, &gb, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
                if (gb > bestProfit) bestProfit = gb;
            }
        }

        // final global best across all ranks
        MPI_Allreduce(&bestProfit, &globalBest, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);

        // pick the rank that actually owns a clique equal to the global best
        int myProfit = 0;
        for (int i = 0; i < (int)bestClique.size(); i++) myProfit += g.profit[bestClique[i]];
        int myRank = (myProfit == globalBest && !bestClique.empty()) ? rank : nprocs;
        int winner = nprocs;
        MPI_Allreduce(&myRank, &winner, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
        if (winner >= nprocs) winner = 0;

        // winner ships its clique to rank 0 for output
        if (winner == 0) {
            if (rank == 0) finalClique = bestClique;
        } else {
            if (rank == winner) {
                int sz = bestClique.size();
                MPI_Send(&sz, 1, MPI_INT, 0, 0, MPI_COMM_WORLD);
                if (sz > 0) MPI_Send(bestClique.data(), sz, MPI_INT, 0, 1, MPI_COMM_WORLD);
            } else if (rank == 0) {
                int sz = 0;
                MPI_Recv(&sz, 1, MPI_INT, winner, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                if (sz > 0) {
                    finalClique.resize(sz);
                    MPI_Recv(finalClique.data(), sz, MPI_INT, winner, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                }
            }
        }
    } else {
        // MASTER-WORKER: rank 0 hands out jobs on demand, others compute.this is the design strategy that i have used 
        // Better load balance than round-robin when jobs have uneven cost.
        if (rank == 0) {
            // MASTER loop - keep dispatching until all workers are done
            globalBest = 0;
            int nextJob = 0, finished = 0, numWorkers = nprocs - 1;
            while (finished < numWorkers) {
                int req[2];
                MPI_Status status;
                MPI_Recv(req, 2, MPI_INT, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);
                int src = status.MPI_SOURCE;
                int workerBest = req[0], cliqueSize = req[1];

                // worker reports its best clique - keep it if it beats the global best
                if (cliqueSize > 0 && workerBest > globalBest) {
                    vector<int> wClique(cliqueSize);
                    MPI_Recv(wClique.data(), cliqueSize, MPI_INT, src, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    globalBest = workerBest;
                    finalClique = wClique;
                } else if (cliqueSize > 0) {
                    // still need to drain the pending send even if we discard it
                    vector<int> tmp(cliqueSize);
                    MPI_Recv(tmp.data(), cliqueSize, MPI_INT, src, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                }
                if (workerBest > globalBest) globalBest = workerBest;

                // hand out next job, or -1 to tell the worker to stop
                int reply[2];
                if (nextJob < (int)jobs.size()) {
                    reply[0] = nextJob++;
                    reply[1] = globalBest;
                } else {
                    reply[0] = -1;
                    reply[1] = globalBest;
                    finished++;
                }
                MPI_Send(reply, 2, MPI_INT, src, 2, MPI_COMM_WORLD);
            }
        } else {
            // WORKER loop - keep asking for jobs until master says done
            while (true) {
                // report our best-so-far and ask for next job
                int req[2] = {bestProfit, (int)bestClique.size()};
                MPI_Send(req, 2, MPI_INT, 0, 0, MPI_COMM_WORLD);
                if (!bestClique.empty())
                    MPI_Send(bestClique.data(), bestClique.size(), MPI_INT, 0, 1, MPI_COMM_WORLD);

                int reply[2];
                MPI_Recv(reply, 2, MPI_INT, 0, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                if (reply[0] < 0) break; // no more jobs
                if (reply[1] > bestProfit) bestProfit = reply[1]; // tighten local bound

                Job &job = jobs[reply[0]];
                if (job.profit > bestProfit) { bestProfit = job.profit; bestClique = job.clique; }
                if (!job.candidates.empty())
                    findClique(job.candidates, job.profit, job.cost, job.clique, g);
            }
        }
    }

    // rank 0 writes the final answer
    if (rank == 0) {
        sort(finalClique.begin(), finalClique.end());
        ofstream fout(argv[2]);
        fout << globalBest << "\n";
        for (int i = 0; i < (int)finalClique.size(); i++) {
            if (i > 0) fout << " ";
            fout << finalClique[i];
        }
        fout << "\n";
    }

    // if (rank == 0)
    //     cout << "Execution time: " << (MPI_Wtime() - startTime) << " seconds" << endl;

    MPI_Finalize();
    return 0;
}
