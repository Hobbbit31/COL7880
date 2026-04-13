// main.cpp
// MPI Parallel Branch and Bound for Budgeted Maximum Weight Clique Problem
//
// How it works:
//   1. Rank 0 reads the graph from file and broadcasts it to all processes
//   2. All processes generate a list of sub-problems (jobs) by expanding the B&B tree
//   3. Jobs are distributed among processes in round-robin fashion
//   4. Each process solves its assigned jobs using branch and bound
//   5. Processes periodically share the best solution found (for better pruning)
//   6. At the end, the best solution is collected and written to output file
//
// Compile: mpic++ -O3 -std=c++17 main.cpp -o main
// Run:     mpirun -np <num_processes> ./main <input_file> <output_file>

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

// A Job represents a sub-problem in the branch and bound tree
// It stores a partial clique and the remaining candidates to explore
struct Job {
    vector<int> clique;       // vertices already in the clique
    vector<int> candidates;   // vertices that can still be added
    int profit;               // total profit of clique so far
    int cost;                 // total cost of clique so far
};

// generateJobs - expand the B&B tree to a certain depth to create sub-problems
// This way we get multiple independent jobs that can be solved in parallel
void generateJobs(const vector<int> &candidates, const vector<int> &clique,
                  int profit, int cost, int depth, int maxDepth,
                  const Graph &g, vector<Job> &jobs) {

    // if we reached the max depth or no candidates left, save this as a job
    if (depth >= maxDepth || candidates.empty()) {
        if (!candidates.empty() || !clique.empty()) {
            Job job;
            job.clique = clique;
            job.candidates = candidates;
            job.profit = profit;
            job.cost = cost;
            jobs.push_back(job);
        }
        return;
    }

    // try each candidate vertex
    int jobsBefore = jobs.size();
    vector<int> cands = candidates;

    while (!cands.empty()) {
        int v = cands.back();
        cands.pop_back();

        // only consider vertex if it fits in the budget
        if (cost + g.cost[v] <= g.B) {
            // find candidates for the next level (neighbors of v in current candidates)
            vector<int> nextCands;
            for (int i = 0; i < (int)cands.size(); i++) {
                if (g.isAdj(v, cands[i])) {
                    nextCands.push_back(cands[i]);
                }
            }

            // add v to clique and go deeper
            vector<int> newClique = clique;
            newClique.push_back(v);
            generateJobs(nextCands, newClique, profit + g.profit[v],
                        cost + g.cost[v], depth + 1, maxDepth, g, jobs);
        }
    }

    // if no jobs were created (all candidates over budget), save the current clique
    if ((int)jobs.size() == jobsBefore && !clique.empty()) {
        Job job;
        job.clique = clique;
        job.candidates = {};
        job.profit = profit;
        job.cost = cost;
        jobs.push_back(job);
    }
}

// chooseExpandDepth - decide how deep to expand the tree to get enough jobs
// We want at least 8 jobs per process for good load balancing
int chooseExpandDepth(const vector<int> &allVertices, int numProcs, const Graph &g) {
    if (numProcs <= 1) return 1;

    int target = 8 * numProcs;

    // try increasing depths until we get enough jobs
    for (int depth = 1; depth <= 4; depth++) {
        vector<Job> testJobs;
        generateJobs(allVertices, {}, 0, 0, 0, depth, g, testJobs);
        if ((int)testJobs.size() >= target || depth == 4) {
            return depth;
        }
    }
    return 1;
}

