Runtime ISA dispatch
====================

``libtreeweave_c`` can ship a single binary that picks the widest SIMD
instruction set the host CPU supports at load time, rather than being pinned to
the ``-march`` it was compiled with. This is the ``TREEWEAVE_C_MULTIARCH``
build mode: the eval kernels are compiled once per ISA level, and a tiny
dispatcher selects one the first time a fit is built.

Enable it at configure time:

.. code-block:: bash

   cmake -B build -DTREEWEAVE_C_MULTIARCH=ON -DTREEWEAVE_ARCH=x86-64   # x86 / MSVC
   cmake -B build -DTREEWEAVE_C_MULTIARCH=ON -DTREEWEAVE_ARCH=armv8-a  # aarch64

``TREEWEAVE_ARCH`` is the **baseline** the dispatcher itself and the C shim are
compiled at, so the library loads on any CPU of the family — keep it at the
lowest level (``x86-64``, ``armv8-a``), not a tuned one.

How the family is chosen
------------------------

The set of variants is selected at compile time from ``xsimd::best_arch`` by CPU
family (see ``include/treeweave/detail/dispatch_arch.hpp``):

- **x86-64** — a four-level ladder. The dispatcher walks it widest-first and
  selects the first level the host reports as available. GCC/Clang use
  ``SSE2 -> SSE4.2 -> AVX2 -> AVX-512``; MSVC ABI compilers use
  ``SSE2 -> AVX -> AVX2 -> AVX-512`` because there is no ``/arch:SSE4.2``.

  ====================  ============  ===========================
  ISA level             arch          ``TREEWEAVE_FORCE_ARCH`` name
  ====================  ============  ===========================
  ``x86-64-v4``         AVX-512BW     ``avx512bw``
  ``x86-64-v3``         FMA3 + AVX2   ``fma3+avx2``
  ``x86-64-v2``         SSE4.2        ``sse4.2`` (GCC/Clang)
  ``/arch:AVX``         AVX           ``avx`` (MSVC ABI)
  ``x86-64``            SSE2          ``sse2``
  ====================  ============  ===========================

- **aarch64** (non-Apple) — a single ``neon64`` variant
  (``TREEWEAVE_FORCE_ARCH`` name ``arm64+neon``). NEON64 is mandatory on
  ARMv8-A, so it always dispatches. The win here is a uniform, portable dispatch
  path — not a new performance tier.

- **riscv64** — a single ``rvv`` variant (fixed 128-bit RVV). **Best-effort and
  untested**: there is no RISC-V CI runner, so this branch compiles but is not
  verified.

Apple silicon and unknown targets fall back to a single-arch build at
``TREEWEAVE_ARCH`` (no runtime dispatch).

Why no SVE on ARM
-----------------

ARM SVE is excluded: xsimd's ``sve<N>`` bakes the width in at compile time, but the runtime probe only checks SVE *presence* — not width — so a fixed-width variant would falsely match hardware of a different width. NEON64 is mandatory on ARMv8-A, so dropping SVE costs no coverage.

Forcing an ISA for testing
---------------------------

Set ``TREEWEAVE_FORCE_ARCH`` to one of the names above to pin the dispatcher to
a specific level (capped at what the host actually supports), so one capable
machine can exercise every fallback path:

.. code-block:: bash

   TREEWEAVE_FORCE_ARCH=sse2       ./test_c_abi   # force the x86 baseline
   TREEWEAVE_FORCE_ARCH=avx        ./test_c_abi   # force MSVC's middle rung
   TREEWEAVE_FORCE_ARCH=fma3+avx2  ./test_c_abi   # force AVX2
   TREEWEAVE_FORCE_ARCH=arm64+neon ./test_c_abi   # force NEON64 on aarch64

An unset, unknown, or unsupported value falls through to the normal
widest-supported selection.
