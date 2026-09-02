"""Compare treeweave against the interpolators people reach for by default.

For each target function and each requested accuracy, every method is grown
until it MEETS that accuracy on a dense test grid, and only then measured. So
the table compares like with like: same achieved error, different cost. A
method that cannot reach the accuracy at all is reported as such rather than
dropped, because that is the interesting result for some of them.

Reported per method:
  f-evals   calls to the target function needed to build the approximation
  memory    bytes the approximation occupies
  Meval/s   evaluation throughput on a shuffled batch of 1e6 points
  max err   achieved max error relative to max|f| on the test grid

The comparison set, and why each one is here:
  scipy CubicSpline        the default reach for 1-D interpolation
  scipy quintic spline     a higher-order spline, so the story is not "order 3"
  scipy PchipInterpolator  shape-preserving cubic Hermite: monotone, third order
  numpy Chebyshev          one global polynomial: spectral, but not adaptive
  sklearn spline features  SplineTransformer + LinearRegression, the ML route
  chebpy chebfun           adaptive Chebyshev with splitting: the closest peer
  baobzi                   treeweave's predecessor, same adaptive-tree idea

scipy and numpy are required. The rest are optional; a missing one is skipped
with a note instead of failing the run.

Run:  python benchmarks/compare_interpolators.py
      python benchmarks/compare_interpolators.py --self-test
"""

from __future__ import annotations

import os
import pathlib
import re
import sys
import tempfile
import time

import numpy as np

try:
    from scipy.interpolate import CubicSpline, PchipInterpolator, make_interp_spline
except ImportError:  # pragma: no cover
    sys.exit("this comparison needs scipy: pip install scipy")

import treeweave

N_TEST = 200_001
N_BENCH = 1_000_000
REPEATS = 5
MAX_SIZE = 1 << 20

TREEWEAVE = "treeweave"


class Counter:
    """Wraps a target function and counts calls, including vectorized ones."""

    def __init__(self, f):
        self.f = f
        self.n = 0

    def __call__(self, x):
        arr = np.asarray(x)
        self.n += arr.size if arr.ndim else 1
        return self.f(arr)


def throughput(evaluate, xs: np.ndarray) -> float:
    """Mevals/s, minimum over repeats (the least contaminated run)."""
    evaluate(xs)  # warm up: first call builds whatever the object caches
    best = np.inf
    for _ in range(REPEATS):
        t0 = time.perf_counter()
        evaluate(xs)
        best = min(best, time.perf_counter() - t0)
    return xs.size / best / 1e6


def row(name, evals, memory, rate, err):
    return dict(name=name, evals=evals, memory=memory, rate=rate, err=err)


def missed(name, evals, err):
    """A method that never reached the tolerance. memory 0 marks the failure."""
    return row(name, evals, 0, float("nan"), err)


def grow(name, build, ctx, tol, start=16, cap=None):
    """Double the size until the approximation meets tol, then measure it.

    `build(n)` returns (evaluate, memory_bytes, f_evals) for size n.
    """
    n = start
    err = float("inf")
    while n <= (cap or MAX_SIZE):
        evaluate, memory, evals = build(n)
        err = ctx.error(evaluate)
        if err <= tol:
            return row(name, evals, memory, throughput(evaluate, ctx.xs), err)
        n *= 2
    return missed(name, n, err)


def tighten(name, build, ctx, tol):
    """Ask an adaptive method for a tighter tolerance until it delivers tol.

    `build(requested)` returns (evaluate, memory_bytes, f_evals).
    """
    requested = tol
    err = float("inf")
    for _ in range(6):
        evaluate, memory, evals = build(requested)
        err = ctx.error(evaluate)
        if err <= tol:
            return row(name, evals, memory, throughput(evaluate, ctx.xs), err)
        requested /= 100.0
    return missed(name, 0, err)


