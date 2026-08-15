Installation
============

Precompiled binaries are available for most languages and systems; this is the
recommended route. Start with the shortest install path and a tiny example.
Each language guide has the other install routes, options, source builds, and
larger examples.

Python
------

.. code-block:: bash

   pip install treeweave

.. code-block:: python

   import math, treeweave
   # Fit sin(x) on [0, 1] syntax is fit(callback, lower_bound, upper_bound, tol)
   f = treeweave.fit(lambda x: math.sin(x[0]), 0.0, 1.0, tol=1e-10)
   # Evaluate f on (0.5) and print the result.
   print(f(0.5))

See :doc:`guides/python`.

C++
---

.. code-block:: cmake

   # Download treeweave at configure time and link the header-only C++ target.
   CPMAddPackage("gh:DiamonDinoia/treeweave@stable")
   add_executable(my_app example.cpp)
   target_link_libraries(my_app PRIVATE treeweave::treeweave)

.. code-block:: cpp

   #include <treeweave/treeweave.hpp>
   #include <cmath>
   #include <iostream>

   int main() {
       // Fit sin(x) on [0, 1] syntax is  fit(callback, lower_bound, upper_bound, tolerance)
       const auto f = treeweave::fit([](double x) { return std::sin(x); }, 0.0, 1.0, 1e-10);
       // Evaluate f on (0.5) and print the result.
       std::cout << f(0.5) << "\n";
   }

No CMake:

.. code-block:: bash

   URL="https://github.com/DiamonDinoia/treeweave/releases/latest/download/treeweave-cxx-headers.tar.gz"
   curl -fLO "$URL" || wget "$URL"
   tar xzf treeweave-cxx-headers.tar.gz

See :doc:`guides/cpp`.

C
-

.. code-block:: bash

   VER=stable
   PLATFORM=linux-x86_64
   URL="https://github.com/DiamonDinoia/treeweave/releases/download/${VER}/treeweave-${VER}-${PLATFORM}.tar.gz"
   curl -fLO "$URL" || wget "$URL"
   tar xzf "treeweave-${VER}-${PLATFORM}.tar.gz"

.. code-block:: c

   #include <math.h>
   #include <stdio.h>
   #include <treeweave.h>

   static void fn(const double *x, double *y, void *ctx) {
       (void)ctx;
       /* Callback evaluated during fitting. */
       y[0] = sin(x[0]);
   }

   int main(void) {
       double a = 0.0, b = 1.0;
       /* Fit sin(x) on [0, 1] syntax is treeweave_fit(callback, input_dim, output_dim, lower, upper, tolerance, context, options). */
       treeweave_t f = treeweave_fit(fn, 1, 1, &a, &b, 1e-10, NULL, NULL);
       /* Evaluate f on (0.5) and print the result. */
       printf("%g\n", treeweave_eval_1d(f, 0.5));
       treeweave_free(f);
   }

.. code-block:: bash

   gcc example.c -Iinclude -Llib -ltreeweave_c -lm -o example
   LD_LIBRARY_PATH=lib ./example

On Windows the zip puts ``treeweave_c.dll`` in ``bin\`` and the import library
``treeweave_c.lib`` in ``lib\``. Windows has no ``RPATH``, so the loader finds
the DLL only through ``PATH``:

.. code-block:: bat

   cl example.c /I include /link /LIBPATH:lib treeweave_c.lib
   set PATH=%CD%\bin;%PATH%
   example.exe

The same applies to a ``find_package(treeweave)`` consumer: linking succeeds
without ``PATH``, and the executable then fails to start. Either put ``bin\``
on ``PATH``, copy the DLL next to the executable, or link
``treeweave::treeweave_c_static``.

See :doc:`guides/c`.

Julia
-----

.. code-block:: julia

   using Pkg
   Pkg.add(url="https://github.com/DiamonDinoia/treeweave",
           subdir="bindings/julia/Treeweave")

.. code-block:: julia

   using Treeweave
   # Fit sin(x) on [0, 1] syntax is fit(callback, lower_bound, upper_bound, tolerance)
   f = fit(sin, 0.0, 1.0, 1e-10)
   # Evaluate f on (0.5) and print the result.
   println(f(0.5))

See :doc:`guides/julia`.

MATLAB
------

