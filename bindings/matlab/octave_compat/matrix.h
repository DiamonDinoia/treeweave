/* Octave compatibility shim — intentionally empty.
 *
 * mwrap's generated gateway and treeweave.mw #include <matrix.h>, a MATLAB-only
 * header. Octave's <mex.h> already declares the full mxArray / mx* / mex* API,
 * so for Octave builds this directory is placed on the include path to satisfy
 * that include with a no-op. MATLAB builds never see it (their real matrix.h is
 * found on MATLAB's own include path).
 */
#ifndef TREEWEAVE_OCTAVE_MATRIX_H_SHIM
#define TREEWEAVE_OCTAVE_MATRIX_H_SHIM
#endif