class Context:
    """Everything a measurement needs about one target function."""

    def __init__(self, f, a, b, xs):
        self.f, self.a, self.b, self.xs = f, a, b, xs
        self.x_test = np.linspace(a, b, N_TEST)
        self.y_test = f(self.x_test)
        self.scale = float(np.max(np.abs(self.y_test)))

    def error(self, evaluate) -> float:
        got = np.asarray(evaluate(self.x_test)).ravel()
        return float(np.max(np.abs(got - self.y_test))) / self.scale


# --- the methods -------------------------------------------------------------


def m_treeweave(ctx, tol):
    def build(requested):
        counted = Counter(ctx.f)
        approx = treeweave.fit(lambda x: counted(x[0]), ctx.a, ctx.b, tol=requested)
        return approx, approx.memory_usage, counted.n

    return tighten(TREEWEAVE, build, ctx, tol)


def m_cubic_spline(ctx, tol):
    def build(n):
        knots = np.linspace(ctx.a, ctx.b, n)
        spline = CubicSpline(knots, ctx.f(knots))
        return spline, spline.c.nbytes + spline.x.nbytes, n

    return grow("scipy CubicSpline", build, ctx, tol)


def m_quintic_spline(ctx, tol):
    def build(n):
        knots = np.linspace(ctx.a, ctx.b, n)
        spline = make_interp_spline(knots, ctx.f(knots), k=5)
        return spline, spline.c.nbytes + spline.t.nbytes, n

    return grow("scipy quintic spline", build, ctx, tol)


def m_pchip(ctx, tol):
    """Shape-preserving cubic Hermite: third order, the price of monotonicity."""

    def build(n):
        knots = np.linspace(ctx.a, ctx.b, n)
        spline = PchipInterpolator(knots, ctx.f(knots))
        return spline, spline.c.nbytes + spline.x.nbytes, n

    return grow("scipy PchipInterpolator", build, ctx, tol)


def m_chebyshev(ctx, tol):
    """One global Chebyshev interpolant, degree doubled. Spectral, not adaptive."""

    def build(n):
        counted = Counter(ctx.f)
        series = np.polynomial.chebyshev.Chebyshev.interpolate(
            counted, n - 1, domain=[ctx.a, ctx.b]
        )
        return series, series.coef.nbytes, counted.n

    return grow("numpy Chebyshev", build, ctx, tol)


def m_sklearn(ctx, tol):
    """SplineTransformer + LinearRegression: cubic B-spline features, fitted.

    This is least squares over a spline basis, not interpolation, so its floor
    is set by the conditioning of the normal equations rather than by the knot
    spacing. Included because it is the route an ML-shaped codebase takes.
    """
    from sklearn.linear_model import LinearRegression
    from sklearn.pipeline import make_pipeline
    from sklearn.preprocessing import SplineTransformer

    def build(n):
        # 3 samples per basis function: enough to determine the least-squares
        # system without turning the fit into a smoother.
        samples = 3 * (n + 2)
        x = np.linspace(ctx.a, ctx.b, samples).reshape(-1, 1)
        model = make_pipeline(
            SplineTransformer(n_knots=n, degree=3), LinearRegression()
        )
        model.fit(x, ctx.f(x.ravel()))
        memory = model[-1].coef_.nbytes + model[0].bsplines_[0].t.nbytes
        return (lambda q: model.predict(np.asarray(q).reshape(-1, 1))), memory, samples

    # The dense design matrix is samples x features, so the sweep runs out of
    # memory long before the error stops improving. Cap it where it still fits.
    return grow("sklearn spline features", build, ctx, tol, cap=4096)


def m_chebfun(ctx, tol):
    """chebpy: adaptive Chebyshev with interval splitting. The closest peer."""
    import chebpy

    prefs = chebpy.UserPreferences()

    def build(requested):
        counted = Counter(ctx.f)
        saved = prefs.eps
        prefs.eps = requested
        try:
            fun = chebpy.chebfun(counted, [ctx.a, ctx.b])
        finally:
            prefs.eps = saved
        memory = sum(piece.coeffs.nbytes for piece in fun.funs) + 8 * (
            len(fun.funs) + 1
        )
        return fun, memory, counted.n

    return tighten("chebpy chebfun", build, ctx, tol)


