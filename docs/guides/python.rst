Python
======

The Python binding wraps the C ABI with nanobind. ``treeweave.fit`` takes a
callable and returns an object that evaluates NumPy arrays in one call.
Inputs are 1-D, 2-D or 3-D.

Install
-------

Install the latest release from PyPI:

.. literalinclude:: ../../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_PIP_PYPI
   :end-before: # END DOCS_PIP_PYPI
   :dedent: 4

The wheel bundles the C ABI statically and selects a SIMD variant at runtime
(:doc:`dispatch`). NumPy is the only dependency.

To test an unreleased change, every push to ``main`` publishes a staging wheel
(``X.Y.Z.devN``) to `TestPyPI <https://test.pypi.org/project/treeweave/>`_;
install it with TestPyPI as the primary index and real PyPI for the dependencies:

.. literalinclude:: ../../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_PIP_TESTPYPI
   :end-before: # END DOCS_PIP_TESTPYPI
   :dedent: 4

Minimal example
---------------

.. literalinclude:: ../../bindings/python/examples/simple_1d.py
   :language: python

``fit`` infers the input and output dimensions by probing the callable, so the
common case is ``fit(f, a, b, tol=...)``. Call the fitted object directly, with
a single point or with a batch. A failing C++ fit raises a Python exception.

Decorator form
--------------

Omit the callable and ``fit`` returns a decorator, the ``functools.cache``
spelling. The decorated name becomes the fitted approximation:

.. literalinclude:: ../../bindings/python/examples/decorator_1d.py
   :language: python

``fit(a, b, tol)`` and ``fit(a, b, tol=...)`` are both accepted, and every
keyword option below applies unchanged:

.. literalinclude:: ../../bindings/python/examples/decorator_1d.py
   :language: python
   :start-after: # BEGIN DOCS_DECORATOR_2D
   :end-before: # END DOCS_DECORATOR_2D

The decorated object is an ordinary ``TreeweaveFunction``, so ``sorted=`` /
``transposed=`` and the properties all work on it. ``__name__`` and ``__doc__``
come from the original function, which stays reachable as ``__wrapped__``.
Fitting happens at decoration time, so an import-time failure raises there.

Evaluation routes
-----------------

Calling the fitted object dispatches on the shape of its argument, and two
keyword flags select the fast paths:

.. literalinclude:: ../../bindings/python/examples/eval_routes.py
   :language: python
   :start-after: # BEGIN DOCS_ROUTES
   :end-before: # END DOCS_ROUTES

.. include:: ../_shared/sorted.src

``transposed=True`` returns each output component in its own contiguous row.

.. include:: ../_shared/domain.src

Multi-dimensional fits
----------------------

Pass sequence corners; the callback receives a length-``dim`` row and returns a
scalar or a length-``out_dim`` sequence:

.. literalinclude:: ../../bindings/python/examples/simple_2d.py
   :language: python
   :start-after: # BEGIN DOCS_MULTIDIM
   :end-before: # END DOCS_MULTIDIM

Options
-------

Every field of the shared fit options is a keyword argument, spelled in snake
case: ``tol_kind``, ``max_depth``, ``max_memory_mib``,
``allow_max_depth_leaves`` and ``min_uniform_depth``. ``tol_kind`` takes a
string, one of ``'relative_max'``, ``'absolute_max'``, ``'relative_l2'``,
``'absolute_l2'``, ``'relative_tail'`` or ``'absolute_tail'``.

Three Python-only kwargs cover the shape: ``dim`` and ``out_dim`` override the
inference from ``a`` and from a probe of ``f``, and ``dtype`` selects ``'f64'``
or ``'f32'``. A fit that exceeds ``max_depth`` raises ``RuntimeError`` naming
``MaxDepthExceeded``.

Every one of them defaults to ``None``, meaning the library's own default;
``treeweave._treeweave.default_opts()`` returns those values, read from the C
ABI, so no default is written down twice.

See :doc:`options` for a full description of each option and the tolerance kinds.

Further
-------

.. literalinclude:: ../../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_PYTHON_DEV
   :end-before: # END DOCS_PYTHON_DEV
   :dedent: 4

Examples:
`bindings/python/examples/ <https://github.com/DiamonDinoia/treeweave/tree/main/bindings/python/examples>`_.
