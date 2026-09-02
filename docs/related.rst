.. _related:

Related packages
================

treeweave does one thing: fit a smooth function on a box to a requested
tolerance, then evaluate it fast. The packages below overlap that, or cover
what treeweave leaves out. Every claim here is about scope, not a benchmark;
where a speed comparison is asserted, it comes from the cited source. Measured
comparisons are in :doc:`guides/performance`, against ``scipy``, ``numpy``,
``scikit-learn``, ``chebpy`` and ``baobzi`` in Python, Interpolations.jl,
Dierckx.jl, FastChebInterp.jl and DataInterpolations.jl in Julia, Boost.Math's
cardinal B-splines in C++, and ``spline``/``pchip`` in Octave.

Adaptive polynomial interpolation
---------------------------------

* `baobzi <https://github.com/flatironinstitute/baobzi>`__: treeweave's
  predecessor, also from CCM. Same idea, adaptive Chebyshev boxes in 1-D to
  3-D, with C/C++/Fortran/Python/Julia/MATLAB bindings. treeweave changes the
  leaf representation (monomial at fixed low degree instead of Chebyshev), adds
  the counting-sort batch path, the runtime ISA dispatch and the guru
  interface, and takes the dimension as a template parameter rather than a
  fixed set.

* `Chebfun <https://www.chebfun.org/>`__ (MATLAB) and
  `ApproxFun.jl <https://JuliaApproximation.github.io/ApproxFun.jl/dev>`__
  (Julia): the reference implementations of the underlying idea, and far more
  general. They give you an *object algebra*, so a chebfun can be
  differentiated, integrated, rootfound, and used to solve ODEs. treeweave does
  none of that; it optimises evaluation throughput from compiled languages and
  ships a C ABI.

* `HChebInterp.jl <https://github.com/lxvm/HChebInterp.jl>`__: h-adaptive
  Chebyshev interpolation in Julia, the closest single-purpose match to
  treeweave's scope. Julia-only.

* `chebpy <https://github.com/chebpy/chebpy>`__: a Python port of Chebfun's
  1-D core.

Fixed-grid interpolation
------------------------

These take samples, not a callable, so the caller chooses the resolution and
owns whatever error follows. They are the right tool when the data *is* a table
and no callable exists.

* `scipy.interpolate <https://docs.scipy.org/doc/scipy/reference/interpolate.html>`__:
  ``CubicSpline``, ``RegularGridInterpolator``, ``RectBivariateSpline`` and
  friends. Algebraic convergence, :math:`\mathcal{O}(h^4)` for a cubic spline.

* `GSL interpolation <https://www.gnu.org/software/gsl/doc/html/interp.html>`__
  and `Boost.Math interpolators
  <https://www.boost.org/doc/libs/release/libs/math/doc/html/math_toolkit/interpolation.html>`__:
  the C and C++ equivalents, including Boost's cardinal B-splines and its
  barycentric rational interpolant.

* `Interpolations.jl <https://github.com/JuliaMath/Interpolations.jl>`__,
  `Dierckx.jl <https://github.com/kbarbary/Dierckx.jl>`__ and
  `DataInterpolations.jl <https://github.com/SciML/DataInterpolations.jl>`__:
  the Julia equivalents. Dierckx.jl wraps FITPACK, the same Fortran library
  behind ``scipy``'s splines. Interpolations.jl's
  ``cubic_spline_interpolation`` and DataInterpolations.jl's ``CubicSpline``
  both default to the natural boundary condition, which costs them up to two
  orders of magnitude in knots on the targets in
  :doc:`guides/performance`.

* MATLAB and Octave's ``spline``, ``pchip`` and ``interp1``: the same
  piecewise cubics. ``spline`` is not-a-knot, ``pchip`` trades a convergence
  order for monotonicity.

* `scikit-learn <https://scikit-learn.org/>`__: regression and kernel methods
  over scattered data. Different problem. It fits noisy samples with a
  statistical model; treeweave approximates a deterministic function to a
  requested accuracy.

Higher dimensions
-----------------

Past about :math:`d = 4` a tensor-product panel is the wrong data structure, because
its coefficient count is :math:`(\text{degree}+1)^d`. Low-rank and sparse
representations are what to reach for:

* `TensorCrossInterpolation.jl
  <https://github.com/tensor4all/TensorCrossInterpolation.jl>`__ and
  `xfac <https://github.com/tensor4all/xfac>`__: tensor-cross interpolation,
  i.e. tensor trains built from function evaluations.
* `TASMANIAN <https://github.com/ORNL/TASMANIAN>`__: adaptive sparse grids
  for high-dimensional integration and interpolation, from ORNL, with C++,
  Python and MATLAB interfaces.
* `SmolyakApprox.jl <https://github.com/RJDennis/SmolyakApprox.jl>`__: Smolyak
  sparse-grid approximation in Julia.

Scattered data and unbounded domains
------------------------------------

treeweave requires a callable and a bounded box. For scattered samples, look at
radial basis functions, such as `SciPy's RBFInterpolator
<https://docs.scipy.org/doc/scipy/reference/generated/scipy.interpolate.RBFInterpolator.html>`__,
or Gaussian process regression. For an unbounded domain, map it onto a bounded
one and fit the image of the map.

Related Flatiron libraries
--------------------------

* `FINUFFT <https://finufft.readthedocs.io/>`__: nonuniform fast Fourier
  transforms. Where treeweave's docs borrow their structure from.
* `POET <https://github.com/flatironinstitute/poet>`__: the portable-SIMD layer
  treeweave and FINUFFT both build their kernels on.
* `polyfit <https://github.com/DiamonDinoia/polyfit>`__: the Chebyshev-node
  polynomial fitter treeweave uses on each panel.