_BAOBZI_MEMORY = re.compile(r"memory usage of tree:\s*([0-9.eE+-]+)\s*MiB")


def _baobzi_memory(tree) -> int:
    """baobzi reports its footprint by printing; capture the print and parse it."""
    with tempfile.TemporaryFile(mode="w+") as sink:
        saved = os.dup(1)
        try:
            sys.stdout.flush()
            os.dup2(sink.fileno(), 1)
            tree.stats()
            sys.stdout.flush()
        finally:
            os.dup2(saved, 1)
            os.close(saved)
        sink.seek(0)
        text = sink.read()
    match = _BAOBZI_MEMORY.search(text)
    if not match:
        raise RuntimeError(f"baobzi stats() did not report a memory figure:\n{text}")
    return int(float(match.group(1)) * 1024 * 1024)


def m_baobzi(ctx, tol):
    """treeweave's predecessor: the same adaptive-tree idea, one point per call."""
    import baobzi

    center = np.array([0.5 * (ctx.a + ctx.b)])
    # baobzi reads out of bounds for a query that lands exactly on the upper
    # edge of its box, which segfaults for some targets. Widen the box by a
    # relative 1e-9 so the whole of [a, b] is interior.
    half_length = np.array([0.5 * (ctx.b - ctx.a) * (1.0 + 1e-9)])

    def build(requested):
        counted = Counter(ctx.f)
        tree = baobzi.Baobzi(
            # The callback is a ctypes c_double return, so hand it a plain float
            # rather than the 0-d array a vectorized target hands back.
            fin=lambda x: float(counted(x[0])),
            dim=1,
            order=8,
            center=center,
            half_length=half_length,
            tol=requested,
        )
        evaluate = lambda q: tree(np.ascontiguousarray(q, dtype=np.float64))  # noqa: E731
        return evaluate, _baobzi_memory(tree), counted.n

    return tighten("baobzi", build, ctx, tol)


# Order here is the order of the rows in the printed table and in the docs.
METHODS = [
    (TREEWEAVE, m_treeweave, None),
    ("scipy CubicSpline", m_cubic_spline, None),
    ("scipy quintic spline", m_quintic_spline, None),
    ("scipy PchipInterpolator", m_pchip, None),
    ("numpy Chebyshev", m_chebyshev, None),
    ("sklearn spline features", m_sklearn, "sklearn"),
    ("chebpy chebfun", m_chebfun, "chebpy"),
    ("baobzi", m_baobzi, "baobzi"),
]

# The methods the near-pole gate holds treeweave against: a spline on a uniform
# knot grid, which is what refinement has to beat. A global Chebyshev series is
# deliberately NOT in this list; see the note in check().
UNIFORM_SPLINES = ("scipy CubicSpline", "scipy quintic spline")

TARGETS = {
    # An expensive smooth function: the case treeweave is built for.
    "zeta(s), 1000 terms, on [2, 10]": (
        lambda s: np.sum(
            np.power(np.arange(1, 1001)[:, None], -np.atleast_1d(s)[None, :]), axis=0
        ).reshape(np.shape(s)),
        2.0,
        10.0,
    ),
    # A pole just outside the domain: the case adaptivity is built for.
    "1/(x - 1.05) on [-1, 1]": (lambda x: 1.0 / (x - 1.05), -1.0, 1.0),
    # Oscillation: nobody's favourite, included so the table is not cherry-picked.
    "sin(30 x) on [0, 1]": (lambda x: np.sin(30.0 * x), 0.0, 1.0),
}

TOLERANCES = (1e-6, 1e-10)
POLE = "1/(x - 1.05) on [-1, 1]"


def available(module) -> bool:
    if module is None:
        return True
    try:
        __import__(module)
        return True
    except ImportError:
        return False


