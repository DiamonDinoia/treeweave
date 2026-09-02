Performance
===========

How to get the throughput treeweave is built for, and how it compares with
the other ways to approximate a function. Everything measured here is
re-measured by CI on every pull request.

Batch over scalar
-----------------

The batch entry points (``treeweave_batch`` / ``treeweave_sorted`` /
``treeweave_transposed`` in C; calling the fitted object with an array in the
bindings) amortize leaf lookup and vectorize the polynomial evaluation across
points. Prefer them to a scalar loop for anything beyond a handful of points.

This matters most in MATLAB/Octave, where single-point eval carries an extra
per-call overhead from the mwrap binding layer, not from treeweave; see
:ref:`matlab-eval-overhead`. The batch path amortises that overhead to ~zero.

The sorted fast path
~~~~~~~~~~~~~~~~~~~~~~

The general batch path first bin-sorts the inputs by leaf so each leaf's
points are contiguous, runs one vectorized Horner stream per leaf, then permutes
the results back to the caller's order. When the inputs are *already* ascending,
that sort and permute-back are pure overhead. The sorted path skips both and
streams straight through the leaves.

.. include:: ../_shared/sorted.src

Both paths NaN-fill out-of-domain points.

The leaf-table fast path
------------------------

``PolyTree::find_leaf_id`` has a SIMD-quantize + table-lookup fast path: one
``vcvttpd2qq`` (or scalar ``vcvttsd2si``) per point plus one ``uint32_t`` load
from a ``2^(input_dim * D)``-entry table, in place of recursive tree descent.
treeweave builds the table when the leaf index needs at most 16 bits
(``input_dim * D <= 16``) *and* the tree refined uniformly to depth ``D``.

For smooth functions, tolerance-based refinement stops early and the tree never
reaches uniform depth, so the fast path stays off. To turn it on, tighten
``tol`` until refinement is uniform, or set ``min_uniform_depth`` explicitly.
``Function::print_stats()`` reports ``Leaf table: live (N entries, K KiB)`` or
``Leaf table: descent-only``. Table memory grows as ``2^(input_dim * D) * 4 B``,
capped at ~256 KiB; past that treeweave skips the table.

Memory and degree
-----------------

- Tightening ``tol``, raising ``max_depth`` / ``max_memory_mib``, or enabling
  ``allow_max_depth_leaves`` all increase the leaf count and therefore memory.
- A SIMD tuning campaign picked the default leaf degree (7) as the best across
  architectures and dimensions, and the only spill-free degree in the
  register-pressured wide cells. Override it (C++ ``fit<N>``) only with
  measurements in hand.
- Oscillatory or rapidly-varying functions can use a *lot* of memory. Fit one
  period of a periodic function.

Multi-threading
---------------

A fitted object is immutable. Chunk the inputs and call the eval path from
multiple threads, one disjoint output slice each. treeweave never parallelizes
internally, which is what makes that contract safe.

Against the alternatives
------------------------

Four fields, one protocol: the interpolators a Python user reaches for, the
ones a Julia user reaches for, the ones a C++ user reaches for, and what a
stock Octave install offers. All four benchmarks run in CI on every pull
request, and each fails the build if its table below stops matching what it
measures.

In Python
~~~~~~~~~

``benchmarks/compare_interpolators.py`` puts treeweave next to six other ways to
approximate a 1-D function from Python:

.. list-table::
   :header-rows: 1
   :widths: 26 74

   * - method
     - what it is
   * - ``scipy CubicSpline``
     - the default reach for 1-D interpolation, on a uniform knot grid
   * - ``scipy quintic spline``
     - ``make_interp_spline(k=5)``, so the story is not just "order 3"
   * - ``scipy PchipInterpolator``
     - shape-preserving cubic Hermite: monotone, and third order for it
   * - ``numpy Chebyshev``
     - one global Chebyshev interpolant: spectral, but not adaptive
   * - ``sklearn spline features``
     - ``SplineTransformer`` + ``LinearRegression``, the route an ML codebase takes
   * - ``chebpy chebfun``
     - adaptive Chebyshev with interval splitting: the closest peer
   * - ``baobzi``
     - treeweave's predecessor, the same adaptive-tree idea

Every method is grown until it *meets* the requested accuracy on a dense test
grid, and only then measured. The splines double their knot count, the global
Chebyshev doubles its degree, and the adaptive ones are re-asked with a tighter
tolerance. So each row is measured at the accuracy it achieved, not at a
resolution someone guessed. A method that never reaches the accuracy is left in
the table with ``n/a`` rather than dropped.