// broadcastGraph - send the graph from rank 0 to all other processes using MPI
void broadcastGraph(Graph &g, int rank) {
    // broadcast N, E, B to all processes
    MPI_Bcast(&g.N, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&g.E, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&g.B, 1, MPI_INT, 0, MPI_COMM_WORLD);

    // non-root processes need to allocate memory first
    if (rank != 0) {
        g.allocate();
    }

    // broadcast profit and cost arrays
    MPI_Bcast(g.profit.data(), g.N, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(g.cost.data(), g.N, MPI_INT, 0, MPI_COMM_WORLD);

    // broadcast adjacency matrix
    // we do it in chunks because MPI_Bcast has a limit on message size
    long long totalSize = (long long)g.N * g.N;
    long long offset = 0;
    while (offset < totalSize) {
        int chunkSize = min((long long)100000000, totalSize - offset);
        MPI_Bcast(g.adj.data() + offset, chunkSize, MPI_CHAR, 0, MPI_COMM_WORLD);
        offset += chunkSize;
    }
}

int main(int argc, char *argv[]) {
    // initialize MPI
    MPI_Init(&argc, &argv);

    double startTime = MPI_Wtime();
    int rank, numProcs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);     // get my process id
    MPI_Comm_size(MPI_COMM_WORLD, &numProcs); // get total number of processes

    // check command line arguments
    if (argc < 3) {
        if (rank == 0) {
            cout << "Usage: mpirun -np <P> ./main <input_file> <output_file>" << endl;
        }
        MPI_Finalize();
        return 1;
    }

    string inputFile = argv[1];
    string outputFile = argv[2];
    Graph g;

    // Step 1: Rank 0 reads the graph from file
    int readOk = 1;
    if (rank == 0) {
        if (!g.readFromFile(inputFile)) {
            readOk = 0;
        }
    }

    // tell all processes if reading was successful
    MPI_Bcast(&readOk, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (!readOk) {
        MPI_Finalize();
        return 1;
    }

    // Step 2: Send graph to all processes
    broadcastGraph(g, rank);

    // handle empty graph
    if (g.N == 0) {
        if (rank == 0) {
            ofstream fout(outputFile);
            fout << 0 << "\n\n";
        }
        MPI_Finalize();
        return 0;
    }

    // Step 3: Create list of all vertices sorted by profit (ascending)
    // We sort ascending so that when we pop from back, we get high profit first
    vector<int> allVertices(g.N);
    for (int i = 0; i < g.N; i++) {
        allVertices[i] = i;
    }
    sort(allVertices.begin(), allVertices.end(), [&](int a, int b) {
        return g.profit[a] < g.profit[b];
    });

    // Step 4: Generate sub-problems (jobs) for parallel processing
    int expandDepth = chooseExpandDepth(allVertices, numProcs, g);
    vector<Job> jobs;
    generateJobs(allVertices, {}, 0, 0, 0, expandDepth, g, jobs);

    // Step 5: Each process solves its assigned jobs
    bestProfit = 0;
    bestClique.clear();

    for (int i = 0; i < (int)jobs.size(); i++) {
        // round-robin: process 0 gets job 0,4,8,...  process 1 gets job 1,5,9,... etc.
        if (i % numProcs == rank) {
            Job &job = jobs[i];

            // the partial clique itself might be a valid solution
            if (job.profit > bestProfit) {
                bestProfit = job.profit;
                bestClique = job.clique;
            }

            // run branch and bound on the remaining candidates
            if (!job.candidates.empty()) {
                findClique(job.candidates, job.profit, job.cost, job.clique, g);
            }
        }

        // periodically share the best profit across all processes
        // this helps other processes prune their search trees better
        if ((i + 1) % numProcs == 0 || i == (int)jobs.size() - 1) {
            int globalBest = 0;
            MPI_Allreduce(&bestProfit, &globalBest, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
            if (globalBest > bestProfit) {
                bestProfit = globalBest;
            }
        }
    }

    // Step 6: Find which process has the best solution
    int globalBest = 0;
    MPI_Allreduce(&bestProfit, &globalBest, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);

    // calculate the actual profit of my best clique
    int myCliqueProfit = 0;
    for (int i = 0; i < (int)bestClique.size(); i++) {
        myCliqueProfit += g.profit[bestClique[i]];
    }

    // find the rank that has the winning clique (lowest rank if tie)
    int myCandidate = (myCliqueProfit == globalBest && !bestClique.empty()) ? rank : numProcs;
    int winner = numProcs;
    MPI_Allreduce(&myCandidate, &winner, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
    if (winner >= numProcs) winner = 0;

    // Step 7: Send the best clique to rank 0
    vector<int> finalClique;

    if (winner == 0) {
        // rank 0 already has it
        if (rank == 0) {
            finalClique = bestClique;
        }
    } else {
        // winner sends clique to rank 0
        if (rank == winner) {
            int sz = bestClique.size();
            MPI_Send(&sz, 1, MPI_INT, 0, 0, MPI_COMM_WORLD);
            if (sz > 0) {
                MPI_Send(bestClique.data(), sz, MPI_INT, 0, 1, MPI_COMM_WORLD);
            }
        } else if (rank == 0) {
            int sz = 0;
            MPI_Recv(&sz, 1, MPI_INT, winner, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            if (sz > 0) {
                finalClique.resize(sz);
                MPI_Recv(finalClique.data(), sz, MPI_INT, winner, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            }
        }
    }

    // Step 8: Write the result to output file
    if (rank == 0) {
        sort(finalClique.begin(), finalClique.end());
        ofstream fout(outputFile);
        fout << globalBest << "\n";
        for (int i = 0; i < (int)finalClique.size(); i++) {
            if (i > 0) fout << " ";
            fout << finalClique[i];
        }
        fout << "\n";
        fout.close();
    }

    // print execution time
    double endTime = MPI_Wtime();
    if (rank == 0) {
        cout << "Execution time: " << (endTime - startTime) << " seconds" << endl;
    }

    // clean up MPI
    MPI_Finalize();
    return 0;
}
