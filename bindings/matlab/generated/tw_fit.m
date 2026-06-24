function [h] = tw_fit(fh, a, b, tol, input_dim, output_dim, tol_kind, max_depth, max_memory_mib, allow_max_depth_leaves, min_uniform_depth)
mex_id_ = 'c o treeweave_function* = tw_fit_w(c i mxArray, c i double[], c i double[], c i double, c i int, c i int, c i int, c i int, c i int, c i int, c i int)';
[h] = treeweave_mex(mex_id_, fh, a, b, tol, input_dim, output_dim, tol_kind, max_depth, max_memory_mib, allow_max_depth_leaves, min_uniform_depth);

% -----------------------------------------------------------------------
% Single-point eval. `self` is a treeweave (its mwptr holds the handle).
% NOTE: the generated gateway reads every scalar via mxWrapGetScalar, which
% requires mxDOUBLE_CLASS — so the .m stubs must pass plain doubles (no
% int32/int64 casts) and the C side casts them.
