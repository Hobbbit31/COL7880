// ============================================================
// Local Histogram Equalization on 3D Point Clouds (CUDA)
//
// Three approaches:
//   1. Exact KNN    → knn.txt
//   2. Approx KNN   → approx_knn.txt  (random projection)
//   3. K-Means      → kmeans.txt
//
// Usage: ./a2_cuda input.txt knn|approx_knn|kmeans
// ============================================================

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <climits>
#include <algorithm>
#include <chrono>

struct Point {
    int x, y, z;
    int intensity;
};

// ============================================================
// Device helper functions
// ============================================================

__device__ long long dist_sq_d(const Point& a, const Point& b) {
    long long dx = (long long)a.x - b.x;
    long long dy = (long long)a.y - b.y;
    long long dz = (long long)a.z - b.z;
    return dx*dx + dy*dy + dz*dz;
}

__device__ bool is_worse_d(long long dist_a, int idx_a, long long dist_b, int idx_b, const Point* points) {
    if (dist_a != dist_b) return dist_a > dist_b;
    if (points[idx_a].x != points[idx_b].x) return points[idx_a].x > points[idx_b].x;
    if (points[idx_a].y != points[idx_b].y) return points[idx_a].y > points[idx_b].y;
    return points[idx_a].z > points[idx_b].z;
}

__device__ int equalize_d(int point_idx, const int* neighbor_indices, int m, const Point* points) {
    int hist[256];
    for (int v = 0; v < 256; v++) hist[v] = 0;

    for (int j = 0; j < m; j++)
        hist[points[neighbor_indices[j]].intensity]++;

    int cdf[256];
    cdf[0] = hist[0];
    for (int v = 1; v < 256; v++)
        cdf[v] = cdf[v-1] + hist[v];

    int cdf_min = 0;
    for (int v = 0; v < 256; v++)
        if (cdf[v] > 0) { cdf_min = cdf[v]; break; }

    int I_i = points[point_idx].intensity;
    if (m == cdf_min) return I_i;

    int val = (int)floor((double)(cdf[I_i] - cdf_min) / (double)(m - cdf_min) * 255.0);
    if (val < 0) val = 0;
    if (val > 255) val = 255;
    return val;
}

// ============================================================
//                      1. EXACT KNN (CUDA)
//
// Each thread handles one point, brute-force checks all others.
// ============================================================
__global__ void knn_exact_kernel(const Point* points, int n, int k, int* new_intensities) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;

    // Use dynamic local memory (allocated on stack, limited by k)
    // For large k, this may overflow stack — keep k reasonable
    extern __shared__ char shared_mem[];

    // Each thread uses its own local arrays (on registers/local mem)
    // We allocate on heap via malloc — but for basic version, use fixed max
    // Instead, use global memory offsets passed in, but simplest: local arrays
    // For a basic version, limit k to a reasonable max (e.g., 256)

    long long best_dists[256];  // max k = 256
    int best_idx[256];
    int found = 0;

    for (int j = 0; j < n; j++) {
        if (j == i) continue;
        long long d = dist_sq_d(points[i], points[j]);

        if (found < k) {
            best_dists[found] = d;
            best_idx[found] = j;
            found++;
        } else {
            int worst = 0;
            for (int w = 1; w < k; w++) {
                if (is_worse_d(best_dists[w], best_idx[w],
                               best_dists[worst], best_idx[worst], points))
                    worst = w;
            }
            if (is_worse_d(best_dists[worst], best_idx[worst], d, j, points)) {
                best_dists[worst] = d;
                best_idx[worst] = j;
            }
        }
    }

    // Build neighbor list: self + k neighbors
    int neighbors[257];  // max k+1 = 257
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

    int threads = 256;
    int blocks = (n + threads - 1) / threads;
    knn_exact_kernel<<<blocks, threads>>>(d_points, n, k, d_result);
    cudaDeviceSynchronize();

    cudaMemcpy(new_intensities, d_result, n * sizeof(int), cudaMemcpyDeviceToHost);
    cudaFree(d_points);
    cudaFree(d_result);
}

// ============================================================
//                   2. APPROXIMATE KNN (CUDA)
//
// Random projection done on host (sort is hard on GPU for basic ver).
// Window scanning + top-k maintenance done on GPU per projection.
// Final equalization done on GPU.
// ============================================================

