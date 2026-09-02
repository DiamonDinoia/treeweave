% example_routes.m: the four evaluation routes of a fitted object.
%
% Before running: add the build tree (tw_*.m stubs + treeweave_mex.mex/mexa64)
% to the MATLAB/Octave path. This script adds only the source dir (treeweave.m).
addpath(fileparts(fileparts(mfilename('fullpath'))));   % add bindings/matlab/ (treeweave.m)

f   = @(x) [sin(x(1)); cos(x(1))];
obj = treeweave(f, 0, 5, 1e-9);
X   = linspace(0, 5, 1000)';   % ascending, so the sorted route applies

% BEGIN DOCS_ROUTES
Y  = obj(X);                       % batch:  N x dim -> N x out_dim
Y2 = obj.eval(X);                  % identical to obj(X)
Ys = obj(X, 'sorted', true);       % X promised non-decreasing (dim == 1)
Yt = obj(X, 'transposed', true);   % batch -> out_dim x N (out_dim > 1)
% END DOCS_ROUTES

assert(isequal(Y, Y2));
assert(isequal(Y, Ys));
assert(isequal(Y, Yt'));
Yref = [sin(X), cos(X)];
fprintf('routes max abs error: %.3e\n', max(max(abs(Y - Yref))));
assert(max(max(abs(Y - Yref))) < 1e-8);
delete(obj);
