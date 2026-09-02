Fit options
===========

Every option here has a default that works. Set one only to override it.

This page documents each option once. The language guides show how to pass
them, in each language's own naming style. C++, Python, Julia and MATLAB spell
it ``tol_kind``, JavaScript spells it ``tolKind``, and C and Fortran use the C
ABI fields.

Common fit options
------------------

These apply to every language binding:

.. literalinclude:: ../../include/treeweave/treeweave.hpp
   :language: cpp
   :start-after: // BEGIN DOCS_OPTIONS_STRUCT
   :end-before: // END DOCS_OPTIONS_STRUCT

Every binding exposes the same five fields with the same defaults. The C ABI
mirrors the struct in ``treeweave_opts``, and ``treeweave_default_opts()``
fills it with these values.

The leaf polynomial degree is not a runtime option. In C++ it is a template
parameter, ``treeweave::fit<N>``, default 7. The C ABI uses degree 7 for every
cell, independent of the CPU.

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
     - A probe of the callback infers the input/output dimensions; set them
       explicitly when the probe is ambiguous or expensive.
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
       the C ABI's fixed degree 7.

.. _tolkind:

``TolKind``
-----------

.. literalinclude:: ../../include/treeweave/detail/tol_kind.hpp
   :language: cpp
   :start-after: // BEGIN DOCS_TOL_KIND
   :end-before: // END DOCS_TOL_KIND

The C ABI enum carries the same numeric values:

.. literalinclude:: ../../include/treeweave.h
   :language: c
   :start-after: /* BEGIN DOCS_TOL_KIND_C */
   :end-before: /* END DOCS_TOL_KIND_C */

``RelativeMax`` is the default. Switch to an ``Absolute*`` kind when ``f`` can
be zero or when relative accuracy is not meaningful. The ``*Tail`` kinds are
1-D only.
