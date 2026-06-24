function [Yflat] = tw_eval_multi(self, Xflat, output_dim, n)
mex_id_ = 'tw_eval_multi_w(c i treeweave_function*, c i double[], c o double[xx], c i int, c i int64_t)';
[Yflat] = treeweave_mex(mex_id_, self, Xflat, output_dim, n, output_dim, n);

% -----------------------------------------------------------------------
% Sorted 1-D batch eval (dim == 1; caller guarantees ascending x).