``zeta(s)``, 1000 terms, on [2, 10]
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 7 26 9 11 8 9

   * - tol
     - method
     - f-evals
     - memory
     - Meval/s
     - max err
   * - 1e-6
     - treeweave
     - 151
     - 0.9 KiB
     - 205
     - 3.5e-07
   * - 1e-6
     - scipy CubicSpline
     - 256
     - 10.0 KiB
     - 20
     - 2.9e-07
   * - 1e-6
     - scipy quintic spline
     - 128
     - 2.0 KiB
     - 12
     - 1.1e-07
   * - 1e-6
     - scipy PchipInterpolator
     - 512
     - 20.0 KiB
     - 17
     - 5.9e-07
   * - 1e-6
     - numpy Chebyshev
     - 32
     - 0.2 KiB
     - 13
     - 4.3e-12
   * - 1e-6
     - sklearn spline features
     - 390
     - 2.1 KiB
     - 1
     - 1.9e-07
   * - 1e-6
     - chebpy chebfun
     - 50
     - 0.2 KiB
     - 15
     - 1.4e-07
   * - 1e-6
     - baobzi
     - 137
     - 1.2 KiB
     - 81
     - 6.5e-09
   * - 1e-10
     - treeweave
     - 601
     - 3.0 KiB
     - 224
     - 5.1e-11
   * - 1e-10
     - scipy CubicSpline
     - 2048
     - 80.0 KiB
     - 14
     - 7.7e-11
   * - 1e-10
     - scipy quintic spline
     - 512
     - 8.0 KiB
     - 7
     - 4.1e-11
   * - 1e-10
     - scipy PchipInterpolator
     - 16384
     - 640.0 KiB
     - 10
     - 1.9e-11
   * - 1e-10
     - numpy Chebyshev
     - 32
     - 0.2 KiB
     - 13
     - 4.3e-12
   * - 1e-10
     - sklearn spline features
     - 3078
     - 16.1 KiB
     - 0
     - 6.3e-11
   * - 1e-10
     - chebpy chebfun
     - 115
     - 0.3 KiB
     - 13
     - 3.2e-12
   * - 1e-10
     - baobzi
     - 665
     - 6.8 KiB
     - 80
     - 3.3e-14

``1/(x - 1.05)`` on [-1, 1]
^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 7 26 9 11 8 9

   * - tol
     - method
     - f-evals
     - memory
     - Meval/s
     - max err
   * - 1e-6
     - treeweave
     - 331
     - 1.6 KiB
     - 264
     - 7.5e-07
   * - 1e-6
     - scipy CubicSpline
     - 2048
     - 80.0 KiB
     - 16
     - 9.0e-08
   * - 1e-6
     - scipy quintic spline
     - 512
     - 8.0 KiB
     - 8
     - 9.1e-07
   * - 1e-6
     - scipy PchipInterpolator
     - 4096
     - 160.0 KiB
     - 14
     - 2.7e-07
   * - 1e-6
     - numpy Chebyshev
     - 64
     - 0.5 KiB
     - 7
     - 3.5e-09
   * - 1e-6
     - sklearn spline features
     - 1542
     - 8.1 KiB
     - 0
     - 7.7e-07
   * - 1e-6
     - chebpy chebfun
     - 115
     - 0.4 KiB
     - 9
     - 8.9e-08
   * - 1e-6
     - baobzi
     - 265
     - 2.3 KiB
     - 70
     - 2.3e-09
   * - 1e-10
     - treeweave
     - 1201
     - 5.9 KiB
     - 271
     - 5.5e-11
   * - 1e-10
     - scipy CubicSpline
     - 16384
     - 640.0 KiB
     - 11
     - 2.4e-11
   * - 1e-10
     - scipy quintic spline
     - 4096
     - 64.0 KiB
     - 2
     - 7.6e-12
   * - 1e-10
     - scipy PchipInterpolator
     - 65536
     - 2560.0 KiB
     - 10
     - 6.7e-11
   * - 1e-10
     - numpy Chebyshev
     - 128
     - 1.0 KiB
     - 4
     - 3.9e-13
   * - 1e-10
     - sklearn spline features
     - 8192
     - n/a
     - n/a
     - 3.0e-10
   * - 1e-10
     - chebpy chebfun
     - 244
     - 0.7 KiB
     - 5
     - 1.5e-12
   * - 1e-10
     - baobzi
     - 1209
     - 13.4 KiB
     - 57
     - 3.4e-14

