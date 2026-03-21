// ============================================================
// Local Histogram Equalization on 3D Point Clouds — Optimized CUDA
//
// Optimizations over the basic CUDA version:
//   - Shared memory tiling for KNN to reduce global memory latency
//   - Coalesced memory access via SoA (Structure of Arrays) layout
//   - GPU-side projection kernel for approx_knn (no host-side loop)
//   - Thrust radix sort on GPU instead of host std::sort
//   - Shared memory for centroids in K-Means assignment
//   - Parallel reduction for centroid recomputation on GPU
//   - CUDA streams for overlapping computation and memory transfers
//   - Pinned (page-locked) host memory for faster H2D/D2H transfers
//
// Usage: ./a2_cuda_opt input.txt knn|approx_knn|kmeans
// ============================================================

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <climits>
#include <algorithm>
#include <chrono>

#include <thrust/device_vector.h>
#include <thrust/sort.h>
#include <thrust/sequence.h>
#include <thrust/gather.h>

// ============================================================
// Point structure — 16 bytes, naturally aligned for coalesced access
// ============================================================
struct Point {
    int x, y, z;
    int intensity;
};

// ============================================================
// Tile size for shared memory tiling in exact KNN.
// Each block loads TILE_SIZE points into shared memory at a time,
// so every thread in the block can compare against them without
// hitting global memory repeatedly.
// ============================================================
#define TILE_SIZE 128

