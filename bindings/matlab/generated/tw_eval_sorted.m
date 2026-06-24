function [Yflat] = tw_eval_sorted(self, x, output_dim, n)
mex_id_ = 'tw_eval_sorted_w(c i treeweave_function*, c i double[], c o double[xx], c i int, c i int64_t)';
[Yflat] = treeweave_mex(mex_id_, self, x, output_dim, n, output_dim, n);

% -----------------------------------------------------------------------
% Batch SoA eval. Xflat = dim contiguous coordinate planes (n each).