__global__ void approx_window_kernel(const Point* points, int n, int k,
                                      const int* order, const int* pos,
                                      int window_size,
                                      long long* best_dists, int* best_idx, int* found) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;

    int my_pos = pos[i];
    int left  = (my_pos - window_size > 0) ? my_pos - window_size : 0;
    int right = (my_pos + window_size < n) ? my_pos + window_size : n;

    long long* my_dists = &best_dists[i * k];
    int* my_idx = &best_idx[i * k];
    int my_found = found[i];

    for (int s = left; s < right; s++) {
        int j = order[s];
        if (j == i) continue;
        long long d = dist_sq_d(points[i], points[j]);

        // Check for duplicate
        bool dup = false;
        for (int w = 0; w < my_found; w++) {
            if (my_idx[w] == j) { dup = true; break; }
        }
        if (dup) continue;

        if (my_found < k) {
            my_dists[my_found] = d;
            my_idx[my_found] = j;
            my_found++;
        } else {
            int worst = 0;
            for (int w = 1; w < k; w++) {
                if (is_worse_d(my_dists[w], my_idx[w], my_dists[worst], my_idx[worst], points))
                    worst = w;
            }
            if (is_worse_d(my_dists[worst], my_idx[worst], d, j, points)) {
                my_dists[worst] = d;
                my_idx[worst] = j;
            }
        }
    }

    found[i] = my_found;
}

__global__ void equalize_kernel(const Point* points, int n, int k,
                                 const int* best_idx, const int* found,
                                 int* new_intensities) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;

    int num = found[i];
    // neighbors = self + found neighbors (max k+1)
    int neighbors[257];
    neighbors[0] = i;
    for (int j = 0; j < num; j++)
        neighbors[j + 1] = best_idx[i * k + j];

    new_intensities[i] = equalize_d(i, neighbors, num + 1, points);
}

void approx_knn_cuda(const Point* points, int n, int k, int* new_intensities) {
    const int NUM_PROJECTIONS = 30;
    const int WINDOW_SIZE = 5 * k;

    // Generate random directions on host
    double dirs[30][3];
    srand(12345);
    for (int p = 0; p < NUM_PROJECTIONS; p++) {
        dirs[p][0] = (double)rand() / RAND_MAX - 0.5;
        dirs[p][1] = (double)rand() / RAND_MAX - 0.5;
        dirs[p][2] = (double)rand() / RAND_MAX - 0.5;
        double len = sqrt(dirs[p][0]*dirs[p][0] + dirs[p][1]*dirs[p][1] + dirs[p][2]*dirs[p][2]);
        dirs[p][0] /= len; dirs[p][1] /= len; dirs[p][2] /= len;
    }

    // Allocate device memory
    Point* d_points;
    long long* d_best_dists;
    int *d_best_idx, *d_found, *d_order, *d_pos, *d_result;

    cudaMalloc(&d_points, n * sizeof(Point));
    cudaMalloc(&d_best_dists, (long long)n * k * sizeof(long long));
    cudaMalloc(&d_best_idx, n * k * sizeof(int));
    cudaMalloc(&d_found, n * sizeof(int));
    cudaMalloc(&d_order, n * sizeof(int));
    cudaMalloc(&d_pos, n * sizeof(int));
    cudaMalloc(&d_result, n * sizeof(int));

    cudaMemcpy(d_points, points, n * sizeof(Point), cudaMemcpyHostToDevice);
    cudaMemset(d_found, 0, n * sizeof(int));

    // Host arrays for sorting
    double* proj = new double[n];
    int* order = new int[n];
    int* pos = new int[n];

    int threads = 256;
    int blocks = (n + threads - 1) / threads;

    for (int p = 0; p < NUM_PROJECTIONS; p++) {
        double rx = dirs[p][0], ry = dirs[p][1], rz = dirs[p][2];

        // Project + sort on host (basic version)
        for (int i = 0; i < n; i++) {
            proj[i] = points[i].x * rx + points[i].y * ry + points[i].z * rz;
            order[i] = i;
        }
        std::sort(order, order + n, [&](int a, int b) {
            return proj[a] < proj[b];
        });
        for (int s = 0; s < n; s++)
            pos[order[s]] = s;

        // Copy sorted order and position map to device
        cudaMemcpy(d_order, order, n * sizeof(int), cudaMemcpyHostToDevice);
        cudaMemcpy(d_pos, pos, n * sizeof(int), cudaMemcpyHostToDevice);

        // Run window scan kernel
        approx_window_kernel<<<blocks, threads>>>(d_points, n, k,
                                                   d_order, d_pos, WINDOW_SIZE,
                                                   d_best_dists, d_best_idx, d_found);
        cudaDeviceSynchronize();
    }

    // Equalize on GPU
    equalize_kernel<<<blocks, threads>>>(d_points, n, k, d_best_idx, d_found, d_result);
    cudaDeviceSynchronize();

    cudaMemcpy(new_intensities, d_result, n * sizeof(int), cudaMemcpyDeviceToHost);

    // Cleanup
    delete[] proj; delete[] order; delete[] pos;
    cudaFree(d_points); cudaFree(d_best_dists); cudaFree(d_best_idx);
    cudaFree(d_found); cudaFree(d_order); cudaFree(d_pos); cudaFree(d_result);
}

