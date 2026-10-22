"""
PulseLab - Validation analysis (Phase 11)

Compares PulseLab readings with a reference pulse oximeter, and calibrates
the SpO2 formula (SpO2 = A - B * R) on one part of the readings while testing
it on the other part.

What it reports:
  - Heart rate and SpO2 agreement: MAE, bias, 95% limits of agreement
    (Bland-Altman), ARMS (root mean square error, what oximeter standards use),
    Pearson r (reported, but not a measure of agreement on its own)
  - SpO2 calibration fitted on the 'cal' readings only:
      * offset only:  A fitted, B kept at 25 (needs little SpO2 range)
      * full fit:     A and B both fitted (needs a wide SpO2 range)
  - Both calibrations tested on the 'test' readings, which were NOT used to fit

Usage:
    pip install pandas matplotlib numpy tabulate
    python analyze.py readings.csv

CSV columns (see readings_template.csv):
    reading_id, subject, condition, set, pulselab_hr, reference_hr,
    pulselab_spo2, reference_spo2, pulselab_r, notes
  - set:        'cal' or 'test'. If the column is empty or missing, the first
                60% of rows (in file order) are 'cal' and the rest 'test'.
  - pulselab_r: the R value from the Serial Monitor beat line ("R 0.481") at the
                moment of the reading. If it is empty, R is worked back from
                pulselab_spo2 with the current formula (only valid below 100%,
                since the Live screen caps SpO2 at 100).
  - Lines starting with '#' are ignored.

Saves Bland-Altman plots and results.md next to the CSV.
"""

import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

# Current constants in firmware/08_ble/spo2.h
SPO2_A = 110.0
SPO2_B = 25.0
CAL_FRACTION = 0.6       # used when the 'set' column is empty
MIN_RANGE_FULL_FIT = 5.0  # reference SpO2 range (points) needed to trust a fitted slope
SIMULATED = False         # set when the CSV says it's simulated; plots get a watermark


def stats(dev: np.ndarray, ref: np.ndarray) -> dict:
    diff = dev - ref
    bias = diff.mean()
    sd = diff.std(ddof=1) if len(diff) > 1 else float("nan")
    return {
        "n": len(diff),
        "mae": np.abs(diff).mean(),
        "bias": bias,
        "loa_low": bias - 1.96 * sd,
        "loa_high": bias + 1.96 * sd,
        "arms": np.sqrt((diff ** 2).mean()),
        "r": np.corrcoef(dev, ref)[0, 1] if len(diff) > 2 and np.std(dev) > 0 and np.std(ref) > 0 else float("nan"),
    }


def bland_altman(dev, ref, title, unit, path: Path) -> None:
    diff = dev - ref
    mean = (dev + ref) / 2
    s = stats(dev, ref)
    fig, ax = plt.subplots(figsize=(7, 4.5))
    ax.scatter(mean, diff, alpha=0.75)
    ax.axhline(s["bias"], color="black", label=f"Bias = {s['bias']:+.2f} {unit}")
    ax.axhline(s["loa_high"], color="gray", linestyle="--", label=f"+1.96 SD = {s['loa_high']:+.2f}")
    ax.axhline(s["loa_low"], color="gray", linestyle="--", label=f"-1.96 SD = {s['loa_low']:+.2f}")
    ax.set_title(f"{title} (n={s['n']})")
    ax.set_xlabel(f"Mean of both devices ({unit})")
    ax.set_ylabel(f"PulseLab - reference ({unit})")
    ax.legend(loc="best", fontsize=8)
    if SIMULATED:
        ax.text(0.5, 0.5, "SIMULATED DATA", transform=ax.transAxes, ha="center", va="center",
                fontsize=30, color="red", alpha=0.25, rotation=20)
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    plt.close(fig)


