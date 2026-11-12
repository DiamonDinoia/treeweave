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

A third framing compares treeweave with each language's native interpolator
(numpy Chebyshev, FastChebInterp.jl, Boost.Math, GSL, Octave spline, and
baobzi in C), read from the optional ``interp`` list in each YAML. Every
baseline is grown until it meets the same tolerance before it is timed. With
``--readme`` the script also rewrites the README blocks between the
``<!-- bench-summary -->`` and ``<!-- bench-table -->`` marker pairs.

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
        if value < 0.1:  # interpreted per-point loops; .1f would print 0.0
            return f"{value:.0e}"
        return f"{value:,.0f}" if value >= 100 else f"{value:.1f}"
    if unit == "speedup":
        if value < 1:  # .1f would print 0.0× for a 0.04× slowdown
            return f"{value:.2g}×"
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


# ── Native interpolators ─────────────────────────────────────────────────────

C_BAOBZI = "#8172b3"  # purple

# Languages whose bench carries an interpolator baseline. Fortran and JS have no
# interpolator in common use, so their YAML has no ``interp`` list.
INTERP_LANGUAGES = ["c", "c++", "python", "julia", "octave"]


def require_all_interp(records) -> None:
    """The README summary needs every interpolator language; a failed bench job
    must not rewrite it from a subset."""
    missing = [lang for lang in INTERP_LANGUAGES if lang not in records]
    if missing:
        sys.exit(f"Error: no result for {', '.join(missing)}; README left unchanged")


def interp_rows(records) -> list[tuple[str, dict, list[dict]]]:
    """(language, treeweave record, baselines) for every language with baselines.

    A language in INTERP_LANGUAGES whose YAML is present but has no baseline is a
    broken bench, not a quiet one: fail loudly.
    """
    rows = []
    for lang in INTERP_LANGUAGES:
        rec = records.get(lang)
        if rec is None:
            continue
        baselines = rec.get("interp") or []
        if not baselines:
            sys.exit(f"Error: {lang} result has no 'interp' baselines")
        rows.append((lang, rec, baselines))
    return rows


def _speedup(tw: float, other: float) -> str:
    """treeweave over another method, e.g. '12×' or '0.5×'."""
    return _fmt(tw / other, "speedup")


def chart_interp(records, mode: str, out: Path) -> None:
    """treeweave vs every native interpolator, one row per (language, method)."""
    labels, values, colors, texts = [], [], [], []
    for lang, rec, baselines in interp_rows(records):
        tw = float(rec[mode]["treeweave_mevals_s"])
        labels.append(f"{LANGUAGE_LABELS[lang]}: treeweave")
        values.append(tw)
        colors.append(C_TREEWEAVE)
        texts.append(_fmt(tw, "mevals"))
        for bl in baselines:
            rate = float(bl[mode])
            labels.append(f"{LANGUAGE_LABELS[lang]}: {bl['name']}")
            values.append(rate)
            colors.append(C_BAOBZI if bl["name"] == "baobzi" else C_NATIVE)
            texts.append(f"{_fmt(rate, 'mevals')}  (treeweave {_speedup(tw, rate)})")
    if not labels:
        return
    fig, ax = plt.subplots(figsize=(9.0, max(3.0, 0.42 * len(labels) + 1.4)))
    bars = ax.barh(range(len(labels)), values, color=colors, height=0.7)
    for bar, value, text in zip(bars, values, texts):
        ax.text(
            value * 1.05,
            bar.get_y() + bar.get_height() / 2,
            text,
            va="center",
            fontsize=8,
        )
    ax.set_xscale("log")
    ax.set_yticks(range(len(labels)))
    ax.set_yticklabels(labels)
    ax.invert_yaxis()
    ax.set_xlabel("Throughput (Mevals/s, log scale)")
    ax.set_title(
        f"Riemann-zeta {MODES[mode]}\ntreeweave vs native interpolators, higher is better",
        fontsize=13,
        fontweight="bold",
        pad=12,
    )
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    ax.tick_params(axis="both", which="both", labelsize=9)
    ax.margins(x=0.45)  # headroom for the speedup labels
    fig.tight_layout()
    fig.savefig(str(out), bbox_inches="tight")
    plt.close(fig)
    print(f"  Wrote {out}")