``sin(30 x)`` on [0, 1]
^^^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 7 26 9 11 8 9

   * - tol
     - method
     - f-evals
     - memory
     - Meval/s
     - max err
   * - 1e-6
     - treeweave
     - 931
     - 3.2 KiB
     - 261
     - 4.6e-07
   * - 1e-6
     - scipy CubicSpline
     - 512
     - 20.0 KiB
     - 21
     - 3.3e-07
   * - 1e-6
     - scipy quintic spline
     - 256
     - 4.0 KiB
     - 11
     - 3.6e-08
   * - 1e-6
     - scipy PchipInterpolator
     - 16384
     - 640.0 KiB
     - 12
     - 3.8e-07
   * - 1e-6
     - numpy Chebyshev
     - 32
     - 0.2 KiB
     - 17
     - 1.2e-08
   * - 1e-6
     - sklearn spline features
     - 774
     - 4.1 KiB
     - 1
     - 2.9e-07
   * - 1e-6
     - chebpy chebfun
     - 115
     - 0.3 KiB
     - 14
     - 4.7e-08
   * - 1e-6
     - baobzi
     - 761
     - 4.5 KiB
     - 332
     - 4.5e-10
   * - 1e-10
     - treeweave
     - 3211
     - 11.9 KiB
     - 268
     - 7.0e-11
   * - 1e-10
     - scipy CubicSpline
     - 4096
     - 160.0 KiB
     - 14
     - 8.1e-11
   * - 1e-10
     - scipy quintic spline
     - 1024
     - 16.0 KiB
     - 6
     - 8.6e-12
   * - 1e-10
     - scipy PchipInterpolator
     - 1048576
     - 40960.0 KiB
     - 3
     - 5.6e-11
   * - 1e-10
     - numpy Chebyshev
     - 64
     - 0.5 KiB
     - 9
     - 3.9e-14
   * - 1e-10
     - sklearn spline features
     - 6150
     - 32.1 KiB
     - 0
     - 6.8e-11
   * - 1e-10
     - chebpy chebfun
     - 115
     - 0.3 KiB
     - 11
     - 1.2e-12
   * - 1e-10
     - baobzi
     - 3705
     - 28.8 KiB
     - 118
     - 8.5e-15

Intel Xeon w5-3435X, one core, Python 3.12.9, NumPy 2.5.2, SciPy 1.18.1,
scikit-learn 1.9.0, chebpy 0.11.0, baobzi 0.9.6, 2026-09-01,
``taskset -c 31 python benchmarks/compare_interpolators.py``. The f-evals and
memory columns are deterministic and reproduce exactly; CI re-measures them on
every pull request and fails if they stop matching the tables above. Meval/s was
measured on a shared machine and moves by up to a factor of two between runs, so
read that column as an order of magnitude.

Reading the tables:

* **Throughput separates compiled trees from Python objects.** treeweave holds
  205 to 271 Meval/s on every row; baobzi, the other compiled tree, holds 57 to
  332. Everything that evaluates through a SciPy, NumPy or scikit-learn object
  lands at 21 or below, down to the scikit-learn rows near 0.1. All of them take
  one vectorized call per batch, so this is the cost of the evaluation itself,
  not of the Python layer.
* **Against a uniform knot grid, adaptivity wins on size.** At 1e-10 on
  ``1/(x - 1.05)``, the cubic spline needs 16384 knots and 640 KiB and the
  quintic 4096 and 64 KiB; treeweave needs 1201 calls and 5.9 KiB. That factor
  of 108 against the cubic spline is the argument for adaptive panels.
* **One global Chebyshev series is the compact winner when it applies, and it
  is slow.** ``1/(x - 1.05)`` is analytic on [-1, 1], so a Chebyshev series
  converges geometrically: 128 coefficients and 1.0 KiB at 1e-10, a sixth of
  treeweave's footprint. It evaluates at 4 Meval/s against treeweave's 271,
  and it has no answer at all once the function stops being analytic on the
  domain. Reach for it when the fit is built once and evaluated rarely.
* **The scikit-learn route is a fitter, not an interpolator.** Its floor is set
  by the conditioning of the least-squares system, not by the knot spacing: it
  reaches 1e-10 on the two smooth targets only through thousands of samples,
  and on ``1/(x - 1.05)`` it never gets there at 8192 samples. It is also the
  slowest to evaluate by two orders of magnitude.
* **Monotonicity is expensive.** ``PchipInterpolator`` is a cubic Hermite form
  that preserves the shape of the data, and it pays for that with its
  convergence rate. On ``sin(30 x)`` at 1e-10 it needs the whole 1048576-knot
  cap and 40960 KiB, against the not-a-knot spline's 4096 and 160 KiB and
  treeweave's 3211 and 11.9 KiB. Its six rows are also the six ``pchip`` rows
  in the Octave table below, knot for knot and digit for digit, which is what a
  shared algorithm looks like across two implementations.
* **Oscillation is where adaptivity has nothing to exploit.** On
  ``sin(30 x)`` at 1e-6 the cubic spline needs 512 calls against treeweave's
  931, and one global Chebyshev needs 32. A uniform grid is already the right
  answer for a function with no local structure; treeweave keeps its throughput
  lead there and gives up its f-eval lead.
* **Read the max-err column before comparing costs.** Every method overshoots
  the request by the granularity of its own growth step. treeweave and the
  splines land within a decade of the tolerance; chebpy and baobzi land one to
  four decades inside it, so part of their cost buys accuracy nobody asked for.
* **A singularity on or inside the domain is a different question.** No method
  in this table is fitted through one. treeweave refuses, with a diagnostic
  naming the offending panel and the three ways out; see the singularity
  walk-through on the :doc:`front page </index>`.

In Julia
~~~~~~~~

``benchmarks/compare_interpolators.jl`` runs the same protocol against the
interpolators a Julia user reaches for:

