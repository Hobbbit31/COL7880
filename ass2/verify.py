#!/usr/bin/env python3
"""
Verify correctness of histogram equalization output.

Usage:
  python3 verify.py <input_file> <output_file> --method knn|kmeans [--reference ref_file] [--recompute]

Implements the EXACT same algorithm as sequential.cpp with all Piazza clarifications:
- KNN: k neighbors (excl self) + self = k+1 points, m = k+1
- K-Means: integer division for centroids, tie-break by lex smaller centroid
- Tie-breaking for KNN: lexically smaller point
- Coordinates are integers
"""
import sys
import argparse
import math

def read_input(path):
    with open(path) as f:
        n = int(f.readline().strip())
        k = int(f.readline().strip())
        T = int(f.readline().strip())
        points = []
        for _ in range(n):
            parts = f.readline().strip().split()
            x, y, z = int(parts[0]), int(parts[1]), int(parts[2])
            intensity = int(parts[3])
            points.append((x, y, z, intensity))
    return n, k, T, points

def read_output(path):
    points = []
    with open(path) as f:
        for line in f:
            parts = line.strip().split()
            if len(parts) < 4:
                continue
            x, y, z = int(parts[0]), int(parts[1]), int(parts[2])
            intensity = int(float(parts[3]))
            points.append((x, y, z, intensity))
    return points

def dist_sq(a, b):
    return (a[0]-b[0])**2 + (a[1]-b[1])**2 + (a[2]-b[2])**2

def lex_less(a, b):
    """a, b are (x,y,z,intensity) tuples. Compare by (x,y,z)."""
    if a[0] != b[0]: return a[0] < b[0]
    if a[1] != b[1]: return a[1] < b[1]
    return a[2] < b[2]

def equalize_point(point_idx, neighbor_indices, points, m):
    hist = [0] * 256
    for j in neighbor_indices:
        hist[points[j][3]] += 1

    cdf = [0] * 256
    cdf[0] = hist[0]
    for v in range(1, 256):
        cdf[v] = cdf[v-1] + hist[v]

    cdf_min = 0
    for v in range(256):
        if cdf[v] > 0:
            cdf_min = cdf[v]
            break

    I_i = points[point_idx][3]
    if m == cdf_min:
        return I_i

    new_val = int(math.floor((cdf[I_i] - cdf_min) / (m - cdf_min) * 255.0))
    return max(0, min(255, new_val))

def compute_knn_reference(n, k, points):
    """Brute-force KNN: k neighbors (excl self) + self = k+1 points, m = k+1."""
    result = []
    for i in range(n):
        dists = []
        for j in range(n):
            if j == i:
                continue
            dists.append((dist_sq(points[i], points[j]), j))
        # Sort with tie-breaking: same dist -> lex smaller point
        dists.sort(key=lambda x: (x[0], points[x[1]][0], points[x[1]][1], points[x[1]][2]))
        # k neighbors + self
        neighbors = [i] + [d[1] for d in dists[:k]]
        result.append(equalize_point(i, neighbors, points, k + 1))
    return result

