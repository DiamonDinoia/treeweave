# Phase 0 baseline

Host: Intel Core Ultra 7 155H (Meteor Lake), AVX2 (xsimd `batch<double>::size = 4`).
Built `Release -O3 -march=native -DNDEBUG -g -fno-omit-frame-pointer`.
Pinned to one P-core via `taskset -c 2 ./baobzi_microbench`.

Microbench source: `examples/c++/baobzi_microbench.cpp` (sweeps {1D,2D,3D}
× {deg 6,8,10} × N ∈ {1, 32, 1024, 1'000'000}).

Functions covered:

- 1D: Runge `1/(1+25x²)`; `erf`; Bessel `J0`; sharp `tanh(50x)`; `log1p`.
- 2D: anisotropic Gaussian bump; oscillatory `cos(8x)cos(8y)`;
  multiquadric RBF `√(1+r²)`.
- 3D: isotropic Gaussian; Yukawa `e^{-r}/r` off-singularity;
  inverse multiquadric `1/√(1+r²)`.

The cycles/point column should be normalised by SIMD lane width when
comparing across machines (AVX2 W=4 → AVX-512 W=8 ≈ 2× nominal speedup
for SIMD-bound segments).

Raw output: `bench/baseline.txt`.
