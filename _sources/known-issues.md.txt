# Known issues

Current limitations, understood and tracked, not yet fixed. None affects
correctness; each one is a performance or ergonomics caveat.

## MATLAB/Octave single-point eval overhead

mwrap's generic R2008OO codegen, not treeweave, sets the cost of a
single-point MATLAB/Octave eval. The `treeweave.mw` R2008OO convention stores
the handle as a string in the `mwptr` property, so every call re-parses it with
`sscanf`, and every call allocates and copies a temporary output buffer. The
batch API pays that cost once for the whole array instead of once per point.

For anything beyond a handful of points, call `obj.eval(X)` or the sorted-batch
path rather than looping over scalars.
