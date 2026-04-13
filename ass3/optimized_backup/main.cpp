// main.cpp - MPI parallel B&B for budgeted max weight clique
// compile: mpic++ -O3 -std=c++17 main.cpp -o main
// run: mpirun -np <P> ./main <input> <output>

#include <mpi.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <numeric>
#include "graph.h"
#include "bounds.h"
#include "solver.h"

using namespace std;

// each job is a subproblem with a partial clique and remaining candidates
struct Job {
    vector<int> clique;
    vector<int> candidates;
    int profit;
    int cost;
};

// expand the B&B tree to maxDepth to create independent jobs
void generateJobs(const vector<int> &candidates, const vector<int> &clique,
                  int profit, int cost, int depth, int maxDepth,
                  const Graph &g, vector<Job> &jobs) {

    if (depth >= maxDepth || candidates.empty()) {
        if (!candidates.empty() || !clique.empty()) {
            jobs.push_back({clique, candidates, profit, cost});
        }
        return;
    }

    int jobsBefore = jobs.size();
    vector<int> cands = candidates;

    while (!cands.empty()) {
        int v = cands.back();
        cands.pop_back();

        if (cost + g.cost[v] <= g.B) {
            vector<int> nextCands;
            const char *adjRow = g.adjRow(v);
            for (int i = 0; i < (int)cands.size(); i++) {
                if (adjRow[cands[i]] == 1)
                    nextCands.push_back(cands[i]);
            }

            vector<int> newClique = clique;
            newClique.push_back(v);
            generateJobs(nextCands, newClique, profit + g.profit[v],
                        cost + g.cost[v], depth + 1, maxDepth, g, jobs);
        }
    }

    // if nothing was generated, save current clique as a leaf job
    if ((int)jobs.size() == jobsBefore && !clique.empty()) {
        jobs.push_back({clique, {}, profit, cost});
    }
}

// figure out how deep to expand so we get enough jobs (at least 8 per process)
int chooseExpandDepth(const vector<int> &allVertices, int numProcs, const Graph &g) {
    if (numProcs <= 1) return 1;
    int target = 8 * numProcs;

    for (int depth = 1; depth <= 4; depth++) {
        vector<Job> testJobs;
        generateJobs(allVertices, {}, 0, 0, 0, depth, g, testJobs);
        if ((int)testJobs.size() >= target || depth == 4)
            return depth;
    }
    return 1;
}