def main(emit_rst=False, docs_table=None) -> int:
    rng = np.random.default_rng(0)
    methods = [(name, fn) for name, fn, mod in METHODS if available(mod)]
    for name, _, mod in METHODS:
        if not available(mod):
            print(f"note: skipping {name}, {mod} is not installed")

    rows = {}
    for title, (f, a, b) in TARGETS.items():
        ctx = Context(f, a, b, rng.uniform(a, b, N_BENCH))
        print(f"\n{title}")
        print(
            f"  {'tol':>6}  {'method':<24} {'f-evals':>10} {'memory':>11} {'Meval/s':>9} {'max err':>9}"
        )
        for tol in TOLERANCES:
            for name, measure in methods:
                r = measure(ctx, tol)
                rows[title, tol, name] = r
                mem = f"{r['memory'] / 1024:.1f} KiB" if r["memory"] else "n/a"
                rate = "n/a" if np.isnan(r["rate"]) else f"{r['rate']:.1f}"
                print(
                    f"  {tol:>6.0e}  {name:<24} {r['evals']:>10d} {mem:>11} {rate:>9} {r['err']:>9.1e}"
                )
                sys.stdout.flush()
    if emit_rst:
        print()
        print(as_rst(rows, [name for name, _ in methods]))
    status = check(rows)
    if not docs_table:
        return status
    order = [name for name, _ in methods]
    return max(status, check_docs(rows, order), check_overview(rows))


# How each target is written in docs/guides/performance.rst.
TITLES = {
    "zeta(s), 1000 terms, on [2, 10]": "``zeta(s)``, 1000 terms, on [2, 10]",
    "1/(x - 1.05) on [-1, 1]": "``1/(x - 1.05)`` on [-1, 1]",
    "sin(30 x) on [0, 1]": "``sin(30 x)`` on [0, 1]",
}

DOCS_TABLE = "docs/guides/performance.rst"

# The front page repeats four of those rows at 1e-10, under its own labels and
# with "CubicSpline" for short. Gated too: it is the most-read table in the docs.
OVERVIEW = "docs/overview.src"
OVERVIEW_TITLES = {
    ":math:`\\zeta(s)`, 1000 terms, on [2, 10]": "zeta(s), 1000 terms, on [2, 10]",
    ":math:`1/(x - 1.05)` on [-1, 1]": "1/(x - 1.05) on [-1, 1]",
}
OVERVIEW_METHODS = {"treeweave": TREEWEAVE, "CubicSpline": "scipy CubicSpline"}


def fmt_memory(memory) -> str:
    return f"{memory / 1024:.1f} KiB" if memory else "n/a"


def as_rst(rows, order) -> str:
    """Emit the docs tables, one per target. Paste over the tables in performance.rst."""
    out = []
    for title in TARGETS:
        out += [
            TITLES[title],
            "^" * len(TITLES[title]),
            "",
            ".. list-table::",
            "   :header-rows: 1",
            "   :widths: 7 26 9 11 8 9",
            "",
            "   * - tol",
            "     - method",
            "     - f-evals",
            "     - memory",
            "     - Meval/s",
            "     - max err",
        ]
        for tol in TOLERANCES:
            for name in order:
                r = rows.get((title, tol, name))
                if r is None:
                    continue
                rate = "n/a" if np.isnan(r["rate"]) else f"{r['rate']:.0f}"
                out += [
                    f"   * - {tol:.0e}".replace("e-0", "e-"),
                    f"     - {name}",
                    f"     - {r['evals']}",
                    f"     - {fmt_memory(r['memory'])}",
                    f"     - {rate}",
                    f"     - {r['err']:.1e}",
                ]
        out.append("")
    return "\n".join(out)


# The per-language subsections of "Against the alternatives". Both carry the same
# target labels and both carry a treeweave row, so the parser is scoped to one.
SECTIONS = ("In Python", "In Julia", "In C++", "In Octave")
SECTION = "In Python"