def row(name: str, s: dict, unit: str, plot: str = "") -> dict:
    return {
        "What": name,
        "n": s["n"],
        "MAE": f"{s['mae']:.2f} {unit}",
        "Bias": f"{s['bias']:+.2f} {unit}",
        "95% LoA": f"{s['loa_low']:+.2f} to {s['loa_high']:+.2f}",
        "ARMS": f"{s['arms']:.2f} {unit}",
        "Pearson r": "" if np.isnan(s["r"]) else f"{s['r']:.3f}",
        "Plot": plot,
    }


def table_md(rows: list[dict]) -> str:
    df = pd.DataFrame(rows)
    df = df.loc[:, [c for c in df.columns if (df[c].astype(str) != "").any()]]  # drop empty columns
    try:
        return df.to_markdown(index=False)
    except ImportError:
        return df.to_string(index=False)


def load(csv_path: Path) -> pd.DataFrame:
    df = pd.read_csv(csv_path, comment="#", skipinitialspace=True)
    for c in ["pulselab_hr", "reference_hr", "pulselab_spo2", "reference_spo2", "pulselab_r"]:
        if c not in df.columns:
            df[c] = np.nan
        df[c] = pd.to_numeric(df[c], errors="coerce")

    # Split into calibration and test readings
    if "set" not in df.columns or df["set"].isna().all():
        n_cal = int(round(len(df) * CAL_FRACTION))
        df["set"] = ["cal"] * n_cal + ["test"] * (len(df) - n_cal)
        print(f"No 'set' column filled in: first {n_cal} rows = cal, last {len(df) - n_cal} = test.")
    df["set"] = df["set"].astype(str).str.strip().str.lower()

    # R from the Serial line, or worked back from the shown SpO2
    missing_r = df["pulselab_r"].isna() & df["pulselab_spo2"].notna()
    if missing_r.any():
        capped = missing_r & (df["pulselab_spo2"] >= 100)
        df.loc[missing_r & ~capped, "pulselab_r"] = (SPO2_A - df["pulselab_spo2"]) / SPO2_B
        print(f"pulselab_r missing in {int(missing_r.sum())} rows: worked back from pulselab_spo2"
              + (f" ({int(capped.sum())} rows at 100% skipped, R unknown)" if capped.any() else "") + ".")
    return df