// ============================================================
//                     3. K-MEANS (CUDA)
//
// Assignment step on GPU, centroid recomputation on host.
// ============================================================

__global__ void kmeans_assign_kernel(const Point* points, int n, int k,
                                      const int* cx, const int* cy, const int* cz,
                                      int* assignment, int* changed_flag) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;

    long long best_dist = LLONG_MAX;
    int best_c = 0;

    for (int c = 0; c < k; c++) {
        long long dx = (long long)points[i].x - cx[c];
        long long dy = (long long)points[i].y - cy[c];
        long long dz = (long long)points[i].z - cz[c];
        long long d = dx*dx + dy*dy + dz*dz;

        if (d < best_dist) {
            best_dist = d;
            best_c = c;
        } else if (d == best_dist) {
            if (cx[c] < cx[best_c] ||
                (cx[c] == cx[best_c] && cy[c] < cy[best_c]) ||
                (cx[c] == cx[best_c] && cy[c] == cy[best_c] && cz[c] < cz[best_c]))
                best_c = c;
        }
    }

    if (assignment[i] != best_c) {
        assignment[i] = best_c;
        atomicExch(changed_flag, 1);
    }
}

__global__ void kmeans_equalize_kernel(const Point* points, int n,
                                        const int* assignment,
                                        const int* cluster_offsets,
                                        const int* cluster_members,
                                        int* new_intensities) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;

    int c = assignment[i];
    int start = cluster_offsets[c];
    int size = cluster_offsets[c + 1] - start;
    const int* members = &cluster_members[start];

    new_intensities[i] = equalize_d(i, members, size, points);
}

