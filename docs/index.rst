treeweave
=========

**Evaluate an expensive function millions of times, fast.** Fit once; every evaluation afterward is a cheap polynomial lookup.

.. code-block:: cpp

   #include <treeweave/treeweave.hpp>
   auto fn = treeweave::fit([](double s) { double a=0; for(int k=1;k<=1000;++k) a+=std::pow(k,-s); return a; }, 2.0, 10.0, 1e-10);
   double y = fn(3.5);   // evaluate a polynomial, not the original function

Use it from **C++** (header-only) or from **C**, **Fortran**, **Python**, **Julia**, **MATLAB/Octave**, and **JavaScript/TypeScript** through a stable C ABI.

`GitHub <https://github.com/DiamonDinoia/treeweave>`_ · `Releases <https://github.com/DiamonDinoia/treeweave/releases>`_ · `Issues <https://github.com/DiamonDinoia/treeweave/issues>`_ · `License (BSD-3-Clause) <https://github.com/DiamonDinoia/treeweave/blob/main/LICENSE>`_ · `Benchmarks dashboard <https://diamondinoia.github.io/treeweave/dev/bench/>`_

.. toctree::
   :maxdepth: 2
   :caption: Getting Started

   install

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
   guides/performance
   guides/c-abi
   guides/dispatch
   how-treeweave-works
   known-issues

.. toctree::
   :maxdepth: 2
   :caption: API Reference

   api/library_root
