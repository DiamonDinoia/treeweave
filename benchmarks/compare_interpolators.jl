#!/usr/bin/env julia
#
# Compare treeweave against the interpolators a Julia user reaches for.
#
# Same protocol as benchmarks/compare_interpolators.py: for each target and each
# requested accuracy, every method is grown until it MEETS that accuracy on a
# dense test grid, and only then measured. The table compares like with like:
# same achieved error, different cost.
#
# Reported per method:
#   f-evals   calls to the target needed to build the approximation
#   memory    bytes the approximation's coefficients and knots occupy
#   Meval/s   evaluation throughput on a shuffled batch of 1e6 points
#   max err   achieved max error relative to max|f| on the test grid
#
# The comparison set:
#   Interpolations.jl cubic    B-spline on a uniform grid: the default reach
#   Dierckx.jl quintic         FITPACK, the same engine scipy's splines use
#   FastChebInterp.jl          one global Chebyshev series: spectral, not adaptive
#   DataInterpolations cubic   the SciML ecosystem's cubic spline
#   DataInterpolations PCHIP   shape-preserving Hermite: monotone, third order
#
# The adaptive-Chebyshev peer (Chebfun) is measured in the Python table through
# chebpy, which is the same algorithm; ApproxFun.jl offers no tolerance dial that
# fits this protocol, so it is not in the field.
#
# Run:  julia --project=bindings/julia/Treeweave benchmarks/compare_interpolators.jl
#       julia ... benchmarks/compare_interpolators.jl --self-test
#       julia ... benchmarks/compare_interpolators.jl --rst
#       julia ... benchmarks/compare_interpolators.jl --check-docs

using Printf
using Random
using Treeweave
using Interpolations
using Dierckx
using FastChebInterp
using DataInterpolations

const N_TEST = 200_001
const N_BENCH = 1_000_000
const REPEATS = 5
const MAX_SIZE = 1 << 20
const TREEWEAVE = "treeweave"
const TOLERANCES = (1e-6, 1e-10)
const DOCS_TABLE = "docs/guides/performance.rst"

struct Ctx
    f::Function
    a::Float64
    b::Float64
    xs::Vector{Float64}
    x_test::Vector{Float64}
    y_test::Vector{Float64}
    scale::Float64
end

function Ctx(f, a, b, xs)
    x_test = collect(range(a, b; length = N_TEST))
    y_test = f.(x_test)
    Ctx(f, a, b, xs, x_test, y_test, maximum(abs, y_test))
end

max_error(ctx::Ctx, ev) = maximum(abs.(ev(ctx.x_test) .- ctx.y_test)) / ctx.scale

"Mevals/s, minimum over repeats (the least contaminated run)."
function throughput(ev, xs)
    ev(xs)   # warm up: the first call compiles the specialization
    best = Inf
    for _ in 1:REPEATS
        t0 = time_ns()
        ev(xs)
        best = min(best, (time_ns() - t0) / 1e9)
    end
    length(xs) / best / 1e6
end

row(name, evals, memory, rate, err) =
    (name = name, evals = evals, memory = memory, rate = rate, err = err)

"A method that never reached the tolerance. memory 0 marks the failure."
missed(name, evals, err) = row(name, evals, 0, NaN, err)

"""Double the size until the approximation meets tol, then measure it.
`build(n)` returns (evaluate, memory_bytes, f_evals) for size n."""
function grow(name, build, ctx, tol; start = 16, cap = MAX_SIZE)
    n, err = start, Inf
    while n <= cap
        ev, memory, evals = build(n)
        err = max_error(ctx, ev)
        err <= tol && return row(name, evals, memory, throughput(ev, ctx.xs), err)
        n *= 2
    end
    missed(name, n, err)
end

"""Ask an adaptive method for a tighter tolerance until it delivers tol.
`build(requested)` returns (evaluate, memory_bytes, f_evals)."""
function tighten(name, build, ctx, tol)
    requested, err = tol, Inf
    for _ in 1:6
        ev, memory, evals = build(requested)
        err = max_error(ctx, ev)
        err <= tol && return row(name, evals, memory, throughput(ev, ctx.xs), err)
        requested /= 100
    end
    missed(name, 0, err)
end

# --- the methods -------------------------------------------------------------