.. list-table::
   :header-rows: 1
   :widths: 26 74

   * - method
     - what it is
   * - ``Interpolations.jl cubic``
     - ``cubic_spline_interpolation`` on a uniform grid: the default reach
   * - ``Dierckx.jl quintic``
     - ``Spline1D(k=5, s=0.0)``, FITPACK, the same engine behind scipy's splines
   * - ``FastChebInterp.jl``
     - one global Chebyshev series: spectral, not adaptive
   * - ``DataInterpolations cubic``
     - ``CubicSpline``, the SciML stack's spline, natural at the ends
   * - ``DataInterpolations PCHIP``
     - ``PCHIPInterpolation``, the monotone cubic Hermite of that stack

Chebfun's adaptive-Chebyshev algorithm is already in the Python table through
chebpy; ApproxFun.jl exposes no tolerance dial this protocol can drive, so it is
not in the field.

``zeta(s)``, 1000 terms, on [2, 10]
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 7 26 9 11 8 9

   * - tol
     - method
     - f-evals
     - memory
     - Meval/s
     - max err
   * - 1e-6
     - treeweave
     - 151
     - 0.9 KiB
     - 187
     - 3.5e-07
   * - 1e-6
     - Interpolations.jl cubic
     - 2048
     - 16.0 KiB
     - 101
     - 8.8e-07
   * - 1e-6
     - Dierckx.jl quintic
     - 128
     - 2.0 KiB
     - 10
     - 1.1e-07
   * - 1e-6
     - FastChebInterp.jl
     - 32
     - 0.2 KiB
     - 64
     - 4.9e-12
   * - 1e-6
     - DataInterpolations cubic
     - 2048
     - 64.0 KiB
     - 27
     - 8.8e-07
   * - 1e-6
     - DataInterpolations PCHIP
     - 512
     - 12.0 KiB
     - 30
     - 5.9e-07
   * - 1e-10
     - treeweave
     - 601
     - 3.0 KiB
     - 253
     - 5.1e-11
   * - 1e-10
     - Interpolations.jl cubic
     - 262144
     - 2048.0 KiB
     - 91
     - 1.4e-11
   * - 1e-10
     - Dierckx.jl quintic
     - 512
     - 8.0 KiB
     - 7
     - 4.1e-11
   * - 1e-10
     - FastChebInterp.jl
     - 32
     - 0.2 KiB
     - 62
     - 4.9e-12
   * - 1e-10
     - DataInterpolations cubic
     - 262144
     - 8192.0 KiB
     - 14
     - 1.4e-11
   * - 1e-10
     - DataInterpolations PCHIP
     - 16384
     - 384.0 KiB
     - 28
     - 1.9e-11

``1/(x - 1.05)`` on [-1, 1]
^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 7 26 9 11 8 9

   * - tol
     - method
     - f-evals
     - memory
     - Meval/s
     - max err
   * - 1e-6
     - treeweave
     - 331
     - 1.6 KiB
     - 233
     - 7.5e-07
   * - 1e-6
     - Interpolations.jl cubic
     - 16384
     - 128.0 KiB
     - 100
     - 5.8e-07
   * - 1e-6
     - Dierckx.jl quintic
     - 512
     - 8.0 KiB
     - 7
     - 9.1e-07
   * - 1e-6
     - FastChebInterp.jl
     - 64
     - 0.5 KiB
     - 23
     - 2.4e-09
   * - 1e-6
     - DataInterpolations cubic
     - 16384
     - 512.0 KiB
     - 28
     - 5.8e-07
   * - 1e-6
     - DataInterpolations PCHIP
     - 4096
     - 96.0 KiB
     - 27
     - 2.7e-07
   * - 1e-10
     - treeweave
     - 1201
     - 5.9 KiB
     - 247
     - 5.5e-11
   * - 1e-10
     - Interpolations.jl cubic
     - 524288
     - 4096.0 KiB
     - 84
     - 3.1e-11
   * - 1e-10
     - Dierckx.jl quintic
     - 4096
     - 64.0 KiB
     - 2
     - 7.6e-12
   * - 1e-10
     - FastChebInterp.jl
     - 128
     - 1.0 KiB
     - 7
     - 5.3e-15
   * - 1e-10
     - DataInterpolations cubic
     - 524288
     - 16384.0 KiB
     - 9
     - 3.1e-11
   * - 1e-10
     - DataInterpolations PCHIP
     - 65536
     - 1536.0 KiB
     - 22
     - 6.7e-11

