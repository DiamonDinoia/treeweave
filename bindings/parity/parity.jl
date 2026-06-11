# parity.jl — Julia side of the cross-language parity check.
#
# Fits the same 2D -> 3D kernel as reference.c and prints `x0,x1,y0,y1,y2` at
# the identical fixed points and full-precision format, so run_parity.sh can
# diff it against the C reference.

using Printf
using Treeweave

const PTS = [(0.3, 0.4), (0.7, 1.1), (1.0, 0.5), (1.2, 1.3), (0.5, 0.9)]

kernel(x, y) = (exp(0.3x) + sin(2y), cos(x * y) + 2.0, x^2 + y + 1.0)

function main()
    b = fit((x, y) -> kernel(x, y), [0.2, 0.2], [1.5, 1.5], 1e-8; out_dim = 3)
    for (x0, x1) in PTS
        y = b([x0, x1])
        @printf("%.17g,%.17g,%.17g,%.17g,%.17g\n", x0, x1, y[1], y[2], y[3])
    end
end

main()