function m_treeweave(ctx, tol)
    function build(requested)
        calls = Ref(0)
        counted = x -> (calls[] += 1; ctx.f(x))
        approx = fit(counted, ctx.a, ctx.b, requested)
        (x -> approx(x), memory_usage(approx), calls[])
    end
    tighten(TREEWEAVE, build, ctx, tol)
end

function m_interpolations(ctx, tol)
    function build(n)
        knots = range(ctx.a, ctx.b; length = n)
        itp = cubic_spline_interpolation(knots, ctx.f.(knots))
        # The knots are a range, so only the B-spline coefficients occupy memory.
        # itp is an Extrapolation over a ScaledInterpolation over the B-spline.
        (x -> itp.(x), sizeof(parent(itp.itp.itp.coefs)), n)
    end
    grow("Interpolations.jl cubic", build, ctx, tol)
end

function m_dierckx(ctx, tol)
    function build(n)
        knots = collect(range(ctx.a, ctx.b; length = n))
        spl = Spline1D(knots, ctx.f.(knots); k = 5, s = 0.0)
        (x -> spl(x), sizeof(spl.c) + sizeof(spl.t), n)
    end
    # A quintic spline needs at least k+1 = 6 knots.
    grow("Dierckx.jl quintic", build, ctx, tol; start = 16)
end

function m_fastcheb(ctx, tol)
    function build(n)
        x = chebpoints(n - 1, ctx.a, ctx.b)
        c = chebinterp(ctx.f.(x), ctx.a, ctx.b)
        (q -> c.(q), sizeof(c.coefs), n)
    end
    grow("FastChebInterp.jl", build, ctx, tol)
end

"""Bytes an evaluation reads out of a DataInterpolations object.

Every array it holds is a plain `Vector{Float64}` sized by the knot count: the
cubic spline keeps `u`, `t`, `h` and `z`, the Hermite form `du`, `u` and `t`.
The parameter cache is empty unless `cache_parameters = true`, which is not the
default, so summing the object's own float vectors is the whole footprint.
"""
di_memory(itp) = sum(
    sizeof(getfield(itp, f)) for
    f in fieldnames(typeof(itp)) if getfield(itp, f) isa AbstractVector{Float64}
)

function m_datainterp_cubic(ctx, tol)
    function build(n)
        knots = collect(range(ctx.a, ctx.b; length = n))
        itp = DataInterpolations.CubicSpline(ctx.f.(knots), knots)
        (x -> itp.(x), di_memory(itp), n)
    end
    grow("DataInterpolations cubic", build, ctx, tol)
end

function m_datainterp_pchip(ctx, tol)
    function build(n)
        knots = collect(range(ctx.a, ctx.b; length = n))
        itp = DataInterpolations.PCHIPInterpolation(ctx.f.(knots), knots)
        (x -> itp.(x), di_memory(itp), n)
    end
    grow("DataInterpolations PCHIP", build, ctx, tol)
end

# Order here is the order of the rows in the printed table and in the docs.
const METHODS = [
    (TREEWEAVE, m_treeweave),
    ("Interpolations.jl cubic", m_interpolations),
    ("Dierckx.jl quintic", m_dierckx),
    ("FastChebInterp.jl", m_fastcheb),
    ("DataInterpolations cubic", m_datainterp_cubic),
    ("DataInterpolations PCHIP", m_datainterp_pchip),
]

# The methods the near-pole gate holds treeweave against: splines on a uniform
# knot grid, which is what refinement has to beat. A global Chebyshev series is
# deliberately NOT in this list; see the note in check().
const UNIFORM_SPLINES = ("Interpolations.jl cubic", "Dierckx.jl quintic",
                         "DataInterpolations cubic", "DataInterpolations PCHIP")

zeta_n(s) = sum(k -> Float64(k)^(-s), 1:1000)

const TARGETS = [
    # An expensive smooth function: the case treeweave is built for.
    ("zeta(s), 1000 terms, on [2, 10]", zeta_n, 2.0, 10.0),
    # A pole just outside the domain: the case adaptivity is built for.
    ("1/(x - 1.05) on [-1, 1]", x -> 1.0 / (x - 1.05), -1.0, 1.0),
    # Oscillation: nobody's favourite, included so the table is not cherry-picked.
    ("sin(30 x) on [0, 1]", x -> sin(30.0 * x), 0.0, 1.0),
]