With `mip <https://mip.sh/>`_ after ``treeweave`` is published to a mip channel:

.. code-block:: matlab

   mip install treeweave
   mip load treeweave

Or download the MATLAB bundle directly:

.. code-block:: bash

   VER=stable
   PLATFORM=linux-x64
   URL="https://github.com/DiamonDinoia/treeweave/releases/download/${VER}/treeweave-matlab-${VER}-${PLATFORM}.tar.gz"
   curl -fLO "$URL" || wget "$URL"
   tar xzf "treeweave-matlab-${VER}-${PLATFORM}.tar.gz"

.. code-block:: matlab

   % Add the extracted MEX bundle.
   addpath('treeweave-matlab-stable-linux-x64')
   % Fit sin(x) on [0, 1] syntax is treeweave(callback, lower_bound, upper_bound, tolerance, name/value options).
   f = treeweave(@(x) sin(x(1)), [0], [1], 1e-10, 'dim', 1, 'out_dim', 1);
   % Evaluate f on (0.5) and print the result.
   disp(f(0.5));
   delete(f);

See :doc:`guides/matlab`.

Octave
------

.. code-block:: bash

   VER=stable
   URL="https://github.com/DiamonDinoia/treeweave/archive/refs/tags/${VER}.tar.gz"
   curl -fL "$URL" -o "treeweave-${VER}-source.tar.gz" || wget -O "treeweave-${VER}-source.tar.gz" "$URL"
   tar xzf "treeweave-${VER}-source.tar.gz"
   cd "treeweave-${VER}"
   cmake --preset bindings-octave
   cmake --build build/bindings-octave -j

.. code-block:: matlab

   % Fit sin(x) on [0, 1] syntax is treeweave(callback, lower_bound, upper_bound, tolerance, name/value options).
   f = treeweave(@(x) sin(x(1)), [0], [1], 1e-10, 'dim', 1, 'out_dim', 1);
   % Evaluate f on (0.5) and print the result.
   disp(f(0.5));
   delete(f);

See :doc:`guides/matlab`.

Fortran
-------

.. code-block:: bash

   cmake --preset bindings-fortran
   cmake --build build/bindings-fortran -j

.. code-block:: fortran

   subroutine fn(x, y, context) bind(C)
       use, intrinsic :: iso_c_binding
       real(c_double), intent(in)  :: x(*)
       real(c_double), intent(out) :: y(*)
       type(c_ptr), value          :: context
       ! Callback evaluated during fitting.
       y(1) = sin(x(1))
   end subroutine fn

   program example
   use, intrinsic :: iso_c_binding
   use treeweave
   interface
       subroutine fn(x, y, context) bind(C)
           use, intrinsic :: iso_c_binding
           real(c_double), intent(in)  :: x(*)
           real(c_double), intent(out) :: y(*)
           type(c_ptr), value          :: context
       end subroutine fn
   end interface
   real(c_double) :: a(1) = [0.0_c_double], b(1) = [1.0_c_double]
   real(c_double) :: x(1) = [0.5_c_double], y(1)
   type(c_ptr) :: h
   ! Fit sin(x) on [0, 1] syntax is treeweave_fit(callback, input_dim, output_dim, lower, upper, tolerance, context, options).
   h = treeweave_fit(c_funloc(fn), 1_c_int, 1_c_int, a, b, 1.0e-10_c_double, c_null_ptr, c_null_ptr)
   ! Evaluate f on (0.5) and print the result.
   call treeweave_eval(h, x, y)
   print *, y(1)
   h = treeweave_free(h)
   end program example

See :doc:`guides/fortran`.

JavaScript / TypeScript
-----------------------

.. code-block:: bash

   npm install @flatironinstitute/treeweave

.. code-block:: js

   import { Treeweave } from "@flatironinstitute/treeweave";
   // Fit sin(x) on [0, 1] syntax is fit(callback, lower_bound, upper_bound, tolerance)
   const f = await Treeweave.fit((x) => Math.sin(x[0]), 0.0, 1.0, 1e-10);
   // Evaluate f on (0.5) and print the result.
   console.log(f.eval(0.5));
   f.free();

See :doc:`guides/js`.

CMake
-----

For profiles, build options, targets, installed packages, and source builds,
see :doc:`guides/cmake`.
