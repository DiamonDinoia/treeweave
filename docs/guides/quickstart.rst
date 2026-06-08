Quick start
===========

The everyday API is one call: ``fit(f, lo, hi, tol)`` builds the approximant,
then you call the result like the original function. The fitted object is
immutable and safe to call from many threads at once.

Pick your language:

- :doc:`C++ <cpp>` — header-only, the reference API
- :doc:`C <c>` — the stable C ABI (``libtreeweave_c``)
- :doc:`Fortran <fortran>` — thin ``iso_c_binding`` module
- :doc:`Python <python>` — NumPy-friendly, ``pip install treeweave``
- :doc:`Julia <julia>` — auto-downloads the prebuilt C library

A 60-second taste, in C++:

.. code-block:: cpp

   #include <treeweave/treeweave.hpp>

   auto runge = [](double x) { return 1.0 / (1.0 + 25.0 * x * x); };
   auto fn    = treeweave::fit(runge, -1.0, 1.0, /*tol=*/1e-10);
   double y   = fn(0.3);

Common rules across every binding:

- The fit domain is the **semi-open** interval ``[lo, hi)`` — the target only
  needs to be defined there. As a convenience, evaluating exactly at the upper
  corner ``hi`` returns the boundary value (the last cell's polynomial), not
  ``NaN``.
- ``tol`` is positional with no default, so every call states its accuracy.
- There is no input bounds checking. The domain is guarded asymmetrically:
  points below ``lo`` (and ``NaN`` inputs) return ``NaN``; points above ``hi``
  extrapolate from the last cell (the result may be non-finite). This keeps the
  hot path branchless.
- See :doc:`options` for the fit knobs (error metric, depth, memory budget).