const POLE = "1/(x - 1.05) on [-1, 1]"

# How each target is written in docs/guides/performance.rst.
const TITLES = Dict(
    "zeta(s), 1000 terms, on [2, 10]" => "``zeta(s)``, 1000 terms, on [2, 10]",
    "1/(x - 1.05) on [-1, 1]" => "``1/(x - 1.05)`` on [-1, 1]",
    "sin(30 x) on [0, 1]" => "``sin(30 x)`` on [0, 1]",
)

fmt_memory(memory) = memory == 0 ? "n/a" : @sprintf("%.1f KiB", memory / 1024)
fmt_tol(tol) = replace(@sprintf("%.0e", tol), "e-0" => "e-")

"""Every published claim this benchmark can settle.

Near the pole, refinement must beat a spline on a uniform knot grid on both
f-evals and memory. A global Chebyshev series is not in the comparison: the
pole sits at 1.05, outside [-1, 1], so the target is analytic on the domain and
a single series converges geometrically. treeweave does not win that column
there, and the docs say so.
"""
function check(rows)
    failures = String[]
    # Every treeweave row must have reached the accuracy it was asked for. The
    # pole comparison below cannot see this: a fit that never converged reports
    # zero memory, which beats any competitor.
    for key in sort(collect(keys(rows)))
        title, tol, name = key
        name == TREEWEAVE || continue
        r = rows[key]
        r.err > 10 * tol && push!(
            failures,
            "$title @ $(fmt_tol(tol)): treeweave err $(@sprintf("%.2e", r.err)) > 10x tol",
        )
        r.memory == 0 &&
            push!(failures, "$title @ $(fmt_tol(tol)): treeweave never reached the tolerance")
    end
    for tol in TOLERANCES
        tw = rows[(POLE, tol, TREEWEAVE)]
        for name in UNIFORM_SPLINES
            other = rows[(POLE, tol, name)]
            other.memory == 0 && continue
            for (column, mine, theirs) in
                (("f-evals", tw.evals, other.evals), ("memory", tw.memory, other.memory))
                if mine >= theirs
                    push!(
                        failures,
                        "near the pole at $(fmt_tol(tol)): treeweave $column $mine " *
                        "is not below $name's $theirs",
                    )
                end
            end
        end
    end
    for f in failures
        println("FAIL: $f")
    end
    println(isempty(failures) ? "every claim holds" : "$(length(failures)) claim(s) failed")
    isempty(failures) ? 0 : 1
end

"""The docs explain Interpolations.jl's knot count as a boundary layer.

`cubic_spline_interpolation` defaults to `bc = Line(OnGrid())`, the natural
boundary condition, while scipy's `CubicSpline` defaults to not-a-knot. So the
error of the Julia cubic concentrates within a knot spacing of each endpoint and
the interior is already converged. This holds the numbers the docs quote: with
2048 knots on zeta, the max error is above 1e-7 and sits within one knot spacing
of an endpoint, while 5% of the domain in from either end it is below 1e-11.
"""
function check_boundary_layer(; margin = 0.4)
    f, a, b = zeta_n, 2.0, 10.0
    n = 2048
    x = collect(range(a, b; length = N_TEST))
    y = f.(x)
    knots = range(a, b; length = n)
    itp = cubic_spline_interpolation(knots, f.(knots))
    err = abs.(itp.(x) .- y) ./ maximum(abs, y)
    h = (b - a) / (n - 1)
    inner = [i for (i, xi) in enumerate(x) if a + margin <= xi <= b - margin]
    edge, worst = findmax(err)
    interior = maximum(err[inner])
    spacings = min(x[worst] - a, b - x[worst]) / h
    failures = String[]
    edge > 1e-7 ||
        push!(failures, "the boundary error is $(@sprintf("%.1e", edge)), not above 1e-7")
    spacings <= 1 || push!(failures,
        "the worst point is $(@sprintf("%.2f", spacings)) knot spacings from an endpoint, not within one")
    interior < 1e-11 || push!(failures,
        "the interior error is $(@sprintf("%.1e", interior)), not below 1e-11")
    for msg in failures
        println("FAIL: boundary layer: $msg")
    end
    @printf("boundary layer at n=%d: max %.1e at %.2f h from an endpoint, interior %.1e\n",
            n, edge, spacings, interior)
    isempty(failures) ? 0 : 1