def summary_markdown(records) -> tuple[str, str]:
    """(one-paragraph headline, results table) in GitHub markdown."""
    rows = interp_rows(records)
    if not rows:
        return "", ""
    native, baobzi = {}, {}  # mode -> [treeweave / baseline]
    table = [
        "| Language | Method | Size | Max err / max\\|f\\| | Max rel err | Single (Mevals/s) | Batch (Mevals/s) | Sorted batch (Mevals/s) |",
        "|---|---|---|---|---|---|---|---|",
    ]
    for lang, rec, baselines in rows:
        tw = {m: float(rec[m]["treeweave_mevals_s"]) for m in MODES}
        table.append(
            f"| {LANGUAGE_LABELS[lang]} | treeweave | tol {float(rec['tol']):.0e} | "
            f"{float(rec['relmax_err']):.1e} | {float(rec['max_rel_err']):.1e} | "
            + " | ".join(_fmt(tw[m], "mevals") for m in MODES)
            + " |"
        )
        for bl in baselines:
            cells = []
            for m in MODES:
                rate = float(bl[m])
                (baobzi if bl["name"] == "baobzi" else native).setdefault(m, []).append(
                    tw[m] / rate
                )
                cells.append(f"{_fmt(rate, 'mevals')} ({_speedup(tw[m], rate)})")
            table.append(
                f"| {LANGUAGE_LABELS[lang]} | {bl['name']} | {bl['size']} | "
                f"{float(bl['relmax_err']):.1e} | {float(bl['max_rel_err']):.1e} | "
                + " | ".join(cells)
                + " |"
            )

    def span(ratios: list[float]) -> str:
        lo, hi = min(ratios), max(ratios)
        return (
            _fmt(lo, "speedup")
            if f"{lo:.1f}" == f"{hi:.1f}"
            else f"{_fmt(lo, 'speedup')} to {_fmt(hi, 'speedup')}"
        )

    names = ", ".join(
        dict.fromkeys(
            bl["name"] for _, _, bls in rows for bl in bls if bl["name"] != "baobzi"
        )
    )
    lines = [
        f"Against the native interpolator of each language ({names}), each grown until it "
        f"meets the same tolerance, treeweave's throughput is {span(native['multi_eval'])} "
        f"theirs in batch, {span(native['sorted_eval'])} on sorted batches, and "
        f"{span(native['single_eval'])} one point at a time."
    ]
    if baobzi:
        lines.append(
            f"Against baobzi, the library that inspired treeweave, from C: {span(baobzi['multi_eval'])} in batch, "
            f"{span(baobzi['sorted_eval'])} on sorted batches, {span(baobzi['single_eval'])} one point at a time."
        )
    lines.append(
        "The benchmark-showcase CI workflow writes these numbers; see [Benchmarks](#benchmarks)."
    )
    return " ".join(lines), "\n".join(table)


def update_readme(readme: Path, summary: str, table: str) -> None:
    """Replace the text between each marker pair; a missing pair is an error."""
    text = readme.read_text()
    for tag, body in (("bench-summary", summary), ("bench-table", table)):
        begin, end = f"<!-- {tag} -->", f"<!-- /{tag} -->"
        i, j = text.find(begin), text.find(end)
        if i < 0 or j < i:
            sys.exit(f"Error: {readme} has no {begin} ... {end} block")
        text = text[: i + len(begin)] + "\n" + body + "\n" + text[j:]
    readme.write_text(text)
    print(f"  Updated {readme}")


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
    # treeweave vs each language's native interpolator.
    for mode in MODES:
        chart_interp(
            records, mode, output_dir / f"interp_{mode.replace('_eval', '')}.svg"
        )


def self_test() -> None:
    """Positive controls for the README guards, on synthetic results."""
    import tempfile

    def rec(lang: str) -> dict:
        modes = {
            m: {"treeweave_mevals_s": 100.0, "native_mevals_s": 1.0} for m in MODES
        }
        bl = {
            "name": "x",
            "size": "degree 9",
            "relmax_err": 1e-11,
            "max_rel_err": 2e-11,
        }
        return {
            "language": lang,
            "tol": 1e-10,
            "relmax_err": 5e-11,
            "max_rel_err": 9e-11,
            **modes,
            "interp": [{**bl, **{m: 10.0 for m in MODES}}],
        }

    def exits(fn, *args) -> bool:
        try:
            fn(*args)
        except SystemExit as exc:
            return exc.code not in (None, 0)
        return False

    full = {lang: rec(lang) for lang in INTERP_LANGUAGES}
    summary, table = summary_markdown(full)
    assert "10× to 10×" not in summary and "10×" in summary, summary
    assert table.count("\n") == 1 + 2 * len(INTERP_LANGUAGES), table
    print("PASS: summary and table built from complete results")
    assert _fmt(0.04, "speedup") == "0.04×" and _fmt(3.04, "speedup") == "3.0×"
    print("PASS: a sub-1 speedup keeps two significant digits")

    no_interp = {**full, "c": {**full["c"], "interp": []}}
    assert exits(interp_rows, no_interp)
    print("PASS: a result with no interp baselines fails")

    partial = {k: v for k, v in full.items() if k != "julia"}
    assert exits(require_all_interp, partial)
    print("PASS: a missing language blocks the README rewrite")

    with tempfile.TemporaryDirectory() as tmp:
        readme = Path(tmp) / "README.md"
        readme.write_text("# x\n")
        assert exits(update_readme, readme, summary, table)
        print("PASS: a README without the markers fails")
        readme.write_text(
            "a\n<!-- bench-summary -->\nold\n<!-- /bench-summary -->\n"
            "b\n<!-- bench-table -->\nold\n<!-- /bench-table -->\nc\n"
        )
        update_readme(readme, summary, table)
        once = readme.read_text()
        update_readme(readme, summary, table)
        assert readme.read_text() == once and "old" not in once and once.endswith("c\n")
        print("PASS: the README rewrite replaces both blocks and is idempotent")


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
    parser.add_argument(
        "--self-test", action="store_true", help="run the guard self-test and exit"
    )
    parser.add_argument(
        "--readme",
        help="README to rewrite between the bench-summary/bench-table markers",
    )
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return

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
    summary, table = summary_markdown(records)
    (output_dir / "summary.md").write_text(summary + "\n\n" + table + "\n")
    if args.readme:
        require_all_interp(records)
        update_readme(Path(args.readme), summary, table)
    print("Done.")


if __name__ == "__main__":
    main()