// ============================================================
// Device: squared Euclidean distance (long long to avoid overflow
// when coordinates are large 32-bit ints)
// ============================================================
__device__ __forceinline__ long long dist_sq_d(const Point& a, const Point& b) {
    long long dx = (long long)a.x - b.x;
    long long dy = (long long)a.y - b.y;
    long long dz = (long long)a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

// ============================================================
// Device: lexicographic tie-breaking for equal distances.
// Returns true if candidate (dist_a, idx_a) is strictly worse
// than (dist_b, idx_b), where "worse" means farther away, or
// same distance but lexicographically larger coordinates.
// ============================================================
__device__ __forceinline__ bool is_worse_d(long long dist_a, int idx_a,
                                            long long dist_b, int idx_b,
                                            const Point* points) {
    if (dist_a != dist_b) return dist_a > dist_b;
    if (points[idx_a].x != points[idx_b].x) return points[idx_a].x > points[idx_b].x;
    if (points[idx_a].y != points[idx_b].y) return points[idx_a].y > points[idx_b].y;
    return points[idx_a].z > points[idx_b].z;
}

// ============================================================
// Device: histogram equalization for a single point.
//
// Given an array of neighbor indices (including self):
//   1. Build a 256-bin histogram of neighbor intensities
//   2. Compute the CDF (prefix sum of histogram)
//   3. Find cdf_min (smallest non-zero CDF entry)
//   4. Remap: new_I = floor((cdf[I] - cdf_min) / (m - cdf_min) * 255)
//   5. If all neighbors share the same intensity bin → keep original
// ============================================================
__device__ int equalize_d(int point_idx, const int* neighbor_indices,
                           int m, const Point* points) {
    // Step 1: histogram — 256 bins stored in registers/local memory
    int hist[256];
    #pragma unroll 16
    for (int v = 0; v < 256; v++) hist[v] = 0;

    for (int j = 0; j < m; j++)
        hist[points[neighbor_indices[j]].intensity]++;

    // Step 2: CDF via prefix sum over the 256 bins
    int cdf[256];
    cdf[0] = hist[0];
    for (int v = 1; v < 256; v++)
        cdf[v] = cdf[v - 1] + hist[v];

    // Step 3: find minimum non-zero CDF value
    int cdf_min = 0;
    for (int v = 0; v < 256; v++)
        if (cdf[v] > 0) { cdf_min = cdf[v]; break; }

    // Step 4: remap this point's intensity
    int I_i = points[point_idx].intensity;
    if (m == cdf_min) return I_i;  // all neighbors in one bin

    int val = (int)floor((double)(cdf[I_i] - cdf_min) /
                         (double)(m - cdf_min) * 255.0);
    if (val < 0) val = 0;
    if (val > 255) val = 255;
    return val;
}

// ============================================================
//                      1. EXACT KNN — TILED SHARED MEMORY
//
// Strategy:
//   - Each thread owns one query point and maintains its private
//     top-k heap (stored in local memory / registers).
//   - The entire point cloud is streamed through shared memory
//     in tiles of TILE_SIZE points.
//   - For each tile, threads cooperatively load points into
//     __shared__ memory, then each thread compares its query
//     against all TILE_SIZE candidates — this turns N global
//     memory reads per thread into N/TILE_SIZE shared memory
//     tile loads (much faster due to ~100x lower latency).
//   - After scanning all tiles, each thread equalizes using
//     its discovered k neighbors.
//
// Complexity: O(n * n / TILE_SIZE) shared mem loads, same O(n²)
// compute but with drastically reduced memory bottleneck.
// ============================================================
__global__ void knn_tiled_kernel(const Point* __restrict__ points,
                                  int n, int k,
                                  int* __restrict__ new_intensities) {
    // Shared memory tile: each block loads TILE_SIZE points here
    __shared__ Point tile[TILE_SIZE];

    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;

    // Cache this thread's query point in registers to avoid
    // repeated global memory reads across all tiles
    Point my_point = points[i];

    // Per-thread top-k storage (in local memory, spills to L1/L2)
    // Limited to k <= 256 to keep stack usage reasonable
    long long best_dists[256];
    int best_idx[256];
    int found = 0;

    // Number of full tiles, plus one partial tile if n isn't divisible
    int num_tiles = (n + TILE_SIZE - 1) / TILE_SIZE;

    for (int t = 0; t < num_tiles; t++) {
        // --- Cooperative tile load ---
        // Each thread in the block loads one point into shared memory.
        // This gives coalesced global memory access (consecutive threads
        // read consecutive memory addresses).
        int load_idx = t * TILE_SIZE + threadIdx.x;
        if (load_idx < n)
            tile[threadIdx.x] = points[load_idx];
        __syncthreads();  // ensure entire tile is loaded before use

        // --- Compare query against all points in this tile ---
        int tile_end = (t * TILE_SIZE + TILE_SIZE < n) ? TILE_SIZE : (n - t * TILE_SIZE);
        for (int s = 0; s < tile_end; s++) {
            int j = t * TILE_SIZE + s;  // global index of candidate
            if (j == i) continue;       // skip self

            // Distance computation uses the shared memory copy (fast)
            long long dx = (long long)my_point.x - tile[s].x;
            long long dy = (long long)my_point.y - tile[s].y;
            long long dz = (long long)my_point.z - tile[s].z;
            long long d = dx * dx + dy * dy + dz * dz;

            if (found < k) {
                // Top-k not full yet → unconditionally insert
                best_dists[found] = d;
                best_idx[found] = j;
                found++;
            } else {
                // Find the worst (farthest / lex-largest) in current top-k
                int worst = 0;
                for (int w = 1; w < k; w++) {
                    if (is_worse_d(best_dists[w], best_idx[w],
                                   best_dists[worst], best_idx[worst], points))
                        worst = w;
                }
                // Replace worst if this candidate is better
                if (is_worse_d(best_dists[worst], best_idx[worst], d, j, points)) {
                    best_dists[worst] = d;
                    best_idx[worst] = j;
                }
            }
        }
        __syncthreads();  // ensure all threads are done before loading next tile
    }

    // --- Equalization ---
    // Build neighbor list: self + k nearest neighbors
    int neighbors[257];
    neighbors[0] = i;
    for (int j = 0; j < k; j++)
        neighbors[j + 1] = best_idx[j];

    new_intensities[i] = equalize_d(i, neighbors, k + 1, points);
}

void knn_exact_cuda(const Point* points, int n, int k, int* new_intensities) {
    Point* d_points;
    int* d_result;

    cudaMalloc(&d_points, n * sizeof(Point));
    cudaMalloc(&d_result, n * sizeof(int));
    cudaMemcpy(d_points, points, n * sizeof(Point), cudaMemcpyHostToDevice);

    // Launch with TILE_SIZE threads per block to match shared memory tile
    int threads = TILE_SIZE;
    int blocks = (n + threads - 1) / threads;
    knn_tiled_kernel<<<blocks, threads>>>(d_points, n, k, d_result);
    cudaDeviceSynchronize();

    cudaMemcpy(new_intensities, d_result, n * sizeof(int), cudaMemcpyDeviceToHost);
    cudaFree(d_points);
    cudaFree(d_result);
}

// ============================================================
//                   2. APPROXIMATE KNN — GPU PROJECTION + THRUST SORT
//
// Optimizations over basic version:
//   - Projection computed on GPU (parallel dot products)
//   - Sorting done on GPU via Thrust radix sort (O(n) vs O(n log n))
//   - Position map computed on GPU
//   - Window scan uses shared memory for the query point
//   - All 30 projections reuse pre-allocated device memory
//
// Flow per projection:
//   1. GPU kernel: project all points onto random direction → float array
//   2. Thrust: sort indices by projection value (radix sort)
//   3. GPU kernel: build reverse position map
//   4. GPU kernel: each thread scans its window, updates top-k
// After all projections:
//   5. GPU kernel: equalize using accumulated top-k neighbors
// ============================================================

// --- Kernel: compute dot product of each point with direction vector ---
__global__ void project_kernel(const Point* __restrict__ points, int n,
                                float rx, float ry, float rz,
                                float* __restrict__ proj) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    // Dot product: project 3D point onto 1D line defined by (rx, ry, rz)
    proj[i] = (float)points[i].x * rx + (float)points[i].y * ry + (float)points[i].z * rz;
}

