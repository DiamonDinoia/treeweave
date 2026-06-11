# Example: 2D -> 3D vector approximation.
using Treeweave

# f: R^2 -> R^3
f = (x, y) -> (sin(x)*cos(y), x + y, x*y)

println("Fitting f: R^2 -> R^3 on [0,1]^2 ...")
b = fit(f, [0.0, 0.0], [1.0, 1.0], 1e-8)   # out_dim inferred from a probe of f
println("  ", b)

# Single-point eval
pt = [0.3, 0.7]
got = b(pt)
ref = collect(f(pt...))
println("\nSingle point [0.3, 0.7]:")
println("  approx = $got")
println("  exact  = $ref")
println("  error  = $(maximum(abs.(got .- ref)))")

# Batch eval — call the handle directly
n = 5
X = rand(n, 2)
R = b(X)                     # n×3 matrix
println("\nBatch eval ($n points), max component error:")
errs = [maximum(abs.(R[i,:] .- collect(f(X[i,1], X[i,2])))) for i in 1:n]
println("  max error = $(maximum(errs))")