end

"Emit the docs table for the Julia field. Paste over the table in performance.rst."
function as_rst(rows, order)
    out = String[]
    for (title, _, _, _) in TARGETS
        label = TITLES[title]
        append!(
            out,
            [
                label,
                "^"^length(label),
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
            ],
        )
        for tol in TOLERANCES, name in order
            r = get(rows, (title, tol, name), nothing)
            r === nothing && continue
            rate = isnan(r.rate) ? "n/a" : @sprintf("%.0f", r.rate)
            append!(
                out,
                [
                    "   * - $(fmt_tol(tol))",
                    "     - $name",
                    "     - $(r.evals)",
                    "     - $(fmt_memory(r.memory))",
                    "     - $rate",
                    "     - " * @sprintf("%.1e", r.err),
                ],
            )
        end
        push!(out, "")
    end
    join(out, "\n")
end

# The per-language subsections of "Against the alternatives". Both carry the same
# target labels and both carry a treeweave row, so the parser is scoped to one.
const SECTIONS = ("In Python", "In Julia", "In C++", "In Octave")
const SECTION = "In Julia"

"""Read the published table back as Dict((title, tol, method) => (evals, memory)).

Each target's table is introduced by a line holding exactly that target's label,
so the parser keys rows on the most recent such line. Only rows under `section`
are read, and only for methods in `order`: the Python table repeats both the
labels and the treeweave row under its own heading.
"""
function parse_docs_table(text, order; section = SECTION)
    labels = Dict(label => title for (title, label) in TITLES)
    table = Dict{Tuple{String,Float64,String},Tuple{String,String}}()
    title, cells = nothing, String[]
    # A document with no section heading at all is one table (the self-test).
    inside = true
    flush!() = begin
        if title !== nothing && length(cells) == 6 && cells[2] in order
            table[(title, parse(Float64, cells[1]), cells[2])] = (cells[3], cells[4])
        end
    end
    for line in split(text, '\n')
        stripped = strip(line)
        if stripped in SECTIONS
            flush!()
            inside, title, cells = stripped == section, nothing, String[]
        elseif !inside
            continue
        elseif haskey(labels, stripped)
            flush!()
            title, cells = labels[stripped], String[]
        elseif startswith(stripped, "* -")
            flush!()
            cells = [strip(stripped[4:end])]
        elseif startswith(stripped, "- ") && !isempty(cells)
            push!(cells, strip(stripped[3:end]))
        elseif isempty(stripped)
            flush!()
            cells = String[]
        end
    end
    flush!()
    table
end

"""The published table must be the one this script measures.

Only the deterministic columns are compared: f-evals and memory are set by the
algorithms, not by the machine. Regenerate with --rst after a change that moves
them.
"""
function check_docs(rows, order; path = DOCS_TABLE, section = SECTION)
    published = parse_docs_table(read(path, String), order; section = section)
    failures = 0
    for (key, r) in sort(collect(rows); by = first)
        (title, tol, name) = key
        name in order || continue
        entry = get(published, key, nothing)
        if entry === nothing
            println("FAIL: $path has no row for $(TITLES[title]) @ $(fmt_tol(tol)) / $name")
            failures += 1
            continue
        end
        for (column, measured, was) in
            (("f-evals", string(r.evals), entry[1]), ("memory", fmt_memory(r.memory), entry[2]))
            if measured != was
                println(
                    "FAIL: $(TITLES[title]) @ $(fmt_tol(tol)) / $name: $column is " *
                    "$measured, docs say $was",
                )
                failures += 1
            end
        end
    end
    for key in setdiff(keys(published), keys(rows))
        println("FAIL: $path has a row this run did not produce: $key")
        failures += 1
    end
    println(failures == 0 ? "docs table matches" : "$failures docs-table mismatch(es)")
    failures == 0 ? 0 : 1
end

