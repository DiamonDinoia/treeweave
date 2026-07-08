JavaScript / TypeScript
=======================

Install
-------

.. code-block:: bash

   npm install @flatironinstitute/treeweave

Prebuilt native N-API binaries ship for common Linux, macOS, and Windows
platforms. A bundled WASM backend is used in browsers and hosts without a
matching native prebuild.

Usage
-----

.. code-block:: js

   import { Treeweave } from "@flatironinstitute/treeweave";

   function zeta(x) {
       let y = 0.0;
       for (let k = 1; k <= 1000; ++k) y += Math.pow(k, -x[0]);
       return y;
   }

   const approx = await Treeweave.fit(zeta, 2.0, 10.0, 1e-10);
   console.log(approx.eval(3.5));
   approx.free();

Options
-------

Pass a ``FitOptions`` object as the fifth argument to ``Treeweave.fit``. Shared
fit options are documented in :doc:`options`; JavaScript uses lower camel case
names such as ``tolKind`` and ``maxMemoryMib``.

.. code-block:: ts

   const approx = await Treeweave.fit(f, 0.0, 10.0, 1e-10, {
       tolKind: "absolute_max",
       maxDepth: 30,
       maxMemoryMib: 64,
       backend: "native",
   });

JavaScript-specific fields:

- ``dim`` and ``outDim`` are inferred by default; set them only when probing the
  callback at the box midpoint is not enough.
- ``dtype`` defaults to ``"f64"``; use ``"f32"`` for single precision.
- ``backend`` defaults to ``"auto"``; force ``"native"`` or ``"wasm"`` when you
  need a specific backend.

Source build
------------

.. code-block:: bash

   cmake --preset bindings-js
   cmake --build build/bindings-js -j

Use ``bindings-js-wasm`` for the browser WASM backend.
