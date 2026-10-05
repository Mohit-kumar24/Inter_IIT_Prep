"""Package Part D CSV data and generate the report's LaTeX tables."""

import csv
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent.parent
SOURCE_CSV = BASE_DIR / "analysis" / "part_d_results.csv"
REPORT_DATA = BASE_DIR / "docs" / "report" / "data"
REPORT_CSV = REPORT_DATA / "part_d_run_matrix.csv"
REPORT_TABLE = REPORT_DATA / "per_run_detail.tex"
CASE_TABLE = REPORT_DATA / "case_summary.tex"

ALGORITHM_LABELS = {
    "greedy_spt": "ECT",
    "local_search": "LS",
    "simulated_annealing": "SA",
    "tabu_search": "TS",
    "genetic_algorithm": "GA",
    "memetic_algorithm": "Memetic",
}
CASE_LABELS = {
    "bottleneck_hand": "Bottleneck hand",
    "extreme_time_gaps": "Time gaps",
    "high_flexibility": "High flexibility",
    "high_variance": "High variance",
    "identical_processing_times": "Identical times",
    "long_critical_chains": "Long chains",
    "machine_advantage_hand": "Specialization hand",
    "many_very_short_operations": "Very short ops",
    "near_total_flexibility": "Near-total flex",
    "near_zero_flexibility": "Near-zero flex",
    "one_job": "One job",
    "single_machine": "Single machine",
}


def tex_escape(value):
    replacements = {
        "\\": r"\textbackslash{}",
        "&": r"\&",
        "%": r"\%",
        "$": r"\$",
        "#": r"\#",
        "_": r"\_",
        "{": r"\{",
        "}": r"\}",
    }
    return "".join(replacements.get(char, char) for char in str(value))


def main():
    if not SOURCE_CSV.exists():
        raise FileNotFoundError(
            f"Run environment/run_part_d_experiments.py first: {SOURCE_CSV}"
        )

    with SOURCE_CSV.open(newline="", encoding="utf-8") as source:
        rows = list(csv.DictReader(source))
    if not rows:
        raise ValueError(f"No experiment rows in {SOURCE_CSV}")

    REPORT_DATA.mkdir(parents=True, exist_ok=True)
    REPORT_CSV.write_bytes(SOURCE_CSV.read_bytes())

    run_lines = [
        r"\begin{landscape}",
        r"\scriptsize",
        r"\setlength{\tabcolsep}{3pt}",
        r"\begin{longtable}{@{}p{25mm}rrrp{15mm}p{14mm}crrrrrr@{}}",
        r"\caption{Complete per-algorithm Part D measurements. Feasibility was checked by the independent C++ validator. Utilization is averaged over all machines; case labels are shortened for layout.}\\",
        r"\label{tab:all-run-metrics}\\",
        r"\toprule",
        r"Case & $J$ & $M$ & $N$ & $p_{\min}/p_{\max}$ & Solver & Valid & $C_{\max}$ & Time (s) & Gap (\%) & CP & $\bar U$ (\%) & $U_{\max}$ (\%)\\",
        r"\midrule",
        r"\endfirsthead",
        r"\toprule",
        r"Case & $J$ & $M$ & $N$ & $p_{\min}/p_{\max}$ & Solver & Valid & $C_{\max}$ & Time (s) & Gap (\%) & CP & $\bar U$ (\%) & $U_{\max}$ (\%)\\",
        r"\midrule",
        r"\endhead",
    ]
    for row in rows:
        values = [
            tex_escape(CASE_LABELS.get(row["instance"], row["instance"])),
            row["jobs"],
            row["machines"],
            row["operations"],
            f"{float(row['processing_time_min']):g}/{float(row['processing_time_max']):g}",
            ALGORITHM_LABELS.get(row["algorithm"], tex_escape(row["algorithm"])),
            "Y" if row["feasible"].lower() == "true" else "N",
            row["makespan"] or "---",
            row["runtime_seconds"] or "---",
            row["lower_bound_gap_percent"] or "---",
            row["critical_path_length"] or "---",
            row["machine_utilization_mean_percent"] or "---",
            row["machine_utilization_max_percent"] or "---",
        ]
        run_lines.append(" & ".join(values) + r" \\")
    run_lines.extend([
        r"\bottomrule",
        r"\end{longtable}",
        r"\end{landscape}",
        "",
    ])
    REPORT_TABLE.write_text("\n".join(run_lines), encoding="utf-8", newline="\n")

    grouped = {}
    for row in rows:
        grouped.setdefault(row["instance"], []).append(row)
    case_lines = [
        r"\begin{landscape}",
        r"\scriptsize",
        r"\setlength{\tabcolsep}{4pt}",
        r"\begin{longtable}{@{}lrrrp{13mm}p{16mm}clrrrr@{}}",
        r"\caption{Instance characteristics and best validated result per case. Runtime belongs to the fastest solver among tied best methods; case labels are shortened for layout.}\\",
        r"\label{tab:case-summary}\\",
        r"\toprule",
        r"Case & $J$ & $M$ & $N$ & Mean flex. & $p_{\min}/p_{\max}$ & Valid & Best method(s) & $C_{\max}$ & LB & Gap (\%) & Time (s)\\",
        r"\midrule",
        r"\endfirsthead",
        r"\toprule",
        r"Case & $J$ & $M$ & $N$ & Mean flex. & $p_{\min}/p_{\max}$ & Valid & Best method(s) & $C_{\max}$ & LB & Gap (\%) & Time (s)\\",
        r"\midrule",
        r"\endhead",
    ]
    for name, case_rows in grouped.items():
        valid_rows = [row for row in case_rows if row["feasible"].lower() == "true"]
        best_makespan = min(float(row["makespan"]) for row in valid_rows)
        winners = [
            row for row in valid_rows
            if float(row["makespan"]) == best_makespan
        ]
        fastest_winner = min(winners, key=lambda row: float(row["runtime_seconds"]))
        labels = "/".join(
            ALGORITHM_LABELS.get(row["algorithm"], tex_escape(row["algorithm"]))
            for row in winners
        )
        summary = [
            tex_escape(CASE_LABELS.get(name, name)),
            fastest_winner["jobs"],
            fastest_winner["machines"],
            fastest_winner["operations"],
            f"{float(fastest_winner['mean_flexibility']):.3f}",
            (
                f"{float(fastest_winner['processing_time_min']):g}/"
                f"{float(fastest_winner['processing_time_max']):g}"
            ),
            f"{len(valid_rows)}/{len(case_rows)}",
            labels,
            f"{best_makespan:g}",
            f"{float(fastest_winner['lower_bound']):g}",
            f"{float(fastest_winner['lower_bound_gap_percent']):.2f}",
            f"{float(fastest_winner['runtime_seconds']):.3f}",
        ]
        case_lines.append(" & ".join(summary) + r" \\")
    case_lines.extend([
        r"\bottomrule",
        r"\end{longtable}",
        r"\end{landscape}",
        "",
    ])
    CASE_TABLE.write_text("\n".join(case_lines), encoding="utf-8", newline="\n")
    print(f"Packaged {len(rows)} rows in {REPORT_CSV.relative_to(BASE_DIR)}")
    print(f"Generated {REPORT_TABLE.relative_to(BASE_DIR)}")
    print(f"Generated {CASE_TABLE.relative_to(BASE_DIR)}")


if __name__ == "__main__":
    main()
