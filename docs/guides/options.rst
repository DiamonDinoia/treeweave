Fit options
===========

Everything here is optional — the defaults are tuned to be good out of the box.

Options are documented here once. Language guides show how to pass them, and
use each language's naming style: C++ uses ``tol_kind``, Python/Julia/MATLAB use
``tol_kind``, JavaScript uses ``tolKind``, and C/Fortran use the C ABI fields.

Common fit options
------------------

These apply to every language binding:

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

Language-specific options
-------------------------

Some bindings add convenience fields around the shared C ABI:

.. list-table::
   :header-rows: 1
   :widths: 24 24 52

   * - Language
     - Field
     - Meaning
   * - Python, JavaScript
     - ``dim`` / ``out_dim`` or ``outDim``
     - Input/output dimensions can be inferred by probing the callback; set
       them explicitly when inference is ambiguous or expensive.
   * - Python, JavaScript
     - ``dtype``
     - Select ``f64``/``float64`` or ``f32``/``float32``.
   * - JavaScript
     - ``backend``
     - ``auto`` chooses native under Node and WASM in browsers; ``native`` and
       ``wasm`` force one backend.
   * - C++
     - degree template parameter
     - ``treeweave::fit<N>`` sets the leaf polynomial degree. Other bindings use
       the C ABI's CPU-selected degree.

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
