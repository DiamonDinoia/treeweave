% example_vector.m: 2-D input, 3-D output vector fit
%
% Before running: add the build tree (tw_*.m stubs + treeweave_mex.mex/mexa64)
% to the MATLAB/Octave path. This script adds only the source dir (treeweave.m).
addpath(fileparts(fileparts(mfilename('fullpath'))));   % add bindings/matlab/ (treeweave.m)

% out_dim is inferred by probing f at the box midpoint (=> 3 here).
% Fit f(x, y) on [-1, 1]^2 syntax is
% treeweave(callback, lower_bound, upper_bound, tolerance, name/value options).
% BEGIN DOCS_MULTIDIM
f   = @(x) [sin(x(1)+x(2)); cos(x(1)-x(2)); x(1)*x(2)];
obj = treeweave(f, [-1,-1], [1,1], 1e-6, 'max_memory_mib', 64);

[gx, gy] = meshgrid(linspace(-1,1,30));
Xgrid    = [gx(:), gy(:)];
Yhat     = obj.eval(Xgrid);                 % 900 x 3
Yt       = obj(Xgrid, 'transposed', true);  % 3 x 900 (struct-of-arrays)
% END DOCS_MULTIDIM

Yref = [sin(Xgrid(:,1)+Xgrid(:,2)), ...
        cos(Xgrid(:,1)-Xgrid(:,2)), ...
        Xgrid(:,1).*Xgrid(:,2)];
fprintf('out_dim (inferred): %d\n', obj.output_dim());
fprintf('2D->3D max abs error: %.3e\n', max(max(abs(Yhat - Yref))));
assert(max(max(abs(Yhat - Yref))) < 1e-5);

fprintf('transposed vs AoS match: %.3e\n', max(max(abs(Yt' - Yhat))));
assert(isequal(Yt', Yhat));
fprintf('Memory: %.1f KiB\n', obj.memory_usage()/1024);
delete(obj);
