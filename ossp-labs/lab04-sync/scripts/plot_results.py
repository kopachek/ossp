"""Usage: python scripts/plot_results.py"""
from pathlib import Path
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt


ROOT = Path(__file__).resolve().parent.parent
DATA_DIR = ROOT / "data"
OUT_DIR = ROOT / "report" / "figures"

plt.rcParams["figure.dpi"] = 150
plt.rcParams["savefig.dpi"] = 200
plt.rcParams["axes.grid"] = True
plt.rcParams["grid.alpha"] = 0.25


def load_csv_if_exists(filename: str) -> pd.DataFrame | None:
    path = DATA_DIR / filename
    if not path.exists():
        print(f"[INFO] Файл {path} не найден, соответствующий блок пропущен.")
        return None
    df = pd.read_csv(path)
    if df.empty:
        print(f"[WARN] Файл {path} пуст, соответствующий блок пропущен.")
        return None
    return df


def summarize(df: pd.DataFrame) -> pd.DataFrame:
    work = df.copy()
    total_ops = (work["procs"] * work["ops"]).astype(float)
    work["ns_per_op"] = np.where(total_ops > 0, (work["time_ms"] * 1e6) / total_ops, 0.0)

    grouped = (
        work.groupby(["mode", "procs", "ops"], as_index=False)
        .agg(
            runs=("ok", "count"),
            ok_runs=("ok", "sum"),
            time_mean_ms=("time_ms", "mean"),
            time_std_ms=("time_ms", "std"),
            ns_per_op_mean=("ns_per_op", "mean"),
            lost_mean=("lost", "mean"),
            violations_sum=("violations", "sum"),
        )
    )
    grouped["time_std_ms"] = grouped["time_std_ms"].fillna(0.0)
    return grouped.sort_values(["mode", "procs", "ops"]).reset_index(drop=True)


def annotate_bars(ax, bars, fmt: str = "{:.1f}"):
    for bar in bars:
        height = bar.get_height()
        if np.isfinite(height):
            ax.annotate(
                fmt.format(height),
                xy=(bar.get_x() + bar.get_width() / 2, height),
                xytext=(0, 3),
                textcoords="offset points",
                ha="center",
                va="bottom",
                fontsize=8,
            )


def plot_scaling(scaling_df: pd.DataFrame):
    summary = summarize(scaling_df)
    summary.to_csv(DATA_DIR / "summary_scaling.csv", index=False)
    print("\n--- Сводка: scaling.csv ---")
    print(summary.round(3).to_string(index=False))

    modes = [m for m in ["atomic", "mutex"] if m in summary["mode"].unique()]
    if not modes:
        return

    # График 1: Среднее время выполнения при росте числа процессов
    fig, ax_time = plt.subplots(figsize=(8, 4.5))

    for mode in modes:
        sub = summary[summary["mode"] == mode].sort_values("procs")
        ax_time.errorbar(
            sub["procs"],
            sub["time_mean_ms"],
            yerr=sub["time_std_ms"],
            marker="o",
            linewidth=2,
            capsize=4,
            label=mode,
        )

    ax_time.set_title("Среднее время выполнения при изменении числа процессов")
    ax_time.set_xlabel("Число процессов")
    ax_time.set_ylabel("Время, мс (среднее ± СКО)")
    ax_time.legend(title="Режим")

    fig.tight_layout()
    fig.savefig(OUT_DIR / "fig1_scaling_time.png")
    plt.close(fig)

    # График 2: Среднее время на одну операцию (нс) по числу процессов
    fig, ax_ns = plt.subplots(figsize=(8, 4.5))
    procs_list = sorted(summary["procs"].unique())
    x = np.arange(len(procs_list))
    width = 0.8 / max(len(modes), 1)

    for idx, mode in enumerate(modes):
        sub = summary[summary["mode"] == mode].set_index("procs").reindex(procs_list)
        offset = (idx - (len(modes) - 1) / 2) * width
        bars = ax_ns.bar(x + offset, sub["ns_per_op_mean"], width=width, label=mode)
        annotate_bars(ax_ns, bars, fmt="{:.0f}")

    ax_ns.set_xticks(x)
    ax_ns.set_xticklabels([str(p) for p in procs_list])
    ax_ns.set_title("Удельное время на одну операцию")
    ax_ns.set_xlabel("Число процессов")
    ax_ns.set_ylabel("Время на операцию, нс")
    ax_ns.legend(title="Режим")

    fig.tight_layout()
    fig.savefig(OUT_DIR / "fig2_ns_per_op.png")
    plt.close(fig)


def plot_race(race_df: pd.DataFrame):
    summary = summarize(race_df)
    summary.to_csv(DATA_DIR / "summary_race.csv", index=False)
    print("\n--- Сводка: race.csv ---")
    print(summary.round(3).to_string(index=False))

    sub = summary.sort_values("procs")
    fig, ax = plt.subplots(figsize=(7.5, 4.5))
    x = np.arange(len(sub))
    bars = ax.bar(x, sub["lost_mean"], width=0.5, color="#c0392b")
    annotate_bars(ax, bars, fmt="{:.0f}")

    ax.set_xticks(x)
    ax.set_xticklabels([str(p) for p in sub["procs"]])
    ax.set_title("Потерянные обновления счётчика без синхронизации (режим none)")
    ax.set_xlabel("Число процессов")
    ax.set_ylabel("Среднее число потерянных инкрементов")

    fig.tight_layout()
    fig.savefig(OUT_DIR / "fig3_race_lost_updates.png")
    plt.close(fig)

def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    scaling_df = load_csv_if_exists("scaling.csv")
    if scaling_df is not None:
        plot_scaling(scaling_df)

    race_df = load_csv_if_exists("race.csv")
    if race_df is not None:
        plot_race(race_df)

    print(f"\nГотово. Графики сохранены в: {OUT_DIR}")


if __name__ == "__main__":
    main()