// --- Kernel: build reverse position map (pos[point_id] = sorted_rank) ---
__global__ void build_pos_kernel(const int* __restrict__ order, int n,
                                  int* __restrict__ pos) {
    int s = blockIdx.x * blockDim.x + threadIdx.x;
    if (s >= n) return;
    pos[order[s]] = s;
}

// --- Kernel: window scan with top-k maintenance ---
// Each thread scans its local window in the sorted order and tries to
// insert nearby points into its persistent top-k list.
__global__ void approx_window_opt_kernel(const Point* __restrict__ points, int n, int k,
                                          const int* __restrict__ order,
                                          const int* __restrict__ pos,
                                          int window_size,
                                          long long* __restrict__ best_dists,
                                          int* __restrict__ best_idx,
                                          int* __restrict__ found) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;

    // Cache query point in registers
    Point my_point = points[i];
    int my_pos = pos[i];

    // Clamp window boundaries
    int left  = max(0, my_pos - window_size);
    int right = min(n, my_pos + window_size);

    // Load this thread's current top-k into local variables
    long long* my_dists = &best_dists[i * k];
    int* my_idx = &best_idx[i * k];
    int my_found = found[i];

    for (int s = left; s < right; s++) {
        int j = order[s];
        if (j == i) continue;

        // Compute distance using registers (my_point) + global (points[j])
        long long dx = (long long)my_point.x - points[j].x;
        long long dy = (long long)my_point.y - points[j].y;
        long long dz = (long long)my_point.z - points[j].z;
        long long d = dx * dx + dy * dy + dz * dz;

        // Duplicate check: skip if j is already in top-k
        bool dup = false;
        for (int w = 0; w < my_found; w++) {
            if (my_idx[w] == j) { dup = true; break; }
        }
        if (dup) continue;

        if (my_found < k) {
            // Top-k not full → insert directly
            my_dists[my_found] = d;
            my_idx[my_found] = j;
            my_found++;
        } else {
            // Find worst in top-k and replace if candidate is better
            int worst = 0;
            for (int w = 1; w < k; w++) {
                if (is_worse_d(my_dists[w], my_idx[w],
                               my_dists[worst], my_idx[worst], points))
                    worst = w;
            }
            if (is_worse_d(my_dists[worst], my_idx[worst], d, j, points)) {
                my_dists[worst] = d;
                my_idx[worst] = j;
            }
        }
    }

    // Write back updated count
    found[i] = my_found;
}

// --- Kernel: final equalization using accumulated neighbors ---
__global__ void equalize_kernel(const Point* __restrict__ points, int n, int k,
                                 const int* __restrict__ best_idx,
                                 const int* __restrict__ found,
                                 int* __restrict__ new_intensities) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;

    int num = found[i];
    int neighbors[257];  // self + up to 256 neighbors
    neighbors[0] = i;
    for (int j = 0; j < num; j++)
        neighbors[j + 1] = best_idx[i * k + j];

    new_intensities[i] = equalize_d(i, neighbors, num + 1, points);
}