``sin(30 x)`` on [0, 1]
^^^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 7 26 9 11 8 9

   * - tol
     - method
     - f-evals
     - memory
     - Meval/s
     - max err
   * - 1e-6
     - treeweave
     - 931
     - 3.2 KiB
     - 220
     - 4.6e-07
   * - 1e-6
     - Interpolations.jl cubic
     - 8192
     - 64.0 KiB
     - 90
     - 6.5e-07
   * - 1e-6
     - Dierckx.jl quintic
     - 256
     - 4.0 KiB
     - 7
     - 3.6e-08
   * - 1e-6
     - FastChebInterp.jl
     - 32
     - 0.2 KiB
     - 58
     - 2.0e-08
   * - 1e-6
     - DataInterpolations cubic
     - 8192
     - 256.0 KiB
     - 24
     - 6.5e-07
   * - 1e-6
     - DataInterpolations PCHIP
     - 16384
     - 384.0 KiB
     - 24
     - 3.8e-07
   * - 1e-10
     - treeweave
     - 3211
     - 11.9 KiB
     - 224
     - 7.0e-11
   * - 1e-10
     - Interpolations.jl cubic
     - 524288
     - 4096.0 KiB
     - 74
     - 8.6e-12
   * - 1e-10
     - Dierckx.jl quintic
     - 1024
     - 16.0 KiB
     - 4
     - 8.6e-12
   * - 1e-10
     - FastChebInterp.jl
     - 64
     - 0.5 KiB
     - 19
     - 5.7e-15
   * - 1e-10
     - DataInterpolations cubic
     - 524288
     - 16384.0 KiB
     - 10
     - 8.6e-12
   * - 1e-10
     - DataInterpolations PCHIP
     - 1048576
     - 24576.0 KiB
     - 7
     - 5.6e-11

Intel Xeon w5-3435X, one core, Julia 1.12.7, Interpolations.jl 0.16.3,
Dierckx.jl 0.5.4, FastChebInterp.jl 1.3.1, DataInterpolations.jl 10.1.0,
2026-09-01,
``taskset -c 31 julia --project=... benchmarks/compare_interpolators.jl``. As in
the Python table, CI re-measures f-evals and memory on every pull request and
fails if they stop matching; Meval/s is an order of magnitude, not a number.

Reading the Julia tables:

* **The same tree costs the same from either language.** treeweave's f-evals and
  memory are identical row for row to the Python table: 601 calls and 3.0 KiB on
  ``zeta(s)`` at 1e-10, 1201 and 5.9 KiB on the pole, 3211 and 11.9 KiB on the
  oscillation. Throughput agrees to within its own noise, 187 to 253 Meval/s from
  Julia against 205 to 271 from Python. The bindings add nothing measurable to a
  batched call.
* **Dierckx.jl and scipy's quintic spline are the same FITPACK.** Every f-eval and
  memory cell matches between the two tables, and both evaluate at 2 to 12 Meval/s.
  That column is a property of FITPACK's evaluator, not of either wrapper.
* **FastChebInterp.jl and numpy's Chebyshev are the same series, evaluated
  differently.** Same degree and same 0.2 to 1.0 KiB in every row, but 7 to
  64 Meval/s against numpy's 4 to 17. A compiled Clenshaw loop is worth one and
  a half to five times a NumPy object, and still under treeweave's rate on every
  row.
* **A native Julia spline is faster than the same spline through scipy, and the
  algorithm still decides.** Interpolations.jl holds 74 to 101 Meval/s against
  ``scipy CubicSpline``'s 11 to 21, a five- to sevenfold win that costs nothing
  but the language. It is under half of treeweave's rate, and it needs 262144
  knots where treeweave needs 601 calls.
* **Julia's default cubic pays for its boundary condition, not for its
  interior.** ``cubic_spline_interpolation`` defaults to ``bc = Line(OnGrid())``,
  the natural condition; scipy's ``CubicSpline`` defaults to not-a-knot. On
  ``zeta(s)`` at 1e-10 that is 262144 knots against scipy's 2048. The error is a
  boundary layer: with 2048 knots the max error is 8.8e-7, at 0.38 knot spacings
  from the left endpoint, while 5% of the domain in from either end it is
  1.6e-12, a factor of 5e5. Those three figures are one measurement; what CI
  enforces on every run is the shape they describe, that the max error exceeds
  1e-7, sits within one knot spacing of an endpoint, and falls below 1e-11 in
  the interior (``check_boundary_layer``). A max-error tolerance therefore buys
  grid for the two endpoints and pays for it everywhere. Pass an explicit ``bc``
  if the endpoints are not where the accuracy is needed.
* **DataInterpolations.jl confirms the boundary condition, from a second
  package.** Its ``CubicSpline`` is natural at the ends too, and its f-eval count
  and achieved error match Interpolations.jl in all six rows, 262144 knots and
  1.4e-11 on ``zeta(s)`` at 1e-10 included. Two natural-condition
  implementations need 262144 knots there; the two not-a-knot ones, scipy and
  Octave, need 2048. The condition decides, not the package. What differs is
  storage: DataInterpolations keeps ``u``, ``t``, ``h`` and ``z``, four doubles
  per knot, against Interpolations.jl's one.
* **Three PCHIP implementations agree to the digit.**
  ``PCHIPInterpolation`` matches ``scipy PchipInterpolator`` and Octave's
  ``pchip`` on knots and error in every row, and it is the cheapest of the
  three to store: 24 bytes per knot for ``du``, ``u`` and ``t``, against the 40
  that a coefficient-major cubic pp costs. On ``sin(30 x)`` at 1e-10 all three
  hit the 1048576-knot cap; monotone Hermite is third order and no wrapper can
  change that.