def parse_docs_table(text, order, section=SECTION):
    """Read the published table back out as {(title, tol, method): (evals, memory)}.

    Each target's table is introduced by a line holding exactly that target's
    label, so the parser keys rows on the most recent such line. Only rows under
    `section` are read, and only for methods in `order`: the Julia table repeats
    both the labels and the treeweave row under its own heading.
    """
    labels = {label: title for title, label in TITLES.items()}
    table, title, cells = {}, None, []
    # A document with no section heading at all is one table (the self-test).
    inside = True

    def flush():
        if title and len(cells) == 6 and cells[1] in order:
            table[title, float(cells[0]), cells[1]] = (cells[2], cells[3], cells[4])

    for line in text.splitlines():
        stripped = line.strip()
        if stripped in SECTIONS:
            flush()
            inside, title, cells = stripped == section, None, []
        elif not inside:
            continue
        elif stripped in labels:
            flush()
            title, cells = labels[stripped], []
        elif stripped.startswith("* -"):
            flush()
            cells = [stripped[3:].strip()]
        elif stripped.startswith("- ") and cells:
            cells.append(stripped[2:].strip())
        elif not stripped:
            flush()
            cells = []
    flush()
    return table


def check_docs(rows, order, path=None, section=SECTION) -> int:
    """The published tables must be the ones this script measures.

    Only the deterministic columns are compared: f-evals and memory are set by
    the algorithms, not by the machine. Regenerate with --rst after a change
    that moves them.
    """
    path = pathlib.Path(path or DOCS_TABLE)
    published = parse_docs_table(path.read_text(encoding="utf-8"), order, section)
    failures = 0
    for key, r in sorted(rows.items()):
        title, tol, name = key
        entry = published.get(key)
        if entry is None:
            print(f"FAIL: {path} has no row for {TITLES[title]} @ {tol:.0e} / {name}")
            failures += 1
            continue
        for column, measured, was in (
            ("f-evals", str(r["evals"]), entry[0]),
            ("memory", fmt_memory(r["memory"]), entry[1]),
        ):
            if measured != was:
                print(
                    f"FAIL: {TITLES[title]} @ {tol:.0e} / {name}: {column} is {measured}, docs say {was}"
                )
                failures += 1
    for key in sorted(set(published) - set(rows)):
        print(f"FAIL: {path} has a row this run did not produce: {key}")
        failures += 1
    print(
        "docs tables match" if not failures else f"{failures} docs-table mismatch(es)"
    )
    return 0 if not failures else 1


def check_overview(rows, path=None, perf_path=None) -> int:
    """The front-page table must be the 1e-10 rows this script measures.

    f-evals and memory are held against a fresh measurement. Meval/s is held
    against the published performance table instead: the rate moves by up to a
    factor of two between runs, so a fresh number cannot gate it, but the front
    page must quote the same run as the page it links to.
    """
    path = pathlib.Path(path or OVERVIEW)
    perf = parse_docs_table(
        pathlib.Path(perf_path or DOCS_TABLE).read_text(encoding="utf-8"),
        list(OVERVIEW_METHODS.values()),
        SECTION,
    )
    title, cells, failures, seen = None, [], 0, 0

    def compare():
        nonlocal failures, seen
        if len(cells) != 6 or cells[1] not in OVERVIEW_METHODS or title is None:
            return
        seen += 1
        r = rows[title, 1e-10, OVERVIEW_METHODS[cells[1]]]
        # The front page rounds KiB to whole numbers where it can; accept either.
        want_memory = {
            fmt_memory(r["memory"]),
            fmt_memory(r["memory"]).replace(".0 KiB", " KiB"),
        }
        if cells[2] != str(r["evals"]):
            print(
                f"FAIL: {path}: {cells[1]} on {title}: f-evals is {r['evals']}, page says {cells[2]}"
            )
            failures += 1
        if cells[3] not in want_memory:
            print(
                f"FAIL: {path}: {cells[1]} on {title}: memory is {fmt_memory(r['memory'])}, page says {cells[3]}"
            )
            failures += 1
        published = perf.get((title, 1e-10, OVERVIEW_METHODS[cells[1]]))
        if published is None:
            print(
                f"FAIL: {path}: {cells[1]} on {title}: no such row in the performance table"
            )
            failures += 1
        elif cells[4] != published[2]:
            print(
                f"FAIL: {path}: {cells[1]} on {title}: Meval/s is {published[2]} in the "
                f"performance table, page says {cells[4]}"
            )
            failures += 1

    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped.startswith("* -"):
            compare()
            cells = [stripped[3:].strip()]
            title = OVERVIEW_TITLES.get(cells[0], title)
        elif stripped.startswith("- ") and cells:
            cells.append(stripped[2:].strip())
        elif not stripped:
            compare()
            cells = []
    compare()

    if seen != 2 * len(OVERVIEW_TITLES):
        print(
            f"FAIL: {path}: found {seen} comparison rows, expected {2 * len(OVERVIEW_TITLES)}"
        )
        failures += 1
    print(
        "front-page table matches"
        if not failures
        else f"{failures} front-page mismatch(es)"
    )
    return 0 if not failures else 1


