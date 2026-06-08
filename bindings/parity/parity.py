"""parity.py — Python side of the cross-language parity check.

Fits the same 2D -> 3D kernel as reference.c and prints `x0,x1,y0,y1,y2` at the
identical fixed points, in the identical full-precision format, so run_parity.sh
can diff it against the C reference.
"""

import numpy as np

import treeweave

PTS = [(0.3, 0.4), (0.7, 1.1), (1.0, 0.5), (1.2, 1.3), (0.5, 0.9)]


def kernel(x):
    return np.array([
        np.exp(0.3 * x[0]) + np.sin(2.0 * x[1]),
        np.cos(x[0] * x[1]) + 2.0,
        x[0] * x[0] + x[1] + 1.0,
    ])


def main():
    fn = treeweave.fit(kernel, [0.2, 0.2], [1.5, 1.5], tol=1e-8, out_dim=3)
    for x0, x1 in PTS:
        y = fn([x0, x1])
        print("%.17g,%.17g,%.17g,%.17g,%.17g" % (x0, x1, y[0], y[1], y[2]))


if __name__ == "__main__":
    main()
