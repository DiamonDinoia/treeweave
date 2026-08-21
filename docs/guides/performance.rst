Performance
===========

Batch over scalar
-----------------

The batch entry points (``treeweave_batch`` / ``treeweave_sorted`` /
``treeweave_transposed`` in C; calling the fitted object with an array in the
bindings) amortize leaf lookup and vectorize the polynomial evaluation across
points. Prefer them to a scalar loop for anything beyond a handful of points.

This matters most in MATLAB/Octave, where single-point eval carries an extra
per-call overhead from the mwrap binding layer, not from treeweave; see
:doc:`/known-issues`. The batch path amortises that overhead to ~zero.

The sorted fast path
~~~~~~~~~~~~~~~~~~~~~~

The general batch path first bin-sorts the inputs by leaf so each leaf's
points are contiguous, runs one vectorized Horner stream per leaf, then permutes
the results back to the caller's order. When the inputs are *already* ascending,
that sort and permute-back are pure overhead. The ``sorted`` path skips both and
streams straight through the leaves, ~3-4x faster on a presorted 1-D batch.

Common cases where inputs are ascending for free: regular grids, ``linspace``,
quadrature nodes, time series, and parameter sweeps.

The caller promises ``x[i] <= x[i+1]``; treeweave does not verify the promise, so
unsorted input yields wrong values. Use the plain batch path when the order is
unknown. It sorts internally and is always correct. Both paths NaN-fill
out-of-domain points.

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

Cross-language throughput and latency
--------------------------------------

Riemann-zeta sum benchmarked from all 7 bindings against native recomputation.
Bars: Mevals/s (log scale); labels: within-language speedup.
Cross-language Mevals/s is not comparable, because the CI runners differ.
Compare each language's treeweave bar to its own native bar.
The ``benchmark-showcase`` CI matrix regenerates these charts.

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
