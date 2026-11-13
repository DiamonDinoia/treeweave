using Test
using Treeweave

# 1. Accuracy: 1D scalar fit
@testset "1D scalar accuracy" begin
    f1d = x -> exp(0.5x) + sin(3x)
    b   = fit(f1d, 0.0, 1.0, 1e-8)
    @test b isa TreeweaveFn{Float64}
    # The fit domain is [a, b); exact boundary coordinates are evaluable too
    # (b returns the boundary value), but this sweep uses interior points.
    for x in LinRange(0.02, 0.98, 50)
        @test abs(b(x) - f1d(x)) < 1e-5
    end
end

# 2. 2D -> 3D vector fit
@testset "2D->3D vector accuracy" begin
    f2d3 = (x, y) -> (sin(x)*cos(y), x + y, x*y)
    b    = fit(f2d3, [0.0, 0.0], [1.0, 1.0], 1e-6; out_dim=3)
    @test b isa TreeweaveFn{Float64}
    @test b.dim     == 2
    @test b.out_dim == 3

    for (xi, yi) in ((0.1, 0.2), (0.5, 0.7), (0.9, 0.3))
        ref = collect(f2d3(xi, yi))
        got = b([xi, yi])
        @test got isa Vector{Float64}
        @test length(got) == 3
        for k in 1:3
            @test abs(got[k] - ref[k]) < 1e-4
        end
    end
end

# 3. sorted vs general-batch parity (dim==1)
@testset "sorted vs batch parity" begin
    f1d = x -> sin(x) + 0.5cos(2x)
    b   = fit(f1d, 0.0, Float64(pi), 1e-8)
    xs  = sort(rand(200) .* pi)

    r_batch  = b(xs)
    r_sorted = b(xs; sorted=true)

    @test r_batch == r_sorted
end

# 4. NaN out-of-domain
@testset "NaN out-of-domain" begin
    b = fit(x -> x^2, 0.0, 1.0, 1e-8)
    @test isnan(b(2.0))
    @test isnan(b(-1.0))
end

# 5. Raising Julia closure propagates as Julia exception (does NOT crash)
@testset "raising closure throws Julia exception" begin
    boom = x -> error("intentional test error")
    @test_throws ErrorException fit(boom, 0.0, 1.0, 1e-8)
end

# 6. Too-tight tolerance or too-shallow depth raises a descriptive error
@testset "fit failure raises informative error" begin
    # Very tight tolerance + tiny max_depth forces MaxDepthExceeded.
    opts = TreeweaveOptions(max_depth=1, max_memory_mib=4)
    noisy = x -> sin(1000x) + cos(997x)
    err = try
        fit(noisy, 0.0, 1.0, 1e-14; options=opts)
        nothing
    catch e
        e
    end
    @test err !== nothing
    msg = sprint(showerror, err)
    @test occursin(r"MaxDepth|MemoryBudget|fit failed"i, msg)
end

# 7. batch matrix interface
@testset "batch matrix" begin
    f2d = (x, y) -> sin(x + y)
    b   = fit(f2d, [0.0, 0.0], [1.0, 1.0], 1e-7)
    n   = 20
    X   = rand(n, 2)
    R   = b(X)
    @test size(R) == (n,)   # out_dim==1 returns Vector of length n
    for i in 1:n
        @test abs(R[i] - sin(X[i,1] + X[i,2])) < 1e-5
    end
end

# 8. memory_usage is positive
@testset "memory_usage" begin
    b = fit(x -> cos(x), 0.0, 2.0, 1e-9)
    @test memory_usage(b) > 0
end

# 9. Float32 fit
@testset "Float32 fit" begin
    b = fit(x -> Float32(exp(x)), Float32(0), Float32(1), 1e-5; dtype=Float32)
    @test b isa TreeweaveFn{Float32}
    val = b(Float32(0.5))
    @test abs(val - Float32(exp(0.5))) < Float32(1e-3)
end

# 10. transposed (SoA) layout parity, and inferred out_dim
@testset "transposed parity + inferred out_dim" begin
    f2d3 = (x, y) -> (sin(x)*cos(y), x + y, x*y)
    b    = fit(f2d3, [0.0, 0.0], [1.0, 1.0], 1e-6)   # out_dim inferred from probe
    @test b.out_dim == 3

    X = rand(30, 2)
    aos = b(X)                  # 30×3
    tr  = b(X; transposed=true) # 3×30
    @test size(aos) == (30, 3)
    @test size(tr)  == (3, 30)
    @test tr == permutedims(aos)
end

# 11. strict validation of mis-shaped input / illegal flags
@testset "validation" begin
    b1 = fit(x -> exp(x), 0.0, 1.0, 1e-6)                     # dim 1, out 1
    b2 = fit((x, y) -> x + y, [0.0, 0.0], [1.0, 1.0], 1e-6)   # dim 2, out 1

    @test_throws ErrorException b2([0.5])                     # point length != dim
    @test_throws ErrorException b2([0.1, 0.2, 0.3])           # point length != dim
    @test_throws ErrorException b2(rand(5, 2); sorted=true)   # sorted needs dim==1
    @test_throws ErrorException b1(rand(10); transposed=true) # transposed needs out_dim>1
    @test_throws ErrorException b1(rand(10); sorted=true, transposed=true)
