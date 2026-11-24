"""
Latency summary for latencies.csv (written by the C++ benchmarks): mean,
percentiles, and a text histogram. Needs numpy.
"""

import sys
import numpy as np


def load(path: str) -> np.ndarray:
    # skip the header row, read one integer latency (ns) per line
    return np.loadtxt(path, skiprows=1, dtype=np.int64)


def summarize(latencies: np.ndarray) -> None:
    n = latencies.size
    print(f"samples      : {n:,}")
    print(f"mean         : {latencies.mean():.1f} ns")
    print(f"median (p50) : {np.percentile(latencies, 50):.0f} ns")
    print(f"p95          : {np.percentile(latencies, 95):.0f} ns")
    print(f"p99          : {np.percentile(latencies, 99):.0f} ns")
    print(f"p99.9        : {np.percentile(latencies, 99.9):.0f} ns")
    print(f"max          : {latencies.max():,} ns")


def histogram(latencies: np.ndarray, buckets: int = 10) -> None:
    # Drop the top 1% so a few scheduling spikes don't squash the histogram.
    cutoff = np.percentile(latencies, 99)
    trimmed = latencies[latencies <= cutoff]

    counts, edges = np.histogram(trimmed, bins=buckets)
    peak = counts.max()
    print("\nlatency distribution (bulk 99%):")
    for i in range(buckets):
        bar = "#" * int(50 * counts[i] / peak)
        print(f"  {edges[i]:6.0f}-{edges[i+1]:6.0f} ns | {bar} {counts[i]:,}")


def main() -> None:
    path = sys.argv[1] if len(sys.argv) > 1 else "latencies.csv"
    latencies = load(path)
    print("AtlasRisk latency analytics")
    print("---------------------------")
    summarize(latencies)
    histogram(latencies)


if __name__ == "__main__":
    main()
