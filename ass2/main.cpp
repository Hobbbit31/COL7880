// ============================================================
// Local Histogram Equalization on 3D Point Clouds (OpenMP)
//
// Three approaches:
//   1. Exact KNN    → knn.txt
//   2. Approx KNN   → approx_knn.txt  (random projection)
//   3. K-Means      → kmeans.txt
//
// Usage: ./a2 input.txt knn|approx_knn|kmeans
// ============================================================

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <climits>
#include <algorithm>
#include <vector>
#include <chrono>
#include <omp.h>

// ============================================================
// Data structure: each point has 3D integer coords + intensity
// ============================================================
struct Point {
    int x, y, z;
    int intensity;
};

// ============================================================
// Squared Euclidean distance (long long to avoid overflow)
// ============================================================
inline long long dist_sq(const Point& a, const Point& b) {
    long long dx = (long long)a.x - b.x;
    long long dy = (long long)a.y - b.y;
    long long dz = (long long)a.z - b.z;
    return dx*dx + dy*dy + dz*dz;
}

// ============================================================
// Lexicographic comparison for tie-breaking (x, then y, then z)
// ============================================================
inline bool lex_less(const Point& a, const Point& b) {
    if (a.x != b.x) return a.x < b.x;
    if (a.y != b.y) return a.y < b.y;
    return a.z < b.z;
}

// ============================================================
// Is candidate (dist_a, idx_a) worse than (dist_b, idx_b)?
// Worse = farther away, or same distance but lexically larger
// ============================================================
inline bool is_worse(long long dist_a, int idx_a, long long dist_b, int idx_b, const Point* points) {
    if (dist_a != dist_b) return dist_a > dist_b;
    if (points[idx_a].x != points[idx_b].x) return points[idx_a].x > points[idx_b].x;
    if (points[idx_a].y != points[idx_b].y) return points[idx_a].y > points[idx_b].y;
    return points[idx_a].z > points[idx_b].z;
}

// ============================================================
// Histogram Equalization for one point
//
// Given a set of neighbor indices (including self), remap intensity:
//   1. histogram[v] = count of neighbors with intensity v
//   2. cdf[v] = prefix sum of histogram = count with intensity <= v
//   3. cdf_min = smallest non-zero cdf value
//   4. I' = floor((cdf[I] - cdf_min) / (m - cdf_min) * 255)
//   5. If m == cdf_min → keep original intensity
// ============================================================
int equalize(int point_idx, const int* neighbor_indices, int m, const Point* points) {
    // Build histogram
    int hist[256] = {0};
    for (int j = 0; j < m; j++) {
        hist[points[neighbor_indices[j]].intensity]++;
    }

    // CDF = prefix sum
    int cdf[256];
    cdf[0] = hist[0];
    for (int v = 1; v < 256; v++) {
        cdf[v] = cdf[v-1] + hist[v];
    }

    // Find cdf_min
    int cdf_min = 0;
    for (int v = 0; v < 256; v++) {
        if (cdf[v] > 0) { cdf_min = cdf[v]; break; }
    }

    // Remap
    int I_i = points[point_idx].intensity;
    if (m == cdf_min) return I_i;

    int new_intensity = (int)floor((double)(cdf[I_i] - cdf_min) / (double)(m - cdf_min) * 255.0);
    if (new_intensity < 0) new_intensity = 0;
    if (new_intensity > 255) new_intensity = 255;
    return new_intensity;
}

// ============================================================
//                      1. EXACT KNN
//
// For each point:
//   - Compute distance to every other point
//   - Keep the k closest (using simple array, find-worst-and-replace)
//   - Neighborhood = self + k neighbors → m = k+1
// ============================================================
void knn_exact(const Point* points, int n, int k, int* new_intensities) {
    #pragma omp parallel for schedule(guided)
    for (int i = 0; i < n; i++) {
        // Arrays to store k best neighbors
        long long* best_dists = new long long[k];
        int*       best_idx   = new int[k];
        int        found      = 0;

        // Check every other point
        for (int j = 0; j < n; j++) {
            if (j == i) continue;

            long long d = dist_sq(points[i], points[j]);

            if (found < k) {
                // Still filling up → just add
                best_dists[found] = d;
                best_idx[found]   = j;
                found++;
            } else {
                // Find the worst in current top-k
                int worst = 0;
                for (int w = 1; w < k; w++) {
                    if (is_worse(best_dists[w], best_idx[w],
                                 best_dists[worst], best_idx[worst], points)) {
                        worst = w;
                    }
                }
                // Replace worst if j is better
                if (is_worse(best_dists[worst], best_idx[worst], d, j, points)) {
                    best_dists[worst] = d;
                    best_idx[worst]   = j;
                }
            }
        }

        // Neighborhood = self + k neighbors
        int* neighbors = new int[k + 1];
        neighbors[0] = i;
        for (int j = 0; j < k; j++) {
            neighbors[j + 1] = best_idx[j];
        }

        new_intensities[i] = equalize(i, neighbors, k + 1, points);

        delete[] best_dists;
        delete[] best_idx;
        delete[] neighbors;
    }
}