end

# 12. out= writes in place and returns the caller's vector (zero-copy path)
@testset "out= in-place batch/sorted" begin
    b  = fit(x -> exp(0.5x), 0.0, 1.0, 1e-8)
    xs = collect(LinRange(0.0, 1.0, 256))

    expected = b(xs)
    buf = similar(xs)
    got = b(xs; out=buf)
    @test got === buf            # same array, written in place
    @test got == expected        # bit-exact with the allocating path

    xss        = sort(rand(200))
    exp_sorted = b(xss; sorted=true)
    buf2       = similar(xss)
    got2       = b(xss; sorted=true, out=buf2)
    @test got2 === buf2
    @test got2 == exp_sorted

    @test_throws ErrorException b(xs; out=similar(xs, 10))            # wrong length
    @test_throws ErrorException b(xs; out=zeros(Float32, length(xs))) # wrong eltype
    @test_throws ErrorException b(0.5; out=zeros(1))                  # out= with a point

    bv = fit(x -> (sin(x), cos(x)), 0.0, 1.0, 1e-6; out_dim=2)
    @test_throws ErrorException bv(xs; out=zeros(2 * length(xs)))
    @test_throws ErrorException bv(xs; transposed=true, out=zeros(2, length(xs)))
end

# 13. fit file: the first fit writes it, the second loads it with zero callback calls
@testset "fit file" begin
    mktempdir() do dir
        cases = [
            ("1D f64", (x -> exp(0.5x) + sin(3x)), 0.0, 1.0, Float64, 1e-9, collect(LinRange(0.0, 0.999, 257)), 1),
            ("1D f32", (x -> Float32(exp(x))), Float32(0), Float32(1), Float32, 1e-4, Float32.(LinRange(0, 0.999, 257)), 1),
            ("2D f64", ((x, y) -> sin(x + y)), [0.0, 0.0], [1.0, 1.0], Float64, 1e-7, rand(200, 2), 1),
            ("2D->3D f64", ((x, y) -> (sin(x)*cos(y), x + y, x*y)), [0.0, 0.0], [1.0, 1.0], Float64, 1e-6, rand(200, 2), 3),
        ]
        # out_dim is explicit: the out_dim probe in fit would call g once.
        for (name, f, a, b, T, tol, X, od) in cases
            path  = joinpath(dir, replace(name, r"\W" => "_") * ".txt")
            calls = Ref(0)
            g = (args...) -> (calls[] += 1; f(args...))
            b1 = fit(g, a, b, tol; dtype=T, out_dim=od, filename=path)
            @test calls[] > 0
            @test isfile(path)

            calls[] = 0
            b2 = fit(g, a, b, tol; dtype=T, out_dim=od, filename=SubString(path, 1))   # AbstractString
            @test calls[] == 0
            r1, r2 = b1(X), b2(X)
            @test !any(isnan, r2)
            d1, d2 = Float64.(reshape(r1, :, od)), Float64.(reshape(r2, :, od))
            @test all(maximum(abs.(d1 .- d2); dims=1) .<= tol .* maximum(abs.(d1); dims=1))

            # A changed tol does not match the header: refit.
            calls[] = 0
            fit(g, a, b, tol * 2; dtype=T, out_dim=od, filename=path)
            @test calls[] > 0
        end

        # A malformed file with a matching header makes the fit throw.
        path = joinpath(dir, "bad.txt")
        f = x -> exp(x)
        fit(f, 0.0, 1.0, 1e-10; filename=path)
        write(path, read(path, String) * "extra\n")
        @test_throws ErrorException fit(f, 0.0, 1.0, 1e-10; filename=path)
    end
end

# Every return the out_dim probe sizes with `length` also fits.
@testset "callback return shapes" begin
    for T in (Float64, Float32)
        for (od, g) in ((1, x -> sin(x)), (1, x -> [sin(x)]), (1, x -> Float32(sin(x))),
                        (2, x -> (sin(x), cos(x))), (2, x -> [sin(x), cos(x)]),
                        (2, x -> [sin(x) cos(x)]), (2, x -> reshape([sin(x), cos(x)], 2, 1)),
                        (2, x -> Float32[sin(x), cos(x)]))
            b = fit(g, 0.0, 1.0, 1e-4; dtype=T)
            @test b.out_dim == od
            @test [b(0.3)...] ≈ [sin(0.3), cos(0.3)][1:od] atol=1e-3
        end
    end
    calls = Ref(0)
    grow  = x -> (calls[] += 1; calls[] == 1 ? [1.0, 2.0] : [1.0, 2.0, 3.0])
    err   = try fit(grow, 0.0, 1.0, 1e-4); nothing catch e; e end
    @test err isa ErrorException && occursin("returned 3 values, expected 2", err.msg)
end

println("All Treeweave tests passed.")