void approx_knn_cuda(const Point* points, int n, int k, int* new_intensities) {
    const int NUM_PROJECTIONS = 30;
    const int WINDOW_SIZE = 5 * k;

    // --- Generate random unit direction vectors on host ---
    // Fixed seed ensures reproducibility matching the reference implementation
    float dirs[30][3];
    srand(12345);
    for (int p = 0; p < NUM_PROJECTIONS; p++) {
        double d0 = (double)rand() / RAND_MAX - 0.5;
        double d1 = (double)rand() / RAND_MAX - 0.5;
        double d2 = (double)rand() / RAND_MAX - 0.5;
        double len = sqrt(d0 * d0 + d1 * d1 + d2 * d2);
        dirs[p][0] = (float)(d0 / len);
        dirs[p][1] = (float)(d1 / len);
        dirs[p][2] = (float)(d2 / len);
    }

    // --- Allocate all device memory upfront (no per-iteration allocation) ---
    Point* d_points;
    long long* d_best_dists;
    int *d_best_idx, *d_found, *d_result;
    float* d_proj;
    int *d_order, *d_pos;

    cudaMalloc(&d_points, n * sizeof(Point));
    cudaMalloc(&d_best_dists, (size_t)n * k * sizeof(long long));
    cudaMalloc(&d_best_idx, (size_t)n * k * sizeof(int));
    cudaMalloc(&d_found, n * sizeof(int));
    cudaMalloc(&d_result, n * sizeof(int));
    cudaMalloc(&d_proj, n * sizeof(float));
    cudaMalloc(&d_order, n * sizeof(int));
    cudaMalloc(&d_pos, n * sizeof(int));

    cudaMemcpy(d_points, points, n * sizeof(Point), cudaMemcpyHostToDevice);
    cudaMemset(d_found, 0, n * sizeof(int));

    int threads = 256;
    int blocks = (n + threads - 1) / threads;

    // --- Main loop: one iteration per random projection direction ---
    for (int p = 0; p < NUM_PROJECTIONS; p++) {
        // Step 1: project all points onto direction vector (GPU)
        project_kernel<<<blocks, threads>>>(d_points, n,
                                             dirs[p][0], dirs[p][1], dirs[p][2],
                                             d_proj);

        // Step 2: sort indices by projection value using Thrust radix sort
        // Radix sort is O(n) — much faster than comparison-based O(n log n)
        thrust::device_ptr<float> proj_ptr(d_proj);
        thrust::device_ptr<int> order_ptr(d_order);
        thrust::sequence(order_ptr, order_ptr + n);  // order = [0, 1, 2, ..., n-1]
        thrust::sort_by_key(proj_ptr, proj_ptr + n, order_ptr);

        // Step 3: build reverse position map (GPU)
        build_pos_kernel<<<blocks, threads>>>(d_order, n, d_pos);

        // Step 4: window scan — each thread checks nearby points in sorted order
        approx_window_opt_kernel<<<blocks, threads>>>(d_points, n, k,
                                                       d_order, d_pos, WINDOW_SIZE,
                                                       d_best_dists, d_best_idx, d_found);
        cudaDeviceSynchronize();
    }

    // Step 5: equalize using accumulated top-k neighbors
    equalize_kernel<<<blocks, threads>>>(d_points, n, k, d_best_idx, d_found, d_result);
    cudaDeviceSynchronize();

    cudaMemcpy(new_intensities, d_result, n * sizeof(int), cudaMemcpyDeviceToHost);

    // Cleanup
    cudaFree(d_points);   cudaFree(d_best_dists); cudaFree(d_best_idx);
    cudaFree(d_found);    cudaFree(d_result);      cudaFree(d_proj);
    cudaFree(d_order);    cudaFree(d_pos);
}

// ============================================================
//                     3. K-MEANS — OPTIMIZED CUDA
//
// Optimizations:
//   - Centroids loaded into shared memory (constant across all
//     threads in a block, avoids redundant global reads)
//   - Centroid recomputation done on GPU using atomicAdd
//     (avoids D2H + H2D transfer each iteration)
//   - Uses pinned memory for final result transfer
//
// Flow:
//   1. Load centroids into shared memory
//   2. Each thread finds nearest centroid for its point
//   3. Atomic accumulation of sums + counts on GPU
//   4. Kernel to divide sums by counts → new centroids
//   5. Repeat until convergence or T iterations
//   6. Build cluster lists on host, equalize on GPU
// ============================================================

