treeweave
=========

**Evaluate an expensive function millions of times, fast.**

treeweave takes a function that is slow to compute — a special function, a
kernel, the output of a costly model — and builds a drop-in replacement that
returns the same answer to your chosen accuracy, far more cheaply. You pay the
cost once, at fit time; every evaluation afterward is a quick polynomial lookup.

Internally it fits your function piecewise with low-order polynomials (monomial basis, via polyfit)
on an adaptive tree, refining only where the function is hard to approximate. It
is conceptually like ``chebfun``, but panelled — so it reaches the same
tolerance at lower polynomial order, which is faster on modern SIMD hardware.

.. code-block:: cpp

   #include <treeweave/treeweave.hpp>
   #include <cmath>

   int main() {
       // zeta_N(s) = sum_{k=1..N} k^-s — expensive; fit once, eval a polynomial.
       auto zeta = [](double s) {
           double a = 0.0;
           for (int k = 1; k <= 1000; ++k)
               a += std::pow(k, -s);
           return a;
       };
       auto   fast = treeweave::fit(zeta, 2.0, 10.0, /*tol=*/1e-10);  // fit once
       double y    = fast(3.5);                                       // evaluate forever
       return y > 0 ? 0 : 1;
   }

Use it from **C++** (header-only), or from **C**, **Fortran**, **Python**,
**Julia**, **MATLAB/Octave**, and **JavaScript/TypeScript** through a stable C ABI.

Project links
-------------

- `GitHub repository <https://github.com/DiamonDinoia/treeweave>`_
- `Issue tracker <https://github.com/DiamonDinoia/treeweave/issues>`_
- `Releases <https://github.com/DiamonDinoia/treeweave/releases>`_
- `License (BSD-3-Clause) <https://github.com/DiamonDinoia/treeweave/blob/main/LICENSE>`_
- `Benchmarks dashboard <https://diamondinoia.github.io/treeweave/dev/bench/>`_
- `Background talk: A. Barnett, "What everyone should know about function approximation" (FWAM7, 2025) <https://users.flatironinstitute.org/~ahb/talks/fwam25.pdf>`_

Why treeweave
-------------

- **Fit once, evaluate forever.** Amortize an expensive function into a cheap
  polynomial lookup; trade a little memory for a lot of speed.
- **Adaptive paneling.** Low-order polynomial panels on an adaptive tree reach
  the target tolerance at lower order than a single global fit — SIMD-friendly.
- **Seven languages, one core.** A stable C ABI (``libtreeweave_c``) underpins
  the Python, Julia, MATLAB/Octave, Fortran, and JavaScript/TypeScript wrappers;
  C++ is header-only.
- **Thread-safe by construction.** A fitted object is immutable and its
  ``operator()`` is safe to call concurrently.

.. toctree::
   :maxdepth: 2
   :caption: Getting Started

   install
   guides/quickstart

.. toctree::
   :maxdepth: 2
   :caption: Language guides

   guides/python
   guides/julia
   guides/matlab
   guides/cpp
   guides/c
   guides/fortran

.. toctree::
   :maxdepth: 2
   :caption: Reference

   guides/options
   guides/c-abi
   guides/dispatch
   guides/performance
   how-treeweave-works
   known-issues

.. toctree::
   :maxdepth: 2
   :caption: API Reference

   api/library_root
