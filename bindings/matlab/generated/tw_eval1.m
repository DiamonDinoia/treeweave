function [y] = tw_eval1(self, x, output_dim)
mex_id_ = 'tw_eval1_w(c i treeweave_function*, c i double[], c o double[x], c i int)';
[y] = treeweave_mex(mex_id_, self, x, output_dim, output_dim);

% -----------------------------------------------------------------------
% Batch AoS eval. Xflat = (input_dim*n) doubles, point-major.