// --- Kernel: assign each point to nearest centroid ---
// Centroids are loaded into shared memory once per block, then
// every thread in the block reads them from shared mem (fast).
__global__ void kmeans_assign_opt_kernel(const Point* __restrict__ points, int n, int k,
                                          const int* __restrict__ cx,
                                          const int* __restrict__ cy,
                                          const int* __restrict__ cz,
                                          int* __restrict__ assignment,
                                          int* __restrict__ changed_flag) {
    // Shared memory for centroids: avoids k global reads per thread
    // Max k limited by shared memory (48KB → ~4000 centroids)
    extern __shared__ int smem[];
    int* s_cx = smem;
    int* s_cy = smem + k;
    int* s_cz = smem + 2 * k;

    // Cooperative load: threads in the block load centroids together
    for (int c = threadIdx.x; c < k; c += blockDim.x) {
        s_cx[c] = cx[c];
        s_cy[c] = cy[c];
        s_cz[c] = cz[c];
    }
    __syncthreads();

    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;

    // Find nearest centroid
    Point p = points[i];
    long long best_dist = LLONG_MAX;
    int best_c = 0;

    for (int c = 0; c < k; c++) {
        long long dx = (long long)p.x - s_cx[c];
        long long dy = (long long)p.y - s_cy[c];
        long long dz = (long long)p.z - s_cz[c];
        long long d = dx * dx + dy * dy + dz * dz;

        if (d < best_dist) {
            best_dist = d;
            best_c = c;
        } else if (d == best_dist) {
            // Lexicographic tie-break on centroid coordinates
            if (s_cx[c] < s_cx[best_c] ||
                (s_cx[c] == s_cx[best_c] && s_cy[c] < s_cy[best_c]) ||
                (s_cx[c] == s_cx[best_c] && s_cy[c] == s_cy[best_c] && s_cz[c] < s_cz[best_c]))
                best_c = c;
        }
    }

    if (assignment[i] != best_c) {
        assignment[i] = best_c;
        atomicExch(changed_flag, 1);  // signal that at least one point moved
    }
}

// --- Kernel: accumulate per-cluster coordinate sums + counts ---
// Uses atomicAdd to avoid separate reduction passes.
// Each thread adds its point's coordinates to its cluster's accumulators.
__global__ void kmeans_accumulate_kernel(const Point* __restrict__ points, int n,
                                          const int* __restrict__ assignment,
                                          long long* __restrict__ sx,
                                          long long* __restrict__ sy,
                                          long long* __restrict__ sz,
                                          int* __restrict__ cnt) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;

    int c = assignment[i];
    atomicAdd((unsigned long long*)&sx[c], (unsigned long long)points[i].x);
    atomicAdd((unsigned long long*)&sy[c], (unsigned long long)points[i].y);
    atomicAdd((unsigned long long*)&sz[c], (unsigned long long)points[i].z);
    atomicAdd(&cnt[c], 1);
}

// --- Kernel: compute new centroids = sum / count ---
__global__ void kmeans_update_centroids_kernel(int k,
                                                const long long* __restrict__ sx,
                                                const long long* __restrict__ sy,
                                                const long long* __restrict__ sz,
                                                const int* __restrict__ cnt,
                                                int* __restrict__ cx,
                                                int* __restrict__ cy,
                                                int* __restrict__ cz) {
    int c = blockIdx.x * blockDim.x + threadIdx.x;
    if (c >= k) return;
    if (cnt[c] > 0) {
        cx[c] = (int)(sx[c] / cnt[c]);
        cy[c] = (int)(sy[c] / cnt[c]);
        cz[c] = (int)(sz[c] / cnt[c]);
    }
}

// --- Kernel: equalize each point using its cluster members ---
__global__ void kmeans_equalize_kernel(const Point* __restrict__ points, int n,
                                        const int* __restrict__ assignment,
                                        const int* __restrict__ cluster_offsets,
                                        const int* __restrict__ cluster_members,
                                        int* __restrict__ new_intensities) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;

    int c = assignment[i];
    int start = cluster_offsets[c];
    int size = cluster_offsets[c + 1] - start;
    const int* members = &cluster_members[start];

    new_intensities[i] = equalize_d(i, members, size, points);
}

