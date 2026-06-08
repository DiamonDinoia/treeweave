Fit options
===========

Everything here is optional — the defaults are tuned to be good out of the box.
The C++ knobs live in ``treeweave::options`` (passed as the trailing ``fit``
argument); the C ABI mirrors the subset that matters across the boundary in
``treeweave_opts``.

``treeweave::options``
----------------------

.. list-table::
   :header-rows: 1
   :widths: 24 12 64

   * - Field
     - Default
     - Meaning
   * - ``tol_kind``
     - ``RelativeMax``
     - How ``tol`` is interpreted (see :ref:`tolkind`).
   * - ``max_depth``
     - ``50``
     - Tree-depth ceiling. Hitting it without converging throws
       ``MaxDepthExceeded`` (unless ``allow_max_depth_leaves``). Far above what
       any non-singular function needs; lower it to fail fast near a suspected
       singularity.
   * - ``max_memory_mib``
     - auto
     - Cap on accumulated leaf storage (MiB). Tri-state: ``< 0`` (default) =
       auto, a dimension-scaled budget (4 / 8 / 16 MiB for 1D / 2D / 3D);
       ``0`` = no cap; ``> 0`` = explicit cap. Crossing it throws
       ``MemoryBudgetExceeded`` carrying used/budget bytes and the offending
       panel.
   * - ``allow_max_depth_leaves``
     - ``false``
     - Keep panels that fail tolerance at ``max_depth`` as best-effort leaves
       (inspect via ``Function::non_converged_panels()``) instead of throwing.
   * - ``min_uniform_depth``
     - ``0``
     - Force BFS to refine every panel to at least this depth, driving the
       leaf-table fast path (see :doc:`performance`). ``0`` = tol-based
       refinement only.

The leaf polynomial **degree** is not a runtime option: in C++ it is a template
parameter (``treeweave::fit<N>``, default 7); the C ABI auto-selects a
register-optimal degree for the detected CPU.

.. _tolkind:

``TolKind``
-----------

.. list-table::
   :header-rows: 1
   :widths: 28 72

   * - Kind
     - Meaning
   * - ``RelativeMax`` *(default)*
     - sample-grid max-abs error relative to ``max|f|``
   * - ``AbsoluteMax``
     - sample-grid max-abs absolute error
   * - ``RelativeL2``
     - sample-grid L2 relative error
   * - ``AbsoluteL2``
     - sample-grid L2 absolute error
   * - ``RelativeTail``
     - 1-D only — relative coefficient-tail estimate
   * - ``AbsoluteTail``
     - 1-D only — absolute coefficient-tail estimate

Switch to an ``Absolute*`` kind when ``f`` can be zero or when relative accuracy
is not meaningful. In the C ABI these are the ``treeweave_tol_kind_t`` enum
values (``TREEWEAVE_RELATIVE_MAX`` …).
