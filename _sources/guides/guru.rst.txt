Guru interface (``treeweave::guru``)
====================================

``<treeweave/guru.hpp>`` is treeweave's guru interface — named after FFTW's
guru interface, the established precedent for an expert API that exposes the
planner/executor internals. It re-exposes the C++ batch pipeline's stages for
caller-driven fusion: caller-owned scratch, caller-chosen keys, no per-call
allocation. Everything here is reentrant and stateless; the buffers are caller
arguments. The public ``Function::sorted`` kernel dispatch runs on the same
canonical walk this interface exposes, so library and user code share one path.

When to use it
--------------

Use it when one fit is not enough: approximating a function with singularities,
asymptotic regimes, or scale changes. The standard construction:

1. **Split the domain into regimes.** Cut at singularities, at scale changes,
   anywhere the fit tree must get deep.
2. **Fit each regime separately.** Subtract or factor out the singular part so
   each regime's leftover is polynomial-friendly; keep the analytically-known
   part as a cheap elementwise fixup to apply after each regime's fit.
3. **Evaluate through one sort.** Per point compute one combined key
   ``key = range_base + f.leaf_id(x)`` for the regime's fit. One counting sort
   over those keys packs every regime's points into contiguous runs. Evaluate
   each run with the polyfit SIMD kernel and fuse its regime's fixup while the
   data is still hot. Restore caller order with ``gather`` through the sort's
   ``rank``.

Fully sorted input skips the sort entirely: leaf ids are monotone so runs are
already contiguous (``guru::for_each_sorted_run``).

The worked example is ``tests/test_guru.cpp`` in the source tree — including
a two-fit combined-key sort with per-run fused post-processing. The pattern at
production scale lives in the hank105 evaluator of the
`treeweave-functions <https://github.com/DiamonDinoia/treeweave-functions>`_
repository.

The helpers
-----------

Classification stays the public ``Function`` machinery — ``leaf_id`` /
``leaf_ids``, ``num_leaves()``, ``out_of_domain_id()``,
``has_fast_quantize()``: the SIMD batch form runs when the fit has a live leaf
table, the per-point descent otherwise; the ids are identical either way. A
point is out-of-domain (OOD) when the fit cannot evaluate it — below
``lower``, above ``upper``, or NaN/±Inf; classification maps it to the
sentinel id ``out_of_domain_id()`` (== ``num_leaves()``).

The sort blocks are generic u32 counting-sort primitives. The u32 buffers are
concrete ``std::span``\ s (a ``std::vector`` or ``std::array`` converts
implicitly); the value buffers accept any contiguous range with matching
element types — pointer callers wrap ``std::span{p, n}``:

* ``counting_sort(keys, in, packed, counts, rank)`` — one shot
  (``histogram`` → ``exclusive_scan`` → ``scatter``). On return ``counts[b]``
  is bin ``b``'s ending offset (feed to ``for_each_run``), ``packed`` is
  bin-ordered, and ``rank[i]`` is point ``i``'s packed slot — the inverse
  permutation, consumed directly by ``gather``.
* ``histogram(keys, counts)`` + ``exclusive_scan(counts)`` +
  ``scatter(keys, in, packed, counts, rank)`` — the split form for callers
  that fuse the histogram into their own classify sweep. ``scatter`` keeps its
  literature name (the counting sort's placement pass); unlike
  ``thrust::scatter``, the destination map comes from ``keys`` + ``counts``,
  not from the caller.
* ``gather(rank, packed, out)`` — ``out[i] = packed[rank[i]]``
  (``thrust::gather``-exact), one call per output component; owns the
  gathered-read prefetch.

The run evaluators call the polyfit kernels directly:

* ``eval_leaf_aos(f, id, x, out, n[, k])`` — interleaved
  ``out[q * output_dim + d]``.
* ``eval_leaf_soa(f, id, x, out, n[, k])`` — per-component spans
  (``output_dim > 1``).
* ``fill_out_of_domain(f, out, n)`` — the OOD bucket's writeback:
  ``n * output_dim`` quiet NaNs (AoS overload; SoA twin takes the component
  pointer array). ``Function::sorted`` runs on the same definition.
* ``for_each_run(ends, fn)`` — ``fn(id, begin, count)`` once per non-empty
  bin, ascending id order.
* ``for_each_sorted_run(f, xs, n, fn)`` — sorted input, zero scratch; the
  same ``fn(id, begin, count)`` callback. OOD points arrive with
  ``id == f.out_of_domain_id()`` — the callback owns their fill
  (``fill_out_of_domain``); ``eval_leaf_*`` on the sentinel is a contract
  violation (asserted in Debug).

Rules worth pinning
-------------------

* **Key layout.** Fold each fit's OWN sentinel first, then offset per regime:
  ``key = (id == f.out_of_domain_id()) ? ood_bucket : range_base + id`` —
  never compare against an offset number. Lay the per-regime key spaces
  back-to-back without holes; put the shared OOD bucket last. Neighbourhood:
  ``key < counts.size()`` everywhere, every tile stays below ``2^32`` points.
* **Writeback.** ``gather(rank, packed_out, out)`` unconditionally, per
  output component. Every slot belongs to exactly one bin — the OOD buckets
  included. (``eval_scatter_sorted`` in ``<treeweave/eval_scatter.hpp>``
  records the opposite, forward permutation: its ``perm[k]`` is the original
  index of packed slot ``k``.)
* **Tiles.** Process the batch in tiles sized to keep per-point scratch
  (``keys`` 4 B + ``rank`` 4 B + packed in/out streams) L2-resident. On a
  2 MiB L2 host the measured valley is ``2^14`` points for a 4-output fit;
  sweep ``{16384, 32768, 65536}`` if your layout differs.
* **Contract errors.** ``key >= counts.size()`` into the sort and the OOD
  sentinel into ``eval_leaf_*`` are assert-guarded: clean failure in Debug,
  undefined behaviour in NDEBUG. Wire new callers up under a Debug or
  sanitizer build first.
* **ISA expectations.** SIMD classification needs the fit's leaf table
  (``has_fast_quantize()``); otherwise the work degenerates to per-point tree
  descent with identical ids. The numeric kernels are ISA-generic; ports below
  AVX2 run every piece on the public scalar machinery.

The default kernel-policy parameter (``k``) spells
``treeweave::default_kernel_policy`` — an alias of the library's internal
``detail::InlineKernels`` — and exists for callers that pass a custom policy;
nobody else should pass it.
