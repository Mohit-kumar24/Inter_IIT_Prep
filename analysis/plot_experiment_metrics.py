"""Plot makespan and runtime summaries from the experiment CSV."""

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path

BASE_DIR = Path(__file__).parent.parent.resolve()
ANALYSIS_DIR = BASE_DIR / "analysis"
CSV_FILE = ANALYSIS_DIR / "experiment_summary.csv"

def generate_plots():
    if not CSV_FILE.exists():
        print(f"Error: {CSV_FILE} not found.")
        return

    df = pd.read_csv(CSV_FILE)
    
    # 1. Makespan Comparison Bar Chart
    plt.figure(figsize=(14, 8))
    
    instances = df['Instance'].unique()
    algorithms = df['Algorithm'].unique()
    
    x = np.arange(len(instances))
    width = 0.15
    
    for i, algo in enumerate(algorithms):
        subset = df[df['Algorithm'] == algo]
        # Ensure ordering matches instances
        subset = subset.set_index('Instance').reindex(instances).reset_index()
        offset = (i - len(algorithms)/2) * width + width/2
        plt.bar(x + offset, subset['Makespan'], width, label=algo)

    plt.title("Algorithm Makespan Comparison Across Instances", fontsize=16, pad=20)
    plt.xticks(x, instances, rotation=45, ha="right")
    plt.ylabel("Makespan (Lower is Better)")
    plt.legend(title="Algorithm", bbox_to_anchor=(1.05, 1), loc='upper left')
    plt.tight_layout()
    plt.savefig(ANALYSIS_DIR / "makespan_comparison.png", bbox_inches='tight', dpi=300)
    plt.close()

    # 2. Runtime Comparison Bar Chart (Log Scale)
    plt.figure(figsize=(14, 8))
    
    for i, algo in enumerate(algorithms):
        subset = df[df['Algorithm'] == algo]
        subset = subset.set_index('Instance').reindex(instances).reset_index()
        offset = (i - len(algorithms)/2) * width + width/2
        plt.bar(x + offset, subset['Runtime_sec'], width, label=algo)

    plt.title("Algorithm Runtime Comparison (Log Scale)", fontsize=16, pad=20)
    plt.xticks(x, instances, rotation=45, ha="right")
    plt.ylabel("Runtime in Seconds (Log Scale)")
    plt.yscale('log')
    plt.legend(title="Algorithm", bbox_to_anchor=(1.05, 1), loc='upper left')
    plt.tight_layout()
    plt.savefig(ANALYSIS_DIR / "runtime_comparison.png", bbox_inches='tight', dpi=300)
    plt.close()
    
    print("Plots generated successfully in analysis/")

if __name__ == "__main__":
    generate_plots()
