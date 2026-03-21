// Pure sequential reference implementation (no OpenMP, no tricks)
// Used only for correctness verification

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <climits>
#include <vector>

struct Point {
    int x, y, z;
    int intensity;
};

long long dist_sq(const Point& a, const Point& b) {
    long long dx = (long long)a.x - b.x;
    long long dy = (long long)a.y - b.y;
    long long dz = (long long)a.z - b.z;
    return dx*dx + dy*dy + dz*dz;
}

int equalize(int point_idx, const int* neighbors, int m, const Point* points) {
    int hist[256] = {0};
    for (int j = 0; j < m; j++)
        hist[points[neighbors[j]].intensity]++;

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

// ---- KNN (brute force, no OpenMP) ----
void knn_seq(const Point* points, int n, int k, int* result) {
    for (int i = 0; i < n; i++) {
        long long* bd = new long long[k];
        int*       bi = new int[k];
        int found = 0;

        for (int j = 0; j < n; j++) {
            if (j == i) continue;
            long long d = dist_sq(points[i], points[j]);

            if (found < k) {
                bd[found] = d;
                bi[found] = j;
                found++;
            } else {
                // find worst
                int worst = 0;
                for (int w = 1; w < k; w++) {
                    if (bd[w] > bd[worst] ||
                        (bd[w] == bd[worst] &&
                         (points[bi[w]].x > points[bi[worst]].x ||
                          (points[bi[w]].x == points[bi[worst]].x && points[bi[w]].y > points[bi[worst]].y) ||
                          (points[bi[w]].x == points[bi[worst]].x && points[bi[w]].y == points[bi[worst]].y && points[bi[w]].z > points[bi[worst]].z))))
                        worst = w;
                }
                if (bd[worst] > d ||
                    (bd[worst] == d &&
                     (points[bi[worst]].x > points[j].x ||
                      (points[bi[worst]].x == points[j].x && points[bi[worst]].y > points[j].y) ||
                      (points[bi[worst]].x == points[j].x && points[bi[worst]].y == points[j].y && points[bi[worst]].z > points[j].z)))) {
                    bd[worst] = d;
                    bi[worst] = j;
                }
            }
        }

        int* neighbors = new int[k+1];
        neighbors[0] = i;
        for (int j = 0; j < k; j++) neighbors[j+1] = bi[j];
        result[i] = equalize(i, neighbors, k+1, points);

        delete[] bd; delete[] bi; delete[] neighbors;
    }
}

// ---- KMEANS (no OpenMP) ----
void kmeans_seq(const Point* points, int n, int k, int T, int* result) {
    int* cx = new int[k]; int* cy = new int[k]; int* cz = new int[k];
    for (int i = 0; i < k; i++) { cx[i] = points[i].x; cy[i] = points[i].y; cz[i] = points[i].z; }

    int* assignment = new int[n];
    for (int i = 0; i < n; i++) assignment[i] = 0;

    for (int iter = 0; iter < T; iter++) {
        bool changed = false;
        for (int i = 0; i < n; i++) {
            long long best_dist = LLONG_MAX;
            int best_c = 0;
            for (int c = 0; c < k; c++) {
                long long dx = (long long)points[i].x - cx[c];
                long long dy = (long long)points[i].y - cy[c];
                long long dz = (long long)points[i].z - cz[c];
                long long d = dx*dx + dy*dy + dz*dz;
                if (d < best_dist) { best_dist = d; best_c = c; }
                else if (d == best_dist) {
                    if (cx[c] < cx[best_c] ||
                        (cx[c] == cx[best_c] && cy[c] < cy[best_c]) ||
                        (cx[c] == cx[best_c] && cy[c] == cy[best_c] && cz[c] < cz[best_c]))
                        best_c = c;
                }
            }
            if (assignment[i] != best_c) { assignment[i] = best_c; changed = true; }
        }
        if (!changed) break;

        long long* sx = new long long[k](); long long* sy = new long long[k](); long long* sz = new long long[k]();
        int* cnt = new int[k]();
        for (int i = 0; i < n; i++) {
            int c = assignment[i];
            sx[c] += points[i].x; sy[c] += points[i].y; sz[c] += points[i].z; cnt[c]++;
        }
        for (int c = 0; c < k; c++) {
            if (cnt[c] > 0) { cx[c] = (int)(sx[c]/cnt[c]); cy[c] = (int)(sy[c]/cnt[c]); cz[c] = (int)(sz[c]/cnt[c]); }
        }
        delete[] sx; delete[] sy; delete[] sz; delete[] cnt;
    }

    std::vector<std::vector<int>> clusters(k);
    for (int i = 0; i < n; i++) clusters[assignment[i]].push_back(i);

    for (int i = 0; i < n; i++) {
        int c = assignment[i];
        result[i] = equalize(i, clusters[c].data(), (int)clusters[c].size(), points);
    }

    delete[] cx; delete[] cy; delete[] cz; delete[] assignment;
}

void write_output(const char* filename, const Point* points, const int* vals, int n) {
    FILE* f = fopen(filename, "w");
    for (int i = 0; i < n; i++)
        fprintf(f, "%d %d %d %d\n", points[i].x, points[i].y, points[i].z, vals[i]);
    fclose(f);
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "Usage: %s <input> knn|kmeans\n", argv[0]); return 1; }

    FILE* f = fopen(argv[1], "r");
    int n, k, T;
    fscanf(f, "%d", &n); fscanf(f, "%d", &k); fscanf(f, "%d", &T);
    Point* points = new Point[n];
    for (int i = 0; i < n; i++)
        fscanf(f, "%d %d %d %d", &points[i].x, &points[i].y, &points[i].z, &points[i].intensity);
    fclose(f);

    int* result = new int[n];

    if (strcmp(argv[2], "knn") == 0) {
        knn_seq(points, n, k, result);
        write_output("knn_ref.txt", points, result, n);
    } else if (strcmp(argv[2], "kmeans") == 0) {
        kmeans_seq(points, n, k, T, result);
        write_output("kmeans_ref.txt", points, result, n);
    }

    delete[] points; delete[] result;
    return 0;
}
