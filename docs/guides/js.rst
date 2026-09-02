JavaScript / TypeScript
=======================

The JavaScript binding is a Node addon over ``libtreeweave_c``, published as
``@flatironinstitute/treeweave`` with TypeScript types. Prebuilt binaries cover
the common platforms, so the install needs no toolchain. Inputs are 1-D, 2-D or
3-D.

Install
-------

.. code-block:: bash

   npm install @flatironinstitute/treeweave

Prebuilt native N-API binaries ship for common Linux, macOS and Windows
platforms. Browsers and hosts without a matching native prebuild fall back to
the bundled WASM backend.

Usage
-----

.. literalinclude:: ../../bindings/js/examples/simple_1d.mjs
   :language: js
   :start-after: // BEGIN DOCS_USAGE
   :end-before: // END DOCS_USAGE

Options
-------

Pass a ``FitOptions`` object as the fifth argument to ``Treeweave.fit``.
:doc:`options` documents the shared fit options. JavaScript spells them in lower
camel case, such as ``tolKind`` and ``maxMemoryMib``.

.. literalinclude:: ../../bindings/js/examples/simple_1d.mjs
   :language: js
   :start-after: // BEGIN DOCS_OPTIONS
   :end-before: // END DOCS_OPTIONS

JavaScript-specific fields:

- A probe of the callback at the box midpoint infers ``dim`` and ``outDim``; set
  them explicitly when that probe is not enough.
- ``dtype`` defaults to ``"f64"``; use ``"f32"`` for single precision.
- ``backend`` defaults to ``"auto"``; pass ``"native"`` or ``"wasm"`` to force one
  backend.

Source build
------------

.. code-block:: bash

   cmake --preset bindings-js
   cmake --build build/bindings-js -j

Use ``bindings-js-wasm`` for the browser WASM backend.