void kmeans_cuda(const Point* points, int n, int k, int T, int* new_intensities) {
    Point* d_points;
    int *d_assignment, *d_cx, *d_cy, *d_cz, *d_changed, *d_result;

    cudaMalloc(&d_points, n * sizeof(Point));
    cudaMalloc(&d_assignment, n * sizeof(int));
    cudaMalloc(&d_cx, k * sizeof(int));
    cudaMalloc(&d_cy, k * sizeof(int));
    cudaMalloc(&d_cz, k * sizeof(int));
    cudaMalloc(&d_changed, sizeof(int));
    cudaMalloc(&d_result, n * sizeof(int));

    cudaMemcpy(d_points, points, n * sizeof(Point), cudaMemcpyHostToDevice);

    // Init centroids = first k points
    int* h_cx = new int[k]; int* h_cy = new int[k]; int* h_cz = new int[k];
    for (int i = 0; i < k; i++) {
        h_cx[i] = points[i].x; h_cy[i] = points[i].y; h_cz[i] = points[i].z;
    }
    cudaMemcpy(d_cx, h_cx, k * sizeof(int), cudaMemcpyHostToDevice);
    cudaMemcpy(d_cy, h_cy, k * sizeof(int), cudaMemcpyHostToDevice);
    cudaMemcpy(d_cz, h_cz, k * sizeof(int), cudaMemcpyHostToDevice);

    // Init assignments to 0
    cudaMemset(d_assignment, 0, n * sizeof(int));

    int* h_assignment = new int[n];
    int threads = 256;
    int blocks = (n + threads - 1) / threads;

    for (int iter = 0; iter < T; iter++) {
        int zero = 0;
        cudaMemcpy(d_changed, &zero, sizeof(int), cudaMemcpyHostToDevice);

        kmeans_assign_kernel<<<blocks, threads>>>(d_points, n, k,
                                                   d_cx, d_cy, d_cz,
                                                   d_assignment, d_changed);
        cudaDeviceSynchronize();

        int changed = 0;
        cudaMemcpy(&changed, d_changed, sizeof(int), cudaMemcpyDeviceToHost);
        if (!changed) break;

        // Recompute centroids on host
        cudaMemcpy(h_assignment, d_assignment, n * sizeof(int), cudaMemcpyDeviceToHost);

        long long* sx = new long long[k](); long long* sy = new long long[k](); long long* sz = new long long[k]();
        int* cnt = new int[k]();
        for (int i = 0; i < n; i++) {
            int c = h_assignment[i];
            sx[c] += points[i].x; sy[c] += points[i].y; sz[c] += points[i].z; cnt[c]++;
        }
        for (int c = 0; c < k; c++) {
            if (cnt[c] > 0) {
                h_cx[c] = (int)(sx[c] / cnt[c]);
                h_cy[c] = (int)(sy[c] / cnt[c]);
                h_cz[c] = (int)(sz[c] / cnt[c]);
            }
        }
        cudaMemcpy(d_cx, h_cx, k * sizeof(int), cudaMemcpyHostToDevice);
        cudaMemcpy(d_cy, h_cy, k * sizeof(int), cudaMemcpyHostToDevice);
        cudaMemcpy(d_cz, h_cz, k * sizeof(int), cudaMemcpyHostToDevice);

        delete[] sx; delete[] sy; delete[] sz; delete[] cnt;
    }

    // Build cluster member lists on host for equalization
    cudaMemcpy(h_assignment, d_assignment, n * sizeof(int), cudaMemcpyDeviceToHost);

    int* cluster_sizes = new int[k]();
    for (int i = 0; i < n; i++) cluster_sizes[h_assignment[i]]++;

    int* cluster_offsets = new int[k + 1];
    cluster_offsets[0] = 0;
    for (int c = 0; c < k; c++)
        cluster_offsets[c + 1] = cluster_offsets[c] + cluster_sizes[c];

    int* cluster_members = new int[n];
    int* fill = new int[k]();
    for (int i = 0; i < n; i++) {
        int c = h_assignment[i];
        cluster_members[cluster_offsets[c] + fill[c]] = i;
        fill[c]++;
    }

    // Copy to device and run equalization
    int *d_offsets, *d_members;
    cudaMalloc(&d_offsets, (k + 1) * sizeof(int));
    cudaMalloc(&d_members, n * sizeof(int));
    cudaMemcpy(d_offsets, cluster_offsets, (k + 1) * sizeof(int), cudaMemcpyHostToDevice);
    cudaMemcpy(d_members, cluster_members, n * sizeof(int), cudaMemcpyHostToDevice);

    kmeans_equalize_kernel<<<blocks, threads>>>(d_points, n, d_assignment,
                                                 d_offsets, d_members, d_result);
    cudaDeviceSynchronize();

    cudaMemcpy(new_intensities, d_result, n * sizeof(int), cudaMemcpyDeviceToHost);

    // Cleanup
    delete[] h_cx; delete[] h_cy; delete[] h_cz;
    delete[] h_assignment; delete[] cluster_sizes;
    delete[] cluster_offsets; delete[] cluster_members; delete[] fill;
    cudaFree(d_points); cudaFree(d_assignment);
    cudaFree(d_cx); cudaFree(d_cy); cudaFree(d_cz);
    cudaFree(d_changed); cudaFree(d_result);
    cudaFree(d_offsets); cudaFree(d_members);
}

// ============================================================
// Write output
// ============================================================
void write_output(const char* filename, const Point* points, const int* new_intensities, int n) {
    FILE* f = fopen(filename, "w");
    if (!f) { perror(filename); exit(1); }
    for (int i = 0; i < n; i++)
        fprintf(f, "%d %d %d %d\n", points[i].x, points[i].y, points[i].z, new_intensities[i]);
    fclose(f);
}

// ============================================================
// Main
// ============================================================
int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input_file> knn|approx_knn|kmeans\n", argv[0]);
        return 1;
    }

    const char* method = argv[2];

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

    fprintf(stderr, "Loaded %d points, k=%d, T=%d, method=%s (CUDA)\n", n, k, T, method);

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
    fprintf(stderr, "%s (CUDA): %.2f ms\n", method, ms);

    delete[] points;
    delete[] new_intensities;
    return 0;
}
