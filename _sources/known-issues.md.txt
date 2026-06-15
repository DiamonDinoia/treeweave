# Known issues

A short list of current limitations that are understood and tracked, but not yet
fixed. None affect correctness; they are performance or ergonomics caveats.

## MATLAB/Octave single-point eval overhead

MATLAB/Octave **single-point** eval is bottlenecked by mwrap's generic R2008OO
codegen, not by treeweave. Per call the handle is stored as a string in the
`mwptr` property and re-parsed via `sscanf` (the `treeweave.mw` R2008OO
convention), plus a temporary output buffer is allocated and copied. This is
inherent to mwrap and is amortised to ~zero by the **batch** API — use the
vectorized interface for hot loops.

The practical guidance is the same as everywhere else in treeweave: prefer the
batch (`obj.eval(X)`) and sorted-batch paths over a scalar loop whenever you have
more than a handful of points.