// broadcast graph from rank 0 to everyone
void broadcastGraph(Graph &g, int rank) {
    MPI_Bcast(&g.N, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&g.E, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&g.B, 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (rank != 0) g.allocate();

    MPI_Bcast(g.profit.data(), g.N, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(g.cost.data(), g.N, MPI_INT, 0, MPI_COMM_WORLD);

    // send adj matrix in chunks (MPI has size limits)
    long long totalSize = (long long)g.N * g.N;
    long long offset = 0;
    while (offset < totalSize) {
        int chunkSize = min((long long)100000000, totalSize - offset);
        MPI_Bcast(g.adj.data() + offset, chunkSize, MPI_CHAR, 0, MPI_COMM_WORLD);
        offset += chunkSize;
    }

}

int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);
    double startTime = MPI_Wtime();

    int rank, numProcs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &numProcs);

    if (argc < 3) {
        if (rank == 0)
            cout << "Usage: mpirun -np <P> ./main <input_file> <output_file>" << endl;
        MPI_Finalize();
        return 1;
    }

    string inputFile = argv[1];
    string outputFile = argv[2];
    Graph g;

    // rank 0 reads input
    int readOk = 1;
    if (rank == 0) {
        if (!g.readFromFile(inputFile)) readOk = 0;
    }
    MPI_Bcast(&readOk, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (!readOk) { MPI_Finalize(); return 1; }

    broadcastGraph(g, rank);

    if (g.N == 0) {
        if (rank == 0) { ofstream fout(outputFile); fout << 0 << "\n\n"; }
        MPI_Finalize();
        return 0;
    }

    // sort vertices by profit ascending (so popping from back gives high profit first)
    vector<int> allVertices(g.N);
    for (int i = 0; i < g.N; i++) allVertices[i] = i;
    sort(allVertices.begin(), allVertices.end(), [&](int a, int b) {
        double ra = g.cost[a] > 0 ? (double)g.profit[a] / g.cost[a] : 1e18;
        double rb = g.cost[b] > 0 ? (double)g.profit[b] / g.cost[b] : 1e18;
        return ra < rb;
    });

    // generate jobs
    int expandDepth = chooseExpandDepth(allVertices, numProcs, g);
    vector<Job> jobs;
    generateJobs(allVertices, {}, 0, 0, 0, expandDepth, g, jobs);

    bestProfit = 0;
    bestClique.clear();

    // greedy initial solution: try starting from top-20 profit vertices
    for (int s = (int)allVertices.size() - 1; s >= max(0, (int)allVertices.size() - 20); s--) {
        vector<int> clique;
        int profit = 0, cost = 0;
        vector<int> cands = allVertices;

        int sv = allVertices[s];
        if (g.cost[sv] > g.B) continue;
        clique.push_back(sv);
        profit += g.profit[sv]; cost += g.cost[sv];

        // filter to neighbors of sv
        vector<int> filtered;
        const char *adjSv = g.adjRow(sv);
        for (int c : cands) if (c != sv && adjSv[c] == 1) filtered.push_back(c);
        cands = filtered;

        while (!cands.empty()) {
            int best = -1;
            for (int i = (int)cands.size() - 1; i >= 0; i--) {
                if (cost + g.cost[cands[i]] <= g.B) { best = i; break; }
            }
            if (best < 0) break;
            int v = cands[best];
            clique.push_back(v); profit += g.profit[v]; cost += g.cost[v];
            filtered.clear();
            const char *adjV = g.adjRow(v);
            for (int c : cands) if (c != v && adjV[c] == 1) filtered.push_back(c);
            cands = filtered;
        }
        if (profit > bestProfit) { bestProfit = profit; bestClique = clique; }
    }

    // pre-prune jobs that can't beat the greedy bound
    {
        vector<Job> prunedJobs;
        for (auto &job : jobs) {
            if (job.candidates.empty()) {
                if (job.profit > bestProfit) prunedJobs.push_back(job);
            } else {
                // quick upper bound: job profit + sum of candidate profits
                int sumP = job.profit;
                for (int v : job.candidates) sumP += g.profit[v];
                if (sumP > bestProfit) prunedJobs.push_back(job);
            }
        }
        jobs = prunedJobs;
    }

    vector<int> finalClique;
    int globalBest = 0;

    if (numProcs <= 3) {
        // ROUND-ROBIN for np=1,2 (master-worker wastes a rank)
        for (int i = 0; i < (int)jobs.size(); i++) {
            if (i % numProcs == rank) {
                Job &job = jobs[i];
                if (job.profit > bestProfit) {
                    bestProfit = job.profit;
                    bestClique = job.clique;
                }
                if (!job.candidates.empty())
                    findClique(job.candidates, job.profit, job.cost, job.clique, g);
            }
            if ((i + 1) % numProcs == 0 || i == (int)jobs.size() - 1) {
                int gb = 0;
                MPI_Allreduce(&bestProfit, &gb, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
                if (gb > bestProfit) bestProfit = gb;
            }
        }

        MPI_Allreduce(&bestProfit, &globalBest, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);

        int myCliqueProfit = 0;
        for (int i = 0; i < (int)bestClique.size(); i++)
            myCliqueProfit += g.profit[bestClique[i]];

        int myCandidate = (myCliqueProfit == globalBest && !bestClique.empty()) ? rank : numProcs;
        int winner = numProcs;
        MPI_Allreduce(&myCandidate, &winner, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
        if (winner >= numProcs) winner = 0;

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
        // MASTER-WORKER for np>=3
        if (rank == 0) {
            // MASTER
            globalBest = 0;
            int nextJob = 0;
            int numWorkers = numProcs - 1;
            int finishedWorkers = 0;

            while (finishedWorkers < numWorkers) {
                int req[2];
                MPI_Status status;
                MPI_Recv(req, 2, MPI_INT, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);
                int worker = status.MPI_SOURCE;
                int workerBest = req[0];
                int cliqueSize = req[1];

                if (cliqueSize > 0 && workerBest > globalBest) {
                    vector<int> wClique(cliqueSize);
                    MPI_Recv(wClique.data(), cliqueSize, MPI_INT, worker, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    globalBest = workerBest;
                    finalClique = wClique;
                } else if (cliqueSize > 0) {
                    vector<int> tmp(cliqueSize);
                    MPI_Recv(tmp.data(), cliqueSize, MPI_INT, worker, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                }
                if (workerBest > globalBest) globalBest = workerBest;

                int reply[2];
                if (nextJob < (int)jobs.size()) {
                    reply[0] = nextJob++;
                    reply[1] = globalBest;
                } else {
                    reply[0] = -1;
                    reply[1] = globalBest;
                    finishedWorkers++;
                }
                MPI_Send(reply, 2, MPI_INT, worker, 2, MPI_COMM_WORLD);
            }
        } else {
            // WORKER
            while (true) {
                int req[2] = {bestProfit, (int)bestClique.size()};
                MPI_Send(req, 2, MPI_INT, 0, 0, MPI_COMM_WORLD);
                if (!bestClique.empty())
                    MPI_Send(bestClique.data(), bestClique.size(), MPI_INT, 0, 1, MPI_COMM_WORLD);

                int reply[2];
                MPI_Recv(reply, 2, MPI_INT, 0, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                int jobIdx = reply[0];
                int masterBest = reply[1];

                if (jobIdx < 0) break;
                if (masterBest > bestProfit) bestProfit = masterBest;

                Job &job = jobs[jobIdx];
                if (job.profit > bestProfit) {
                    bestProfit = job.profit;
                    bestClique = job.clique;
                }
                if (!job.candidates.empty())
                    findClique(job.candidates, job.profit, job.cost, job.clique, g);
            }
        }
    }

    // write output
    if (rank == 0) {
        sort(finalClique.begin(), finalClique.end());
        ofstream fout(outputFile);
        fout << globalBest << "\n";
        for (int i = 0; i < (int)finalClique.size(); i++) {
            if (i > 0) fout << " ";
            fout << finalClique[i];
        }
        fout << "\n";
    }

    double endTime = MPI_Wtime();
    if (rank == 0)
        cout << "Execution time: " << (endTime - startTime) << " seconds" << endl;

    MPI_Finalize();
    return 0;
}
