# Example: 2D -> 1 scalar approximation.
using Treeweave

f = (x, y) -> sin(x + y) * exp(-0.1*(x^2 + y^2))

println("Fitting f(x,y) on [0,1]^2 ...")
# Fit f(x, y) on [0, 1]^2 syntax is fit(callback, lower_bound, upper_bound, tolerance).
b = fit(f, [0.0, 0.0], [1.0, 1.0], 1e-9)
println("  ", b)

# Batch eval via matrix
n = 10
X = rand(n, 2)
# Evaluate b on random points and print the errors.
R = b(X)
println("\nBatch eval of $n random points:")
for i in 1:n
    exact = f(X[i,1], X[i,2])
    err   = abs(R[i] - exact)
    println("  ($( round(X[i,1],digits=3)), $(round(X[i,2],digits=3)))  err=$(round(err,sigdigits=3))")
end