In C++
~~~~~~

``benchmarks/compare_interpolators.cpp`` runs the same protocol against the
interpolators a C++ user reaches for:

.. list-table::
   :header-rows: 1
   :widths: 26 74

   * - method
     - what it is
   * - ``boost cardinal cubic``
     - ``cardinal_cubic_b_spline``, cubic B-spline on a uniform grid
   * - ``boost cardinal quintic``
     - ``cardinal_quintic_b_spline``, quintic B-spline on a uniform grid

Boost.Math's ``barycentric_rational`` is not in the field: it costs O(n) per
evaluation, so growing it to 1e-10 costs more throughput than the table can
report. GSL's ``gsl_spline`` cubic is the same algorithm as Boost's cardinal
cubic on a uniform grid, so it would add a row and no information. That is
also why the C binding has no table of its own: a C user's alternatives are
GSL's splines, and this table already measures the algorithm behind them.

``zeta(s)``, 1000 terms, on [2, 10]
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 7 26 9 11 8 9

   * - tol
     - method
     - f-evals
     - memory
     - Meval/s
     - max err
   * - 1e-6
     - treeweave
     - 150
     - 0.9 KiB
     - 304
     - 3.5e-07
   * - 1e-6
     - boost cardinal cubic
     - 128
     - 1.1 KiB
     - 121
     - 7.0e-07
   * - 1e-6
     - boost cardinal quintic
     - 128
     - 1.1 KiB
     - 22
     - 6.6e-08
   * - 1e-10
     - treeweave
     - 600
     - 3.4 KiB
     - 316
     - 5.1e-11
   * - 1e-10
     - boost cardinal cubic
     - 2048
     - 16.1 KiB
     - 123
     - 7.4e-12
   * - 1e-10
     - boost cardinal quintic
     - 512
     - 4.1 KiB
     - 22
     - 8.1e-12

``1/(x - 1.05)`` on [-1, 1]
^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 7 26 9 11 8 9

   * - tol
     - method
     - f-evals
     - memory
     - Meval/s
     - max err
   * - 1e-6
     - treeweave
     - 330
     - 1.9 KiB
     - 295
     - 7.5e-07
   * - 1e-6
     - boost cardinal cubic
     - 1024
     - 8.1 KiB
     - 118
     - 1.7e-07
   * - 1e-6
     - boost cardinal quintic
     - 512
     - 4.1 KiB
     - 22
     - 6.7e-07
   * - 1e-10
     - treeweave
     - 1200
     - 8.1 KiB
     - 302
     - 5.5e-11
   * - 1e-10
     - boost cardinal cubic
     - 8192
     - 64.1 KiB
     - 122
     - 3.7e-11
   * - 1e-10
     - boost cardinal quintic
     - 4096
     - 32.1 KiB
     - 22
     - 1.3e-12

``sin(30 x)`` on [0, 1]
^^^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 7 26 9 11 8 9

   * - tol
     - method
     - f-evals
     - memory
     - Meval/s
     - max err
   * - 1e-6
     - treeweave
     - 930
     - 3.4 KiB
     - 320
     - 4.6e-07
   * - 1e-6
     - boost cardinal cubic
     - 256
     - 2.1 KiB
     - 123
     - 7.6e-07
   * - 1e-6
     - boost cardinal quintic
     - 256
     - 2.1 KiB
     - 22
     - 1.1e-08
   * - 1e-10
     - treeweave
     - 3210
     - 13.1 KiB
     - 315
     - 7.0e-11
   * - 1e-10
     - boost cardinal cubic
     - 4096
     - 32.1 KiB
     - 120
     - 7.5e-12
   * - 1e-10
     - boost cardinal quintic
     - 512
     - 4.1 KiB
     - 22
     - 8.9e-11

Intel Xeon w5-3435X, one core, GCC 13.3.0, Boost 1.87.0, ``-O3 -march=native``,
2026-09-01,
``taskset -c 31 build/bench/compare_interpolators_cpp``. Memory here is counted
at the global allocator: the bytes requested by blocks still live after the
approximation is built. That is why treeweave's memory column sits above the
``memory_usage()`` figures in the Python and Julia tables, which count the
tree's own storage and not its vectors' spare capacity. The gap reaches 37% on
the pole at 1e-10, 8.1 KiB against 5.9, and is invisible on ``zeta(s)`` at 1e-6:
a ``std::vector`` that has just doubled carries the most idle capacity. The
self-test holds the two numbers within a factor of two of each other. The count
does not move with the instruction set: an ``x86-64-v3`` build and an AVX-512
build report the same bytes. This is the only arm that times the *scalar* path: Boost's cardinal
splines expose one ``operator()(double)`` and nothing batched, so treeweave is
called the same way, one point per call. The Python, Julia and Octave arms hand
their evaluator the whole 1e6-point array. Compare rates within this table, not
across tables.

Reading the C++ tables:

* **Compiled against compiled, the throughput gap is the evaluator.** treeweave
  holds 294 to 320 Meval/s, Boost's cardinal cubic 118 to 123, and its cardinal
  quintic 22 on every row. No binding sits in any of these paths, so what separates
  them is the work per point: a degree-8 leaf on a flat panel array against a
  B-spline's knot arithmetic.
* **Adaptivity wins the pole in C++ by the same factor as everywhere else.** At
  1e-10 on ``1/(x - 1.05)``, treeweave needs 1200 calls and 8.1 KiB; the
  cardinal cubic needs 8192 and 64.1 KiB, the cardinal quintic 4096 and
  32.1 KiB. Nothing about the argument was a property of Python.
* **Without local structure the uniform grid still wins on samples.** The
  cardinal cubic reaches 1e-6 on ``zeta(s)`` in 128 calls against treeweave's
  150, and the cardinal quintic reaches 1e-10 on ``sin(30 x)`` in 512 against
  3210. Oscillation gives refinement nothing to refine.
* **Cardinal splines are the most compact splines in the comparison.** The grid
  is implicit, so the object holds n coefficients and no knot vector: 512
  samples at 1e-10 on ``zeta(s)`` cost 4.1 KiB here against the 8.0 KiB scipy
  and Dierckx spend on the same 512.
* **The treeweave rows are the same fit as in the other tables.** Its f-eval
  counts sit exactly one below the Python, Julia and Octave rows on all six.
  When the caller does not pass ``out_dim``, each wrapper infers it by calling
  ``f`` once at the box midpoint
  (``bindings/python/treeweave/__init__.py:305``,
  ``bindings/julia/Treeweave/src/Treeweave.jl:257``,
  ``bindings/matlab/treeweave.m:68``). The C++ API reads the output dimension
  from the return type, so it never probes.

In Octave
~~~~~~~~~

``benchmarks/compare_interpolators.m`` runs the same protocol against what a
stock Octave or MATLAB install offers:

.. list-table::
   :header-rows: 1
   :widths: 26 74

   * - method
     - what it is
   * - ``spline (not-a-knot cubic)``
     - ``spline(x, y)`` + ``ppval``, the not-a-knot cubic on a uniform grid
   * - ``pchip (monotone cubic)``
     - shape-preserving cubic Hermite, third order

``interp1(..., 'spline')`` and ``interp1(..., 'pchip')`` build the same
piecewise polynomials and are left out as duplicates. Neither core Octave nor
the octave-forge packages on the CI runner offer an interpolant with a
tolerance dial or a Chebyshev series to grow, so the spectral and adaptive
peers appear only in the Python, Julia and C++ tables.

``zeta(s)``, 1000 terms, on [2, 10]
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 7 26 9 11 8 9

   * - tol
     - method
     - f-evals
     - memory
     - Meval/s
     - max err
   * - 1e-6
     - treeweave
     - 151
     - 0.9 KiB
     - 150
     - 3.5e-07
   * - 1e-6
     - spline (not-a-knot cubic)
     - 256
     - 10.0 KiB
     - 12
     - 2.9e-07
   * - 1e-6
     - pchip (monotone cubic)
     - 512
     - 20.0 KiB
     - 11
     - 5.9e-07
   * - 1e-10
     - treeweave
     - 601
     - 3.0 KiB
     - 156
     - 5.1e-11
   * - 1e-10
     - spline (not-a-knot cubic)
     - 2048
     - 80.0 KiB
     - 10
     - 7.7e-11
   * - 1e-10
     - pchip (monotone cubic)
     - 16384
     - 640.0 KiB
     - 8
     - 1.9e-11

``1/(x - 1.05)`` on [-1, 1]
^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 7 26 9 11 8 9

   * - tol
     - method
     - f-evals
     - memory
     - Meval/s
     - max err
   * - 1e-6
     - treeweave
     - 331
     - 1.6 KiB
     - 141
     - 7.5e-07
   * - 1e-6
     - spline (not-a-knot cubic)
     - 2048
     - 80.0 KiB
     - 10
     - 9.0e-08
   * - 1e-6
     - pchip (monotone cubic)
     - 4096
     - 160.0 KiB
     - 10
     - 2.7e-07
   * - 1e-10
     - treeweave
     - 1201
     - 5.9 KiB
     - 152
     - 5.5e-11
   * - 1e-10
     - spline (not-a-knot cubic)
     - 16384
     - 640.0 KiB
     - 8
     - 2.4e-11
   * - 1e-10
     - pchip (monotone cubic)
     - 65536
     - 2560.0 KiB
     - 7
     - 6.7e-11

``sin(30 x)`` on [0, 1]
^^^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 7 26 9 11 8 9

   * - tol
     - method
     - f-evals
     - memory
     - Meval/s
     - max err
   * - 1e-6
     - treeweave
     - 931
     - 3.2 KiB
     - 159
     - 4.6e-07
   * - 1e-6
     - spline (not-a-knot cubic)
     - 512
     - 20.0 KiB
     - 11
     - 3.3e-07
   * - 1e-6
     - pchip (monotone cubic)
     - 16384
     - 640.0 KiB
     - 8
     - 3.8e-07
   * - 1e-10
     - treeweave
     - 3211
     - 11.9 KiB
     - 154
     - 7.0e-11
   * - 1e-10
     - spline (not-a-knot cubic)
     - 4096
     - 160.0 KiB
     - 10
     - 8.1e-11
   * - 1e-10
     - pchip (monotone cubic)
     - 1048576
     - 40960.0 KiB
     - 3
     - 5.6e-11

