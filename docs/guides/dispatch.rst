Runtime ISA dispatch
====================

``libtreeweave_c`` can ship a single binary that picks the widest SIMD
instruction set the host CPU supports at load time, instead of staying pinned to
its compile-time ``-march``. ``TREEWEAVE_C_MULTIARCH`` turns that on. The build
then compiles the eval kernels once per ISA level, and the dispatcher picks one
level the first time a caller builds a fit.

Enable it at configure time:

.. literalinclude:: ../../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_MULTIARCH
   :end-before: # END DOCS_MULTIARCH
   :dedent: 4

On aarch64, pass ``-DTREEWEAVE_ARCH=armv8-a`` instead; the ``multiarch-arm``
preset spells the same pair. ``_build-c-abi.yml`` builds that leg on an arm
runner.

``TREEWEAVE_ARCH`` is the baseline for the dispatcher and the C shim
themselves, so the library loads on any CPU of the family. Keep it at the
lowest level (``x86-64``, ``armv8-a``), not a tuned one.

How the build chooses the family
--------------------------------

The build selects the variant set at compile time from ``xsimd::best_arch``, by
CPU family (see ``include/treeweave/detail/dispatch_arch.hpp``):

- ``x86-64``: a four-level ladder. The dispatcher walks it widest-first and
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

- ``aarch64`` (non-Apple): a single ``neon64`` variant
  (``TREEWEAVE_FORCE_ARCH`` name ``arm64+neon``). NEON64 is mandatory on
  ARMv8-A, so it always dispatches. The single variant keeps one dispatch path
  across platforms. It is not a new performance tier.

- ``riscv64``: a single ``rvv`` variant (fixed 128-bit RVV). Best-effort and
  untested. No RISC-V CI runner exists, so this branch compiles but nothing
  checks it.

Apple silicon and unknown targets fall back to a single-arch build at
``TREEWEAVE_ARCH`` (no runtime dispatch).

Why no SVE on ARM
-----------------

ARM SVE is excluded. xsimd's ``sve<N>`` bakes the width in at compile time,
while the runtime probe only checks SVE *presence*, never width, so a
fixed-width variant would falsely match hardware of a different width. NEON64
is mandatory on ARMv8-A, so dropping SVE costs no coverage.

Forcing an ISA for testing
---------------------------

Set ``TREEWEAVE_FORCE_ARCH`` to one of the names above to pin the dispatcher to
a specific level, capped at what the host supports, so one capable machine can
exercise every fallback path:

.. literalinclude:: ../../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_FORCE_ARCH
   :end-before: # END DOCS_FORCE_ARCH
   :dedent: 4

A name the build compiled no rung for is a bug, not a fallback: ``avx`` exists
only in an MSVC-ABI build, and asking a GCC or Clang build for it aborts the
C-ABI test. On aarch64 the one variant is ``arm64+neon``. A rung the host cannot
run is a skip, so the ``avx512bw`` line above passes on a pre-AVX-512 CPU too.

An unset or unsupported value falls through to the normal widest-supported
selection.
