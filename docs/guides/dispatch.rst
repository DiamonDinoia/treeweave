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

The rung table in ``cmake/treeweave_c_dispatch.cmake`` defines the ladder. Each
row gives the build level, the ``xsimd`` arch its flags select, the
``TREEWEAVE_FORCE_ARCH`` name that pins it, and the compile flags:

.. literalinclude:: ../../cmake/treeweave_c_dispatch.cmake
   :language: cmake
   :start-after: # BEGIN RUNG_TABLE
   :end-before: # END RUNG_TABLE
   :dedent: 4

Everything downstream is generated from that table: the per-rung kernel object,
the ``xsimd::arch_list`` the dispatcher walks, and one
``test_c_abi_force_<level>`` ctest per rung. The dispatcher walks the ladder
widest-first and takes the first rung the host reports as available. A rung
whose flags do not select the arch its row names is a compile error, not a
silent duplicate.

x86 has four rungs. MSVC ABI compilers substitute AVX for SSE4.2, because there
is no ``/arch:SSE4.2``. ``aarch64`` (non-Apple) has one, ``neon64``: NEON64 is
mandatory on ARMv8-A, so it always dispatches, and it keeps one dispatch path
across platforms rather than adding a performance tier. ``riscv64`` has one,
fixed 128-bit RVV, best-effort and untested since no RISC-V CI runner exists.

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

Set ``TREEWEAVE_FORCE_ARCH`` to one of the names in the table above to pin the dispatcher to
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