"Positive control: every gate must fire on the thing it is supposed to catch."
function self_test()
    failures = String[]
    order = [TREEWEAVE, "Interpolations.jl cubic"]
    good = Dict{Tuple{String,Float64,String},Any}()
    for tol in TOLERANCES
        good[(POLE, tol, TREEWEAVE)] = row(TREEWEAVE, 100, 1024, 200.0, tol / 2)
        # `check` reads every name in UNIFORM_SPLINES, so the fixture carries a
        # row for each. The values only have to sit above treeweave's.
        for (i, name) in enumerate(UNIFORM_SPLINES)
            good[(POLE, tol, name)] = row(name, 1000 - 100 * i, 8192 - 1024 * i, 20.0, tol / 2)
        end
    end
    check(good) == 0 || push!(failures, "a winning table was reported as failing")

    # The interior number is only evidence if the window that measures it
    # excludes the boundary layer. With no margin it must fail.
    check_boundary_layer() == 0 ||
        push!(failures, "the published boundary-layer numbers do not hold")
    check_boundary_layer(margin = 0.0) == 0 &&
        push!(failures, "a boundary layer inside the interior window was not detected")

    for (what, mutate) in [
        ("more f-evals than the cubic spline", r -> row(r.name, 5000, r.memory, r.rate, r.err)),
        ("more memory than the cubic spline", r -> row(r.name, r.evals, 1 << 20, r.rate, r.err)),
        # Zero memory beats every competitor, so only the per-row gate sees this.
        ("treeweave never converged", r -> row(r.name, r.evals, 0, r.rate, r.err)),
        ("an achieved error above 10x the tolerance",
         r -> row(r.name, r.evals, r.memory, r.rate, 1000 * r.err)),
    ]
        bad = copy(good)
        bad[(POLE, 1e-10, TREEWEAVE)] = mutate(good[(POLE, 1e-10, TREEWEAVE)])
        check(bad) == 0 && push!(failures, "$what: not detected")
    end

    mktempdir() do dir
        path = joinpath(dir, "performance.rst")
        rows = Dict(k => v for (k, v) in good if k[3] in order)
        write(path, as_rst(rows, order))
        check_docs(rows, order; path = path) == 0 ||
            push!(failures, "the table as emitted did not match itself")
        drifted = copy(rows)
        drifted[(POLE, 1e-10, TREEWEAVE)] =
            row(TREEWEAVE, 101, 1024, 200.0, 1e-11)
        check_docs(drifted, order; path = path) == 0 &&
            push!(failures, "an f-eval drift against the docs was not detected")
        write(path, "")
        check_docs(rows, order; path = path) == 0 &&
            push!(failures, "an empty docs table was accepted")

        # The Python section repeats the labels and the treeweave rows. Its
        # numbers must not be read as this section's.
        table = as_rst(rows, order)
        drift = replace(table, "     - 100\n" => "     - 4096\n")
        write(path, "$(SECTIONS[1])\n\n$drift\n$(SECTIONS[2])\n\n$table")
        check_docs(rows, order; path = path) == 0 ||
            push!(failures, "the other section's rows were read as this section's")
        write(path, "$(SECTIONS[1])\n\n$table\n$(SECTIONS[2])\n\n$drift")
        check_docs(rows, order; path = path) == 0 &&
            push!(failures, "a drift in this section's rows was not detected")
    end

    for f in failures
        println("FAIL: $f")
    end
    println(isempty(failures) ? "self-test passed" : "$(length(failures)) self-test case(s) failed")
    isempty(failures) ? 0 : 1
end

function main(args)
    "--self-test" in args && return self_test()
    rng = MersenneTwister(0)
    rows = Dict{Tuple{String,Float64,String},Any}()
    order = [name for (name, _) in METHODS]
    for (title, f, a, b) in TARGETS
        ctx = Ctx(f, a, b, a .+ (b - a) .* rand(rng, N_BENCH))
        println("\n$title")
        @printf("  %6s  %-24s %10s %11s %9s %9s\n",
                "tol", "method", "f-evals", "memory", "Meval/s", "max err")
        for tol in TOLERANCES, (name, measure) in METHODS
            r = measure(ctx, tol)
            rows[(title, tol, name)] = r
            rate = isnan(r.rate) ? "n/a" : @sprintf("%.1f", r.rate)
            @printf("  %6s  %-24s %10d %11s %9s %9.1e\n",
                    fmt_tol(tol), name, r.evals, fmt_memory(r.memory), rate, r.err)
            flush(stdout)
        end
    end
    "--rst" in args && println("\n", as_rst(rows, order))
    status = max(check(rows), check_boundary_layer())
    "--check-docs" in args ? max(status, check_docs(rows, order)) : status
end

exit(main(ARGS))