# The deterministic columns are the ones CI can hold to account: f-evals, memory
# and achieved error are reproducible on any machine, while Meval/s is not. Every
# claim docs/guides/performance.rst makes about those three is asserted here, so
# the table cannot drift away from the library without CI saying so.
def check(rows) -> int:
    failures = []

    def fail(msg):
        failures.append(msg)
        print(f"FAIL: {msg}")

    for (title, tol, name), r in rows.items():
        if name == TREEWEAVE:
            if r["err"] > 10 * tol:
                fail(f"{title} @ {tol:.0e}: treeweave err {r['err']:.2e} > 10x tol")
            if r["memory"] == 0:
                fail(f"{title} @ {tol:.0e}: treeweave never reached the tolerance")
        elif r["memory"] == 0:
            print(f"note: {title} @ {tol:.0e}: {name} never reached the tolerance")

    # The headline claim of the comparison: near a pole, adaptive refinement beats
    # a uniform knot grid on both function calls and stored coefficients. It does
    # NOT beat one global Chebyshev series there, and the docs say so: 1/(x-1.05)
    # is analytic on [-1, 1], so a Chebyshev series converges geometrically and
    # stores less. treeweave's win over it in that row is throughput, not size.
    for tol in TOLERANCES:
        tw = rows.get((POLE, tol, TREEWEAVE))
        if tw is None:
            continue
        for name in UNIFORM_SPLINES:
            other = rows.get((POLE, tol, name))
            if other is None or other["memory"] == 0:
                continue
            if tw["evals"] >= other["evals"]:
                fail(
                    f"{POLE} @ {tol:.0e}: treeweave used {tw['evals']} f-evals, {name} {other['evals']}"
                )
            if tw["memory"] >= other["memory"]:
                fail(
                    f"{POLE} @ {tol:.0e}: treeweave stored {tw['memory']} B, {name} {other['memory']} B"
                )

    print("\nchecks passed" if not failures else f"\n{len(failures)} check(s) failed")
    return 0 if not failures else 1


# Positive control for check(): a fabricated table that violates each rule must
# come back non-zero, and the same table repaired must come back zero. Without it
# a check() that can never fail is indistinguishable from a passing benchmark.
def self_test() -> int:
    def table(tw_err, tw_evals, tw_memory):
        rows = {}
        for tol in TOLERANCES:
            rows[POLE, tol, TREEWEAVE] = row(
                TREEWEAVE, tw_evals, tw_memory, 1.0, tw_err(tol)
            )
            for name in UNIFORM_SPLINES:
                rows[POLE, tol, name] = row(name, 4096, 131072, 1.0, tol)
        return rows

    cases = [
        ("clean table", table(lambda tol: tol, 512, 16384), 0),
        ("treeweave misses tol", table(lambda tol: 1000 * tol, 512, 16384), 1),
        ("treeweave costs more f-evals", table(lambda tol: tol, 8192, 16384), 1),
        ("treeweave stores more coefficients", table(lambda tol: tol, 512, 262144), 1),
        ("treeweave never converged", table(lambda tol: tol, 512, 0), 1),
    ]
    bad = 0
    for name, rows, want in cases:
        got = check(rows)
        print(f"self-test {name}: check() -> {got}, expected {want}")
        bad += got != want
    return max(bad and 1, docs_self_test())


