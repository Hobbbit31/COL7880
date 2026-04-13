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
//job struct for B&B tree nodes - contains current clique, candidates, profit, and cost
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

    vector<int> finalClique;
    int globalBest = 0;

    {
        // ROUND-ROBIN: every rank processes jobs where i % numProcs == rank
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
