# Example: the four evaluation routes of a fitted handle.
using Treeweave

wave = x -> (sin(x), cos(x))
# Fit wave(x) on [0, 5] syntax is fit(callback, lower_bound, upper_bound, tolerance).
approx = fit(wave, 0.0, 5.0, 1e-9)
xs = collect(range(0.0, 5.0; length = 1024))  # ascending, so the sorted route applies

# BEGIN DOCS_ROUTES
point = approx(3.5)                    # single point -> length-2 Vector
batch = approx(xs)                     # batch (Vector) -> 1024x2 Matrix
asc   = approx(xs; sorted = true)      # xs promised non-decreasing (dim == 1)
cols  = approx(xs; transposed = true)  # batch -> 2x1024 Matrix
# END DOCS_ROUTES

@assert length(point) == 2
@assert size(batch) == (length(xs), 2)
@assert batch == asc
@assert batch == permutedims(cols)

max_err = maximum(abs.(batch .- hcat(sin.(xs), cos.(xs))))
println("max |approx - exact| over $(length(xs)) points: $(max_err)")
@assert max_err < 1e-8
println("OK")
