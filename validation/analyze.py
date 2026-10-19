"""
PulseLab — Validation analysis (Phase 11)

Compares PulseLab readings with a reference pulse oximeter using:
  - Mean absolute error (MAE)
  - Bias (mean difference)
  - 95% limits of agreement (Bland–Altman)
  - Pearson correlation (reported, but not a measure of agreement on its own)

Usage:
    pip install pandas matplotlib numpy
    python analyze.py readings.csv

Saves Bland–Altman plots as PNG files next to the CSV.
"""

import sys
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

METRICS = [
    ("hr", "Heart rate", "BPM"),
    ("spo2", "SpO2", "%"),
]


def analyze(df: pd.DataFrame, key: str, name: str, unit: str, out_dir: Path) -> dict | None:
    dev_col, ref_col = f"pulselab_{key}", f"reference_{key}"
    pair = df[[dev_col, ref_col]].dropna()
    if len(pair) < 2:
        print(f"{name}: not enough paired readings (need at least 2).")
        return None

    dev = pair[dev_col].to_numpy(float)
    ref = pair[ref_col].to_numpy(float)
    diff = dev - ref
    mean = (dev + ref) / 2

    bias = diff.mean()
    sd = diff.std(ddof=1)
    loa_low, loa_high = bias - 1.96 * sd, bias + 1.96 * sd
    mae = np.abs(diff).mean()
    r = np.corrcoef(dev, ref)[0, 1]

    # Bland–Altman plot
    fig, ax = plt.subplots(figsize=(7, 4.5))
    ax.scatter(mean, diff, alpha=0.75)
    ax.axhline(bias, color="black", label=f"Bias = {bias:+.2f} {unit}")
    ax.axhline(loa_high, color="gray", linestyle="--", label=f"+1.96 SD = {loa_high:+.2f}")
    ax.axhline(loa_low, color="gray", linestyle="--", label=f"−1.96 SD = {loa_low:+.2f}")
    ax.set_title(f"Bland–Altman: {name} (PulseLab vs reference, n={len(pair)})")
    ax.set_xlabel(f"Mean of both devices ({unit})")
    ax.set_ylabel(f"PulseLab − reference ({unit})")
    ax.legend(loc="best", fontsize=8)
    fig.tight_layout()
    plot_path = out_dir / f"bland_altman_{key}.png"
    fig.savefig(plot_path, dpi=150)
    plt.close(fig)

    return {
        "Metric": name,
        "n": len(pair),
        "MAE": f"{mae:.2f} {unit}",
        "Bias": f"{bias:+.2f} {unit}",
        "95% LoA": f"{loa_low:+.2f} to {loa_high:+.2f} {unit}",
        "Pearson r": f"{r:.3f}",
        "Plot": plot_path.name,
    }


def main() -> None:
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(1)

    csv_path = Path(sys.argv[1])
    df = pd.read_csv(csv_path)
    results = [r for k, n, u in METRICS if (r := analyze(df, k, n, u, csv_path.parent))]

    if not results:
        sys.exit(1)

    table = pd.DataFrame(results)
    print("\nValidation results\n")
    print(table.to_string(index=False))
    print("\nMarkdown table for your README:\n")
    try:
        print(table.to_markdown(index=False))
    except ImportError:
        print("(install 'tabulate' for a Markdown table: pip install tabulate)")
        print(table.to_string(index=False))


if __name__ == "__main__":
    main()