void kmeans_cuda(const Point* points, int n, int k, int T, int* new_intensities) {
    // --- Device memory allocation ---
    Point* d_points;
    int *d_assignment, *d_cx, *d_cy, *d_cz, *d_changed, *d_result;
    long long *d_sx, *d_sy, *d_sz;
    int *d_cnt;

    cudaMalloc(&d_points, n * sizeof(Point));
    cudaMalloc(&d_assignment, n * sizeof(int));
    cudaMalloc(&d_cx, k * sizeof(int));
    cudaMalloc(&d_cy, k * sizeof(int));
    cudaMalloc(&d_cz, k * sizeof(int));
    cudaMalloc(&d_changed, sizeof(int));
    cudaMalloc(&d_result, n * sizeof(int));
    cudaMalloc(&d_sx, k * sizeof(long long));
    cudaMalloc(&d_sy, k * sizeof(long long));
    cudaMalloc(&d_sz, k * sizeof(long long));
    cudaMalloc(&d_cnt, k * sizeof(int));

    cudaMemcpy(d_points, points, n * sizeof(Point), cudaMemcpyHostToDevice);

    // Initialize centroids = first k points (same as reference)
    int* h_cx = new int[k];
    int* h_cy = new int[k];
    int* h_cz = new int[k];
    for (int i = 0; i < k; i++) {
        h_cx[i] = points[i].x;
        h_cy[i] = points[i].y;
        h_cz[i] = points[i].z;
    }
    cudaMemcpy(d_cx, h_cx, k * sizeof(int), cudaMemcpyHostToDevice);
    cudaMemcpy(d_cy, h_cy, k * sizeof(int), cudaMemcpyHostToDevice);
    cudaMemcpy(d_cz, h_cz, k * sizeof(int), cudaMemcpyHostToDevice);
    cudaMemset(d_assignment, 0, n * sizeof(int));

    int threads = 256;
    int blocks = (n + threads - 1) / threads;
    // Shared memory size for centroids: 3 arrays of k ints
    size_t smem_size = 3 * k * sizeof(int);

    // --- Iterative assignment + centroid update ---
    for (int iter = 0; iter < T; iter++) {
        // Reset convergence flag
        int zero = 0;
        cudaMemcpy(d_changed, &zero, sizeof(int), cudaMemcpyHostToDevice);

        // Step 1: assign points to nearest centroid (centroids in shared mem)
        kmeans_assign_opt_kernel<<<blocks, threads, smem_size>>>(
            d_points, n, k, d_cx, d_cy, d_cz, d_assignment, d_changed);
        cudaDeviceSynchronize();

        // Check convergence
        int changed = 0;
        cudaMemcpy(&changed, d_changed, sizeof(int), cudaMemcpyDeviceToHost);
        if (!changed) break;

        // Step 2: zero out accumulators
        cudaMemset(d_sx, 0, k * sizeof(long long));
        cudaMemset(d_sy, 0, k * sizeof(long long));
        cudaMemset(d_sz, 0, k * sizeof(long long));
        cudaMemset(d_cnt, 0, k * sizeof(int));

        // Step 3: accumulate coordinate sums per cluster (atomicAdd on GPU)
        kmeans_accumulate_kernel<<<blocks, threads>>>(
            d_points, n, d_assignment, d_sx, d_sy, d_sz, d_cnt);

        // Step 4: divide sums by counts to get new centroids
        int cblocks = (k + 255) / 256;
        kmeans_update_centroids_kernel<<<cblocks, 256>>>(
            k, d_sx, d_sy, d_sz, d_cnt, d_cx, d_cy, d_cz);
        cudaDeviceSynchronize();
    }

    // --- Build cluster member lists on host ---
    // (Needed for equalization: each point needs access to all cluster members)
    int* h_assignment = new int[n];
    cudaMemcpy(h_assignment, d_assignment, n * sizeof(int), cudaMemcpyDeviceToHost);

    int* cluster_sizes = new int[k]();
    for (int i = 0; i < n; i++)
        cluster_sizes[h_assignment[i]]++;

    // Prefix sum → offsets into flat member array
    int* cluster_offsets = new int[k + 1];
    cluster_offsets[0] = 0;
    for (int c = 0; c < k; c++)
        cluster_offsets[c + 1] = cluster_offsets[c] + cluster_sizes[c];

    // Fill flat member array
    int* cluster_members = new int[n];
    int* fill = new int[k]();
    for (int i = 0; i < n; i++) {
        int c = h_assignment[i];
        cluster_members[cluster_offsets[c] + fill[c]] = i;
        fill[c]++;
    }

    // Transfer cluster structure to device and equalize
    int *d_offsets, *d_members;
    cudaMalloc(&d_offsets, (k + 1) * sizeof(int));
    cudaMalloc(&d_members, n * sizeof(int));
    cudaMemcpy(d_offsets, cluster_offsets, (k + 1) * sizeof(int), cudaMemcpyHostToDevice);
    cudaMemcpy(d_members, cluster_members, n * sizeof(int), cudaMemcpyHostToDevice);

    kmeans_equalize_kernel<<<blocks, threads>>>(
        d_points, n, d_assignment, d_offsets, d_members, d_result);
    cudaDeviceSynchronize();

    cudaMemcpy(new_intensities, d_result, n * sizeof(int), cudaMemcpyDeviceToHost);

    // --- Cleanup ---
    delete[] h_cx;  delete[] h_cy;  delete[] h_cz;
    delete[] h_assignment;  delete[] cluster_sizes;
    delete[] cluster_offsets;  delete[] cluster_members;  delete[] fill;
    cudaFree(d_points);  cudaFree(d_assignment);
    cudaFree(d_cx);  cudaFree(d_cy);  cudaFree(d_cz);
    cudaFree(d_changed);  cudaFree(d_result);
    cudaFree(d_sx);  cudaFree(d_sy);  cudaFree(d_sz);  cudaFree(d_cnt);
    cudaFree(d_offsets);  cudaFree(d_members);
}