// ============================================================
//                   2. APPROXIMATE KNN
//
// Random Projection approach:
//   - Pick a random direction vector
//   - Project all points onto it → 1D values
//   - Sort by projection → nearby 3D points tend to be nearby in 1D
//   - For each point, check a window of neighbors in sorted order
//   - Repeat with multiple directions, keep best k across all
//
// Parameters:
//   NUM_PROJECTIONS = how many random directions (more = more accurate)
//   WINDOW_SIZE     = how many sorted neighbors to check (more = more accurate)
// ============================================================

// Try to insert candidate j into point i's top-k
void try_insert(int j, long long d, int k,
                long long* best_dists, int* best_idx, int& found,
                const Point* points) {
    // Skip duplicates
    for (int w = 0; w < found; w++) {
        if (best_idx[w] == j) return;
    }

    if (found < k) {
        best_dists[found] = d;
        best_idx[found] = j;
        found++;
    } else {
        int worst = 0;
        for (int w = 1; w < k; w++) {
            if (is_worse(best_dists[w], best_idx[w], best_dists[worst], best_idx[worst], points)) {
                worst = w;
            }
        }
        if (is_worse(best_dists[worst], best_idx[worst], d, j, points)) {
            best_dists[worst] = d;
            best_idx[worst] = j;
        }
    }
}

void approx_knn(const Point* points, int n, int k, int* new_intensities) {
    const int NUM_PROJECTIONS = 30;
    const int WINDOW_SIZE = 5 * k;

    // Generate random direction vectors
    double dirs[30][3];  // max 30 projections
    srand(12345);
    for (int p = 0; p < NUM_PROJECTIONS; p++) {
        dirs[p][0] = (double)rand() / RAND_MAX - 0.5;
        dirs[p][1] = (double)rand() / RAND_MAX - 0.5;
        dirs[p][2] = (double)rand() / RAND_MAX - 0.5;
        double len = sqrt(dirs[p][0]*dirs[p][0] + dirs[p][1]*dirs[p][1] + dirs[p][2]*dirs[p][2]);
        dirs[p][0] /= len;
        dirs[p][1] /= len;
        dirs[p][2] /= len;
    }

    // Per-point top-k storage
    long long* best_dists = new long long[n * k];
    int*       best_idx   = new int[n * k];
    int*       found      = new int[n];
    for (int i = 0; i < n; i++) found[i] = 0;

    // Process each random projection
    for (int p = 0; p < NUM_PROJECTIONS; p++) {
        double rx = dirs[p][0], ry = dirs[p][1], rz = dirs[p][2];

        // Project all points onto this direction
        double* proj = new double[n];
        int* order = new int[n];
        #pragma omp parallel for schedule(guided)
        for (int i = 0; i < n; i++) {
            proj[i] = points[i].x * rx + points[i].y * ry + points[i].z * rz;
            order[i] = i;
        }

        // Sort indices by projection value
        std::sort(order, order + n, [&](int a, int b) {
            return proj[a] < proj[b];
        });

        // Reverse map: pos[i] = position of point i in sorted order
        int* pos = new int[n];
        for (int s = 0; s < n; s++) {
            pos[order[s]] = s;
        }

        // For each point, check nearby points in sorted order
        #pragma omp parallel for schedule(static)
        for (int i = 0; i < n; i++) {
            int my_pos = pos[i];
            int left  = (my_pos - WINDOW_SIZE > 0) ? my_pos - WINDOW_SIZE : 0;
            int right = (my_pos + WINDOW_SIZE < n) ? my_pos + WINDOW_SIZE : n;

            for (int s = left; s < right; s++) {
                int j = order[s];
                if (j == i) continue;
                long long d = dist_sq(points[i], points[j]);
                try_insert(j, d, k,
                           &best_dists[i * k], &best_idx[i * k], found[i],
                           points);
            }
        }

        delete[] proj;
        delete[] order;
        delete[] pos;
    }

    // Equalize each point
    #pragma omp parallel for schedule(guided)
    for (int i = 0; i < n; i++) {
        int num = found[i];
        int* neighbors = new int[num + 1];
        neighbors[0] = i;
        for (int j = 0; j < num; j++) {
            neighbors[j + 1] = best_idx[i * k + j];
        }
        new_intensities[i] = equalize(i, neighbors, num + 1, points);
        delete[] neighbors;
    }

    delete[] best_dists;
    delete[] best_idx;
    delete[] found;
}

