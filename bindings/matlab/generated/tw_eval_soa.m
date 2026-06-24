function [Yflat] = tw_eval_soa(self, Xflat, output_dim, n)
mex_id_ = 'tw_eval_soa_w(c i treeweave_function*, c i double[], c o double[xx], c i int, c i int64_t)';
[Yflat] = treeweave_mex(mex_id_, self, Xflat, output_dim, n, n, output_dim);

% -----------------------------------------------------------------------
% Introspection + free.