def docs_self_test() -> int:
    """Positive control for check_docs(): the gate must fire when the docs drift."""
    rows = {
        (title, tol, name): row(name, 512, 16384, 1.0, tol)
        for title in TARGETS
        for tol in TOLERANCES
        for name in (TREEWEAVE, "scipy CubicSpline")
    }
    order = [TREEWEAVE, "scipy CubicSpline"]
    page = as_rst(rows, order)
    bad = 0
    with tempfile.TemporaryDirectory() as tmp:
        good = pathlib.Path(tmp) / "good.rst"
        good.write_text(page, encoding="utf-8")
        cases = [
            ("table as measured", page, 0),
            ("f-evals drifted", page.replace("     - 512", "     - 1024", 1), 1),
            ("memory drifted", page.replace("16.0 KiB", "32.0 KiB", 1), 1),
            (
                "a row went missing",
                page.replace("   * - 1e-6\n     - treeweave\n", "", 1),
                1,
            ),
            (
                "another language's rows under the same label",
                page.replace(
                    "   * - 1e-6\n     - treeweave\n",
                    "   * - 1e-6\n     - Dierckx.jl quintic\n     - 99\n"
                    "     - 9.9 KiB\n     - 1\n     - 1e-10\n"
                    "   * - 1e-6\n     - treeweave\n",
                    1,
                ),
                0,
            ),
            # The Julia section repeats the labels and the treeweave rows. Its
            # numbers must not be read as this section's.
            (
                "the other section's treeweave rows drifted",
                f"{SECTIONS[0]}\n\n{page}\n{SECTIONS[1]}\n\n"
                + page.replace("     - 512", "     - 4096"),
                0,
            ),
            (
                "this section's treeweave rows drifted",
                f"{SECTIONS[0]}\n\n"
                + page.replace("     - 512", "     - 4096")
                + f"\n{SECTIONS[1]}\n\n{page}",
                1,
            ),
        ]
        for name, text, want in cases:
            path = pathlib.Path(tmp) / "case.rst"
            path.write_text(text, encoding="utf-8")
            got = check_docs(rows, order, path)
            print(f"self-test docs {name}: check_docs() -> {got}, expected {want}")
            bad += got != want

        front = "\n".join(
            line
            for label, title in OVERVIEW_TITLES.items()
            for line in (
                f"   * - {label}",
                "     - treeweave",
                "     - 512",
                "     - 16.0 KiB",
                "     - 1",
                "     - 1e-10",
                "   * -",
                "     - CubicSpline",
                "     - 512",
                "     - 16.0 KiB",
                "     - 1",
                "     - 1e-10",
            )
        )
        for name, text, want in [
            ("table as measured", front, 0),
            ("f-evals drifted", front.replace("     - 512", "     - 1024", 1), 1),
            ("memory drifted", front.replace("16.0 KiB", "32.0 KiB", 1), 1),
            # The rate is not re-measured, so nothing else catches a stale one.
            ("Meval/s drifted", front.replace("     - 1\n", "     - 2\n", 1), 1),
            (
                "a row went missing",
                front.replace("   * -\n     - CubicSpline\n", "", 1),
                1,
            ),
        ]:
            path = pathlib.Path(tmp) / "overview.src"
            path.write_text(text, encoding="utf-8")
            got = check_overview(rows, path, good)
            print(
                f"self-test front page {name}: check_overview() -> {got}, expected {want}"
            )
            bad += got != want
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    if "--self-test" in sys.argv:
        raise SystemExit(self_test())
    docs = DOCS_TABLE if "--check-docs" in sys.argv else None
    raise SystemExit(main(emit_rst="--rst" in sys.argv, docs_table=docs))