// ============================================================
//                     3. K-MEANS
//
// Step 1: Initialize centroids = first k points
// Step 2: Repeat up to T times:
//         - Assign each point to nearest centroid
//         - Recompute centroids (integer division)
//         - Stop if no assignments changed
// Step 3: Each cluster = a neighborhood
// Step 4: Equalize each point using its cluster, m = cluster size
// ============================================================
void kmeans_method(const Point* points, int n, int k, int T, int* new_intensities) {
    // Initialize centroids
    int* cx = new int[k];
    int* cy = new int[k];
    int* cz = new int[k];
    for (int i = 0; i < k; i++) {
        cx[i] = points[i].x;
        cy[i] = points[i].y;
        cz[i] = points[i].z;
    }

    int* assignment = new int[n];
    for (int i = 0; i < n; i++) assignment[i] = 0;

    // Iterate
    for (int iter = 0; iter < T; iter++) {
        bool changed = false;

        // Assign each point to nearest centroid
        #pragma omp parallel for schedule(static) reduction(||:changed)
        for (int i = 0; i < n; i++) {
            long long best_dist = LLONG_MAX;
            int best_cluster = 0;

            for (int c = 0; c < k; c++) {
                long long dx = (long long)points[i].x - cx[c];
                long long dy = (long long)points[i].y - cy[c];
                long long dz = (long long)points[i].z - cz[c];
                long long d = dx*dx + dy*dy + dz*dz;

                if (d < best_dist) {
                    best_dist = d;
                    best_cluster = c;
                } else if (d == best_dist) {
                    // Tie-break: lexically smaller centroid
                    if (cx[c] < cx[best_cluster] ||
                        (cx[c] == cx[best_cluster] && cy[c] < cy[best_cluster]) ||
                        (cx[c] == cx[best_cluster] && cy[c] == cy[best_cluster] && cz[c] < cz[best_cluster])) {
                        best_cluster = c;
                    }
                }
            }

            if (assignment[i] != best_cluster) {
                assignment[i] = best_cluster;
                changed = true;
            }
        }

        if (!changed) break;

        // Recompute centroids using integer division
        long long* sx = new long long[k]();
        long long* sy = new long long[k]();
        long long* sz = new long long[k]();
        int* count = new int[k]();

        for (int i = 0; i < n; i++) {
            int c = assignment[i];
            sx[c] += points[i].x;
            sy[c] += points[i].y;
            sz[c] += points[i].z;
            count[c]++;
        }

        for (int c = 0; c < k; c++) {
            if (count[c] > 0) {
                cx[c] = (int)(sx[c] / count[c]);
                cy[c] = (int)(sy[c] / count[c]);
                cz[c] = (int)(sz[c] / count[c]);
            }
        }

        delete[] sx; delete[] sy; delete[] sz; delete[] count;
    }

    // Build cluster member lists
    std::vector<std::vector<int>> clusters(k);
    for (int i = 0; i < n; i++) {
        clusters[assignment[i]].push_back(i);
    }

    // Equalize each point using its cluster
    #pragma omp parallel for schedule(guided)
    for (int i = 0; i < n; i++) {
        int c = assignment[i];
        new_intensities[i] = equalize(i, clusters[c].data(), (int)clusters[c].size(), points);
    }

    delete[] cx; delete[] cy; delete[] cz; delete[] assignment;
}

// ============================================================
// Write output
// ============================================================
void write_output(const char* filename, const Point* points, const int* new_intensities, int n) {
    FILE* f = fopen(filename, "w");
    if (!f) { perror(filename); exit(1); }
    for (int i = 0; i < n; i++) {
        fprintf(f, "%d %d %d %d\n", points[i].x, points[i].y, points[i].z, new_intensities[i]);
    }
    fclose(f);
}

// ============================================================
// Main: ./a2 input.txt knn|approx_knn|kmeans
// ============================================================
int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input_file> knn|approx_knn|kmeans\n", argv[0]);
        return 1;
    }

    const char* method = argv[2];

    // Read input
    FILE* f = fopen(argv[1], "r");
    if (!f) { perror(argv[1]); return 1; }

    int n, k, T;
    if (fscanf(f, "%d", &n) != 1) return 1;
    if (fscanf(f, "%d", &k) != 1) return 1;
    if (fscanf(f, "%d", &T) != 1) return 1;

    Point* points = new Point[n];
    for (int i = 0; i < n; i++) {
        if (fscanf(f, "%d %d %d %d", &points[i].x, &points[i].y, &points[i].z, &points[i].intensity) != 4)
            return 1;
    }
    fclose(f);

    fprintf(stderr, "Loaded %d points, k=%d, T=%d, method=%s\n", n, k, T, method);

    int* new_intensities = new int[n];
    auto t0 = std::chrono::high_resolution_clock::now();

    if (strcmp(method, "knn") == 0) {
        knn_exact(points, n, k, new_intensities);
        write_output("knn.txt", points, new_intensities, n);
    } else if (strcmp(method, "approx_knn") == 0) {
        approx_knn(points, n, k, new_intensities);
        write_output("approx_knn.txt", points, new_intensities, n);
    } else if (strcmp(method, "kmeans") == 0) {
        kmeans_method(points, n, k, T, new_intensities);
        write_output("kmeans.txt", points, new_intensities, n);
    } else {
        fprintf(stderr, "Unknown method: %s\n", method);
        return 1;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    fprintf(stderr, "%s: %.2f ms\n", method, ms);

    delete[] points;
    delete[] new_intensities;
    return 0;
}
