#!/usr/bin/env python3
"""Generate test input for histogram equalization. Coordinates are integers."""
import random
import sys

def generate(n=100, k=10, T=50, seed=42):
    random.seed(seed)
    print(n)
    print(k)
    print(T)
    for _ in range(n):
        x = random.randint(-10000, 10000)
        y = random.randint(-10000, 10000)
        z = random.randint(-10000, 10000)
        intensity = random.randint(0, 255)
        print(f"{x} {y} {z} {intensity}")

if __name__ == "__main__":
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    k = int(sys.argv[2]) if len(sys.argv) > 2 else 10
    T = int(sys.argv[3]) if len(sys.argv) > 3 else 50
    seed = int(sys.argv[4]) if len(sys.argv) > 4 else 42
    generate(n, k, T, seed)