def main() -> None:
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(1)

    global SIMULATED
    csv_path = Path(sys.argv[1])
    out = csv_path.parent
    df = load(csv_path)
    md = [f"# Phase 11 results: `{csv_path.name}`", ""]
    SIMULATED = "simulated" in csv_path.read_text(encoding="utf-8", errors="ignore").lower()
    if SIMULATED:
        md += ["> **SIMULATED DATA - not real measurements.** Only for checking the script.", ""]

    # 1. Agreement as measured (current firmware formula)
    rows = []
    hr = df[["pulselab_hr", "reference_hr"]].dropna()
    if len(hr) >= 2:
        bland_altman(hr.pulselab_hr.to_numpy(), hr.reference_hr.to_numpy(),
                     "Bland-Altman: heart rate, PulseLab vs reference", "BPM", out / "bland_altman_hr.png")
        rows.append(row("Heart rate (all readings)", stats(hr.pulselab_hr.to_numpy(), hr.reference_hr.to_numpy()),
                        "BPM", "bland_altman_hr.png"))
    sp = df[["pulselab_spo2", "reference_spo2"]].dropna()
    if len(sp) >= 2:
        dev = np.minimum(sp.pulselab_spo2.to_numpy(), 100.0)  # as shown on the screen
        bland_altman(dev, sp.reference_spo2.to_numpy(),
                     f"Bland-Altman: SpO2, uncalibrated ({SPO2_A:g} - {SPO2_B:g}R)", "%", out / "bland_altman_spo2_uncal.png")
        rows.append(row("SpO2, uncalibrated (all readings)", stats(dev, sp.reference_spo2.to_numpy()),
                        "%", "bland_altman_spo2_uncal.png"))
    if not rows:
        print("Not enough paired readings (need at least 2).")
        sys.exit(1)

    md += ["## Agreement with the reference (as measured)", "", table_md(rows), ""]
    cond_rows = []
    groups = df.dropna(subset=["pulselab_hr", "reference_hr"]).groupby("condition") if "condition" in df.columns else []
    for cond, g in groups:
        if len(g) >= 2:
            cond_rows.append(row(f"Heart rate: {cond}", stats(g.pulselab_hr.to_numpy(), g.reference_hr.to_numpy()), "BPM"))
    if len(cond_rows) > 1:
        md += ["### Heart rate by condition", "", table_md(cond_rows), ""]

    # 2. SpO2 calibration on 'cal', tested on 'test'
    d = df.dropna(subset=["pulselab_r", "reference_spo2"])
    cal, test = d[d.set == "cal"], d[d.set == "test"]
    md += ["## SpO2 calibration", ""]
    if len(cal) < 3 or len(test) < 2:
        md += [f"Not enough readings with R and reference SpO2 (cal {len(cal)}, test {len(test)}; need 3 and 2).", ""]
    else:
        R, ref = cal.pulselab_r.to_numpy(), cal.reference_spo2.to_numpy()
        a_off = float(np.mean(ref + SPO2_B * R))
        slope, intercept = np.polyfit(R, ref, 1)
        a_full, b_full = float(intercept), float(-slope)
        ref_range = ref.max() - ref.min()

        Rt, reft = test.pulselab_r.to_numpy(), test.reference_spo2.to_numpy()
        models = {
            f"Textbook ({SPO2_A:g} - {SPO2_B:g}R)": (SPO2_A, SPO2_B),
            f"Offset only ({a_off:.1f} - {SPO2_B:g}R)": (a_off, SPO2_B),
            f"Full fit ({a_full:.1f} - {b_full:.1f}R)": (a_full, b_full),
        }
        test_rows = []
        for name, (a, b) in models.items():
            pred = np.minimum(a - b * Rt, 100.0)
            s = stats(pred, reft)
            r_ = row(name, s, "%")
            r_["Within 2%"] = f"{100 * np.mean(np.abs(pred - reft) <= 2):.0f}%"
            test_rows.append(r_)
        use_full = ref_range >= MIN_RANGE_FULL_FIT and 10 <= b_full <= 40
        a_use, b_use = (a_full, b_full) if use_full else (a_off, SPO2_B)
        pred = np.minimum(a_use - b_use * Rt, 100.0)
        bland_altman(pred, reft, f"Bland-Altman: SpO2 calibrated, test readings ({a_use:.1f} - {b_use:.1f}R)",
                     "%", out / "bland_altman_spo2_cal.png")

        md += [
            f"Fitted on {len(cal)} 'cal' readings (reference SpO2 {ref.min():g}-{ref.max():g}%, "
            f"R {R.min():.3f}-{R.max():.3f}), tested on {len(test)} 'test' readings that were not used for fitting.",
            "",
            table_md(test_rows),
            "",
            f"Recommended: **{'full fit' if use_full else 'offset only'}** "
            + ("" if use_full else f"(reference range only {ref_range:g} points, so the slope can't be trusted; "
               f"need at least {MIN_RANGE_FULL_FIT:g})") ,
            "",
            "Put this in `firmware/08_ble/spo2.h`:",
            "",
            "```cpp",
            f"constexpr float   SPO2_A           = {a_use:.1f}f;",
            f"constexpr float   SPO2_B           = {b_use:.1f}f;",
            "```",
            "",
            "Plot: `bland_altman_spo2_cal.png`",
            "",
        ]

    text = "\n".join(md)
    (out / "results.md").write_text(text, encoding="utf-8")
    print(text)
    print(f"\nSaved: {out / 'results.md'} and the Bland-Altman PNGs.")


if __name__ == "__main__":
    main()