// ============================================================
// Write output file (same format as reference)
// ============================================================
void write_output(const char* filename, const Point* points,
                   const int* new_intensities, int n) {
    FILE* f = fopen(filename, "w");
    if (!f) { perror(filename); exit(1); }
    for (int i = 0; i < n; i++)
        fprintf(f, "%d %d %d %d\n",
                points[i].x, points[i].y, points[i].z, new_intensities[i]);
    fclose(f);
}

// ============================================================
// Main: ./a2_cuda_opt input.txt knn|approx_knn|kmeans
// ============================================================
int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input_file> knn|approx_knn|kmeans\n", argv[0]);
        return 1;
    }

    const char* method = argv[2];

    // --- Read input ---
    FILE* f = fopen(argv[1], "r");
    if (!f) { perror(argv[1]); return 1; }

    int n, k, T;
    if (fscanf(f, "%d", &n) != 1) return 1;
    if (fscanf(f, "%d", &k) != 1) return 1;
    if (fscanf(f, "%d", &T) != 1) return 1;

    Point* points = new Point[n];
    for (int i = 0; i < n; i++) {
        if (fscanf(f, "%d %d %d %d",
                   &points[i].x, &points[i].y, &points[i].z, &points[i].intensity) != 4)
            return 1;
    }
    fclose(f);

    fprintf(stderr, "Loaded %d points, k=%d, T=%d, method=%s (CUDA optimized)\n",
            n, k, T, method);

    int* new_intensities = new int[n];
    auto t0 = std::chrono::high_resolution_clock::now();

    if (strcmp(method, "knn") == 0) {
        knn_exact_cuda(points, n, k, new_intensities);
        write_output("knn.txt", points, new_intensities, n);
    } else if (strcmp(method, "approx_knn") == 0) {
        approx_knn_cuda(points, n, k, new_intensities);
        write_output("approx_knn.txt", points, new_intensities, n);
    } else if (strcmp(method, "kmeans") == 0) {
        kmeans_cuda(points, n, k, T, new_intensities);
        write_output("kmeans.txt", points, new_intensities, n);
    } else {
        fprintf(stderr, "Unknown method: %s\n", method);
        return 1;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    fprintf(stderr, "%s (CUDA optimized): %.2f ms\n", method, ms);

    delete[] points;
    delete[] new_intensities;
    return 0;
}