Intel Xeon w5-3435X, one core, Octave 9.4.0, 2026-09-01,
``taskset -c 28 octave --eval "compare_interpolators('--rst')"``. Memory here is
what an evaluation reads out of the ``pp`` struct: ``breaks`` plus ``coefs``,
five doubles per knot for a cubic.

Reading the Octave tables:

* **The boundary condition, not the language.** Octave's ``spline`` is
  not-a-knot, and so is scipy's ``CubicSpline``. Every one of the six rows
  agrees: 2048 knots and 80.0 KiB for 1e-10 on ``zeta(s)``, 16384 and 640.0 KiB
  for 1e-10 near the pole, down to the achieved error. That settles what the
  262144 knots in the Julia table mean. ``cubic_spline_interpolation`` defaults
  to the natural boundary condition, which leaves a boundary layer no amount of
  interior accuracy pays for. It is not a property of Julia.
  ``compare_interpolators.m --check-docs`` fails the build if the two published
  tables ever stop agreeing.
* **Monotonicity is expensive.** ``pchip`` is third order where the cubic spline
  is fourth, and the gap compounds: 1e-10 on ``sin(30 x)`` takes the whole
  1048576-knot cap and 40.0 MiB, against treeweave's 3211 calls and 11.9 KiB.
  Reach for it when overshoot matters, not when accuracy does.
* **Adaptivity wins the pole here too.** At 1e-10 on ``1/(x - 1.05)`` treeweave
  needs 1201 calls and 5.9 KiB; the not-a-knot cubic needs 16384 and 640.0 KiB,
  ``pchip`` 65536 and 2560.0 KiB. Same factors as the Python, Julia and C++
  tables.
* **The interpreter is not what separates the rows.** All three methods evaluate
  the same 1e6 points from the same Octave prompt. treeweave holds 141 to
  159 Meval/s through one batched MEX call; ``ppval`` runs at 3 to 12, and its
  rate falls as the knot count grows: the interval lookup is a search over
  ``breaks``, and at 1048576 knots the ``pp`` struct is 40.0 MiB, well past any
  cache.

Not in the comparison: the JavaScript and Fortran bindings. Fortran's spline of
record is FITPACK, and its numbers are already in two tables above, under
``scipy quintic spline`` and ``Dierckx.jl quintic``, which both wrap it. The
JavaScript packages resample data rather than approximate a callable to a
tolerance, so there is nothing there to grow against this protocol.

Cross-language throughput and latency
--------------------------------------

Riemann-zeta sum benchmarked from all 7 bindings against native recomputation.
Bars: Mevals/s (log scale); labels: within-language speedup.
Cross-language Mevals/s is not comparable, because the CI runners differ.
Compare each language's treeweave bar to its own native bar.
The ``showcase`` job in ``benchmarks.yml`` regenerates these charts.

Throughput, Mevals/s, higher is better
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. image:: https://raw.githubusercontent.com/DiamonDinoia/treeweave/benchmark-results/throughput_multi.svg
   :alt: Riemann-zeta batch throughput
   :width: 100%

.. image:: https://raw.githubusercontent.com/DiamonDinoia/treeweave/benchmark-results/throughput_sorted.svg
   :alt: Riemann-zeta sorted-batch throughput
   :width: 100%

.. image:: https://raw.githubusercontent.com/DiamonDinoia/treeweave/benchmark-results/throughput_single.svg
   :alt: Riemann-zeta single-eval throughput
   :width: 100%

Latency, ns/eval, lower is better
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. image:: https://raw.githubusercontent.com/DiamonDinoia/treeweave/benchmark-results/latency_multi.svg
   :alt: Riemann-zeta batch latency
   :width: 100%

.. image:: https://raw.githubusercontent.com/DiamonDinoia/treeweave/benchmark-results/latency_sorted.svg
   :alt: Riemann-zeta sorted-batch latency
   :width: 100%

.. image:: https://raw.githubusercontent.com/DiamonDinoia/treeweave/benchmark-results/latency_single.svg
   :alt: Riemann-zeta single-eval latency
   :width: 100%

Batch vs sorted batch
~~~~~~~~~~~~~~~~~~~~~~~

.. image:: https://raw.githubusercontent.com/DiamonDinoia/treeweave/benchmark-results/sorted_vs_unsorted_throughput.svg
   :alt: treeweave batch vs sorted batch throughput
   :width: 100%

.. image:: https://raw.githubusercontent.com/DiamonDinoia/treeweave/benchmark-results/sorted_vs_unsorted_latency.svg
   :alt: treeweave batch vs sorted batch latency
   :width: 100%