def compute_kmeans_reference(n, k, T, points):
    """K-means with integer division centroids, lex tie-breaking."""
    cx = [points[i][0] for i in range(k)]
    cy = [points[i][1] for i in range(k)]
    cz = [points[i][2] for i in range(k)]

    assignment = [0] * n

    for iteration in range(T):
        changed = False
        for i in range(n):
            best_dist = float('inf')
            best_c = 0
            for c in range(k):
                dx = points[i][0] - cx[c]
                dy = points[i][1] - cy[c]
                dz = points[i][2] - cz[c]
                d = dx*dx + dy*dy + dz*dz
                if d < best_dist:
                    best_dist = d
                    best_c = c
                elif d == best_dist:
                    # Tie-break: lex smaller centroid
                    if (cx[c], cy[c], cz[c]) < (cx[best_c], cy[best_c], cz[best_c]):
                        best_c = c
            if assignment[i] != best_c:
                assignment[i] = best_c
                changed = True

        if not changed:
            break

        # Update centroids with integer division
        sx = [0]*k; sy = [0]*k; sz = [0]*k; cnt = [0]*k
        for i in range(n):
            c = assignment[i]
            sx[c] += points[i][0]
            sy[c] += points[i][1]
            sz[c] += points[i][2]
            cnt[c] += 1
        for c in range(k):
            if cnt[c] > 0:
                # Python integer division truncates toward negative infinity,
                # but C integer division truncates toward zero. Match C behavior.
                cx[c] = int(sx[c] / cnt[c]) if sx[c] * cnt[c] >= 0 else -(-sx[c] // cnt[c]) if sx[c] % cnt[c] == 0 else -((-sx[c]) // cnt[c])
                cy[c] = int(sy[c] / cnt[c]) if sy[c] * cnt[c] >= 0 else -(-sy[c] // cnt[c]) if sy[c] % cnt[c] == 0 else -((-sy[c]) // cnt[c])
                cz[c] = int(sz[c] / cnt[c]) if sz[c] * cnt[c] >= 0 else -(-sz[c] // cnt[c]) if sz[c] % cnt[c] == 0 else -((-sz[c]) // cnt[c])

    clusters = [[] for _ in range(k)]
    for i in range(n):
        clusters[assignment[i]].append(i)

    result = []
    for i in range(n):
        c = assignment[i]
        result.append(equalize_point(i, clusters[c], points, len(clusters[c])))
    return result

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input_file")
    parser.add_argument("output_file")
    parser.add_argument("--method", choices=["knn", "kmeans", "approx_knn"], default="knn")
    parser.add_argument("--reference", help="Reference output file for MAE comparison")
    parser.add_argument("--recompute", action="store_true", help="Recompute ground truth in Python")
    args = parser.parse_args()

    n, k, T, input_points = read_input(args.input_file)
    output_points = read_output(args.output_file)

    print(f"Input:  {n} points, k={k}, T={T}")
    print(f"Output: {len(output_points)} points")

    if len(output_points) != n:
        print(f"FAIL: point count mismatch ({len(output_points)} vs {n})")
        return 1

    # Check coordinates
    coord_ok = True
    for i in range(n):
        if input_points[i][:3] != output_points[i][:3]:
            print(f"FAIL: coordinate mismatch at point {i}: {input_points[i][:3]} vs {output_points[i][:3]}")
            coord_ok = False
            break
    if coord_ok:
        print("PASS: coordinates match exactly")

    # Check intensity range
    out_of_range = sum(1 for p in output_points if p[3] < 0 or p[3] > 255)
    print(f"{'PASS' if out_of_range == 0 else 'FAIL'}: intensity range check ({out_of_range} out of range)")

    # MAE vs reference
    if args.reference:
        ref_points = read_output(args.reference)
        if len(ref_points) == n:
            mae = sum(abs(output_points[i][3] - ref_points[i][3]) for i in range(n)) / n
            errors = [abs(output_points[i][3] - ref_points[i][3]) for i in range(n)]
            print(f"MAE vs reference: {mae:.4f}, max error: {max(errors)}, exact: {sum(1 for e in errors if e==0)}/{n}")

    # Recompute
    if args.recompute:
        print(f"\nRecomputing {args.method} in Python...")
        if args.method in ("knn", "approx_knn"):
            ref = compute_knn_reference(n, k, input_points)
        else:
            ref = compute_kmeans_reference(n, k, T, input_points)

        mismatches = sum(1 for i in range(n) if output_points[i][3] != ref[i])
        mae = sum(abs(output_points[i][3] - ref[i]) for i in range(n)) / n
        print(f"MAE: {mae:.4f}, exact matches: {n - mismatches}/{n}")
        if mismatches > 0:
            count = 0
            for i in range(n):
                if output_points[i][3] != ref[i]:
                    print(f"  Point {i}: got {output_points[i][3]}, expected {ref[i]}")
                    count += 1
                    if count >= 5: break
        else:
            print("PASS: perfect match")

    return 0

if __name__ == "__main__":
    sys.exit(main())
