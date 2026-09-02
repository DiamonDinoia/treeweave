#!/usr/bin/env python3
"""Generate the cross-language zeta benchmark charts from per-language YAML.

Each benchmark in the zeta family (one per binding: C, C++, Fortran, Python,
Julia, Octave, JS) writes a YAML file when the ``TREEWEAVE_BENCH_YAML``
environment variable names a path; see benchmarks/zeta_bench.cpp for the
schema. This script loads every ``*.yaml`` in ``--results-dir``, keyed by each
file's own ``language`` field and not by its path, then emits a family of SVG bar
charts to ``--output-dir``.

Two metrics are charted, both derived from the same per-mode YAML block
(``{treeweave_mevals_s, native_mevals_s, speedup}``):

    throughput  Mevals/s, as measured (higher is better)
    latency     ns per eval = 1000 / Mevals_s (lower is better)

and in two framings:

    treeweave vs native   grouped bars per language (the headline comparison)
    batch vs sorted batch treeweave's plain batch vs its sorted fast path per language

The bars are **horizontal** (languages on the y-axis): the language set grows
downward as new wrappers are added, with no x-axis crowding. The x-axis is
log-scaled, because throughput spans orders of magnitude across a CI matrix that
spreads languages over different runners, but each *pair* of bars within one
language is measured in the same process, so the within-language comparison is
the meaningful one (absolute Mevals/s across languages is not). On the
treeweave-vs-native charts the absolute metric is read off the axis, so each bar
is labelled with the within-language **speedup** instead (native = 1× baseline,
treeweave = N×); the throughput chart also marks the 1 Meval/s line.

The README embeds the three treeweave-vs-native throughput charts (single,
batch, sorted). Even a minimal native Riemann-zeta eval is tens-to-hundreds of
pow()s, so treeweave wins in every mode; the docs guide embeds the fuller latency
and batch-vs-sorted set.

Style mirrors poet's scripts/generate_charts.py (Agg backend, hidden top/right
spines, per-bar value labels, SVG output).

Usage:
    python3 scripts/generate_benchmark_plot.py --results-dir bench_results --output-dir charts
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402  (must follow matplotlib.use)
import yaml  # noqa: E402

# ── Styling ──────────────────────────────────────────────────────────────────

# Fixed display order (top-to-bottom) across all charts. The plot draws only the
# languages present in the results; a missing one leaves no gap, since a log
# x-axis has no meaningful zero bar.
LANGUAGE_ORDER = ["c", "c++", "fortran", "python", "julia", "octave", "js"]

LANGUAGE_LABELS = {
    "c": "C",
    "c++": "C++",
    "fortran": "Fortran",
    "python": "Python",
    "julia": "Julia",
    "octave": "Octave",
    "js": "JS",
}

# seaborn "deep" palette (the family poet uses). Two stable colours for the
# treeweave-vs-native framing, two more for the sorted-vs-unsorted framing.
C_TREEWEAVE = "#4c72b0"  # blue
C_NATIVE = "#dd8452"  # orange
C_SORTED = "#55a868"  # green
C_UNSORTED = "#c44e52"  # red

MODES = {
    "single_eval": "single-eval (scalar API, one point at a time)",
    "multi_eval": "batch (many points in one call, any order)",
    "sorted_eval": "sorted batch (1-D ascending fast path)",
}


def _ns_per_eval(mevals_s: float) -> float:
    """ns/eval from Mevals/s: 1 Meval/s = 1e6 eval/s = 1000 ns/eval."""
    return 1000.0 / mevals_s if mevals_s > 0 else float("nan")


# ── Data loading ─────────────────────────────────────────────────────────────


def load_results(results_dir: Path) -> dict[str, dict]:
    """Load every ``*.yaml`` in results_dir, keyed by its own ``language`` field."""
    records: dict[str, dict] = {}
    for path in sorted(results_dir.glob("*.yaml")):
        try:
            doc = yaml.safe_load(path.read_text())
        except (yaml.YAMLError, OSError) as exc:
            print(f"Warning: failed to load {path}: {exc}", file=sys.stderr)
            continue
        if not isinstance(doc, dict) or "language" not in doc:
            print(f"Warning: {path} has no 'language' field; skipping", file=sys.stderr)
            continue
        records[str(doc["language"]).lower()] = doc
    return records


def _present(records: dict[str, dict], *mode_keys: str) -> list[str]:
    """Languages (in display order) that carry every requested mode block."""
    out = []
    for lang in LANGUAGE_ORDER:
        rec = records.get(lang)
        if rec and all(isinstance(rec.get(k), dict) for k in mode_keys):
            out.append(lang)
    return out


# ── Chart primitive ──────────────────────────────────────────────────────────


def _fmt(value: float, unit: str) -> str:
    if unit == "mevals":
        return f"{value:,.0f}" if value >= 100 else f"{value:.1f}"
    if unit == "speedup":
        return f"{value:,.0f}×" if value >= 10 else f"{value:.1f}×"
    return f"{value:.2f}" if value < 10 else f"{value:.1f}"


def grouped_barh(
    out: Path,
    langs: list[str],
    series: list[tuple[str, list[float], str]],
    title: str,
    xlabel: str,
    unit: str,
    bar_labels: list[list[str]] | None = None,
    vline: tuple[float, str] | None = None,
) -> None:
    """One horizontal grouped bar chart (languages on the y-axis, log x-axis).

    ``series`` is a list of (legend label, per-language values, colour). Bars are
    annotated with ``_fmt(value, unit)`` by default; pass ``bar_labels`` (one
    string list per series, parallel to ``langs``) to label them with something
    other than the bar's own value, e.g. the speedup ``×`` while the axis still
    shows absolute throughput. ``vline`` draws a labelled reference line at the
    given x.
    """
    n_lang = len(langs)
    n_series = len(series)
    fig, ax = plt.subplots(figsize=(8.0, max(3.0, 0.7 * n_lang + 1.4)))

    positions = list(range(n_lang))
    bar_h = 0.8 / n_series
    for i, (label, values, color) in enumerate(series):
        # Stack the series within each language slot, top series first.
        offsets = [
            p + (n_series - 1 - i - (n_series - 1) / 2) * bar_h for p in positions
        ]
        bars = ax.barh(offsets, values, height=bar_h, label=label, color=color)
        for j, (bar, value) in enumerate(zip(bars, values)):
            if value > 0:
                text = bar_labels[i][j] if bar_labels else _fmt(value, unit)
                ax.text(
                    value * 1.03,
                    bar.get_y() + bar.get_height() / 2,
                    text,
                    va="center",
                    ha="left",
                    fontsize=8,
                )

    ax.set_xscale("log")
    if vline is not None:
        x, vlabel = vline
        ax.axvline(
            x, color="0.4", linewidth=0.9, linestyle="--", zorder=0, label=vlabel
        )
    ax.set_yticks(positions)
    ax.set_yticklabels([LANGUAGE_LABELS[lang] for lang in langs])
    ax.invert_yaxis()  # first language at the top
    ax.set_xlabel(xlabel)
    ax.set_title(title, fontsize=13, fontweight="bold", pad=12)
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    ax.tick_params(axis="both", which="both", labelsize=9)
    ax.margins(x=0.18)  # headroom for the value labels at the bar ends
    # Legend outside, to the right: never collides with a bar's value label, and
    # stays put as the language list (and so the chart height) grows.
    ax.legend(loc="center left", bbox_to_anchor=(1.0, 0.5), frameon=False, fontsize=9)

    fig.tight_layout()
    fig.savefig(str(out), bbox_inches="tight")  # format inferred from the suffix
    plt.close(fig)
    print(f"  Wrote {out}")


# ── Chart builders ───────────────────────────────────────────────────────────


def chart_tw_vs_native(records, mode: str, metric: str, out: Path) -> None:
    """treeweave vs its native baseline, one grouped pair per language."""
    langs = _present(records, mode)
    if not langs:
        return
    tw_vals, nat_vals, speedups = [], [], []
    for lang in langs:
        block = records[lang][mode]
        tw, nat = float(block["treeweave_mevals_s"]), float(block["native_mevals_s"])
        speedups.append(tw / nat if nat > 0 else float("nan"))
        if metric == "throughput":
            tw_vals.append(tw)
            nat_vals.append(nat)
        else:  # latency
            tw_vals.append(_ns_per_eval(tw))
            nat_vals.append(_ns_per_eval(nat))

    # The axis carries the absolute metric; label the bars with the within-language
    # speedup instead (native = 1× baseline, treeweave = ×), identical for both
    # framings, since the latency ratio is the throughput ratio.
    bar_labels = [
        [_fmt(s, "speedup") for s in speedups],
        ["1×"] * len(langs),
    ]
    vline = None
    if metric == "throughput":
        xlabel, unit, sense = (
            "Throughput (Mevals/s, log scale)",
            "mevals",
            "higher is better",
        )
        title = f"Riemann-zeta {MODES[mode]}\nthroughput, {sense}"
        vline = (1.0, "1 Meval/s")
    else:
        xlabel, unit, sense = "Latency (ns/eval, log scale)", "ns", "lower is better"
        title = f"Riemann-zeta {MODES[mode]}\nlatency, {sense}"

    grouped_barh(
        out,
        langs,
        [("treeweave", tw_vals, C_TREEWEAVE), ("native", nat_vals, C_NATIVE)],
        title,
        xlabel,
        unit,
        bar_labels=bar_labels,
        vline=vline,
    )


def chart_sorted_vs_unsorted(records, metric: str, out: Path) -> None:
    """treeweave's plain batch vs its sorted-batch fast path, per language.

    The plain ``batch`` path counting-sorts the query points by leaf, then
    evaluates; the ``sorted batch`` path takes points already in ascending order
    (1-D only) and streams straight through the leaves, skipping that sort.
    """
    langs = _present(records, "multi_eval", "sorted_eval")
    if not langs:
        return
    uns_vals, srt_vals = [], []
    for lang in langs:
        uns = float(records[lang]["multi_eval"]["treeweave_mevals_s"])
        srt = float(records[lang]["sorted_eval"]["treeweave_mevals_s"])
        if metric == "throughput":
            uns_vals.append(uns)
            srt_vals.append(srt)
        else:
            uns_vals.append(_ns_per_eval(uns))
            srt_vals.append(_ns_per_eval(srt))

    if metric == "throughput":
        xlabel, unit, sense = (
            "Throughput (Mevals/s, log scale)",
            "mevals",
            "higher is better",
        )
        title = f"treeweave batch vs sorted batch\nthroughput, {sense}"
    else:
        xlabel, unit, sense = "Latency (ns/eval, log scale)", "ns", "lower is better"
        title = f"treeweave batch vs sorted batch\nlatency, {sense}"

    grouped_barh(
        out,
        langs,
        [("sorted batch", srt_vals, C_SORTED), ("batch", uns_vals, C_UNSORTED)],
        title,
        xlabel,
        unit,
    )


# README embeds the three throughput tw-vs-native charts (single/multi/sorted);
# the rest are for the docs performance guide.
def build_all(records, output_dir: Path) -> None:
    # treeweave vs native: throughput + latency, per mode.
    for mode in ("single_eval", "multi_eval", "sorted_eval"):
        short = mode.replace("_eval", "")
        chart_tw_vs_native(
            records, mode, "throughput", output_dir / f"throughput_{short}.svg"
        )
        chart_tw_vs_native(
            records, mode, "latency", output_dir / f"latency_{short}.svg"
        )
    # treeweave's plain batch vs its sorted-batch fast path.
    chart_sorted_vs_unsorted(
        records, "throughput", output_dir / "sorted_vs_unsorted_throughput.svg"
    )
    chart_sorted_vs_unsorted(
        records, "latency", output_dir / "sorted_vs_unsorted_latency.svg"
    )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate cross-language zeta benchmark charts"
    )
    parser.add_argument(
        "--results-dir",
        default="bench_results",
        help="directory of per-language *.yaml files",
    )
    parser.add_argument(
        "--output-dir", default="charts", help="directory to write the SVG charts into"
    )
    args = parser.parse_args()

    results_dir = Path(args.results_dir)
    output_dir = Path(args.output_dir)
    if not results_dir.is_dir():
        print(f"Error: results dir not found: {results_dir}", file=sys.stderr)
        sys.exit(1)
    output_dir.mkdir(parents=True, exist_ok=True)

    records = load_results(results_dir)
    if not records:
        print(f"Error: no usable *.yaml found in {results_dir}", file=sys.stderr)
        sys.exit(1)
    print(f"Loaded languages: {', '.join(sorted(records))}")

    build_all(records, output_dir)
    print("Done.")


if __name__ == "__main__":
    main()
