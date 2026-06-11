% example_2d.m — 2-D scalar fit: sin(x+y) on [-1,1]^2
%
% Before running: add the build tree (tw_*.m stubs + treeweave_mex.mex/mexa64)
% to the MATLAB/Octave path. This script adds only the source dir (treeweave.m).
addpath(fileparts(fileparts(mfilename('fullpath'))));   % add bindings/matlab/ (treeweave.m)

f   = @(x) sin(x(1) + x(2));
obj = treeweave(f, [-1,-1], [1,1], 1e-7, 'dim', 2, 'out_dim', 1);

[gx, gy]  = meshgrid(linspace(-1,1,40));
Xgrid     = [gx(:), gy(:)];
Yhat      = obj.eval(Xgrid);
Yref      = sin(Xgrid(:,1) + Xgrid(:,2));
fprintf('2D->1D max abs error: %.3e\n', max(abs(Yhat - Yref)));
fprintf('Memory: %.1f KiB\n', obj.memory_usage()/1024);
delete(obj);
