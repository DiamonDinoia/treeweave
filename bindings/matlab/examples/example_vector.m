% example_vector.m: 2-D input, 3-D output vector fit
%
% Before running: add the build tree (tw_*.m stubs + treeweave_mex.mex/mexa64)
% to the MATLAB/Octave path. This script adds only the source dir (treeweave.m).
addpath(fileparts(fileparts(mfilename('fullpath'))));   % add bindings/matlab/ (treeweave.m)

f   = @(x) [sin(x(1)+x(2)); cos(x(1)-x(2)); x(1)*x(2)];
% out_dim is inferred by probing f at the box midpoint (=> 3 here).
% Fit f(x, y) on [-1, 1]^2 syntax is
% treeweave(callback, lower_bound, upper_bound, tolerance, name/value options).
obj = treeweave(f, [-1,-1], [1,1], 1e-6, 'max_memory_mib', 64);

[gx, gy] = meshgrid(linspace(-1,1,30));
Xgrid    = [gx(:), gy(:)];
% Evaluate obj on a grid and print the maximum error.
Yhat     = obj.eval(Xgrid);   % 900×3
Yref     = [sin(Xgrid(:,1)+Xgrid(:,2)), ...
             cos(Xgrid(:,1)-Xgrid(:,2)), ...
             Xgrid(:,1).*Xgrid(:,2)];
fprintf('out_dim (inferred): %d\n', obj.output_dim());
fprintf('2D->3D max abs error: %.3e\n', max(max(abs(Yhat - Yref))));

% Transposed (struct-of-arrays) layout: out_dim×N instead of N×out_dim.
% Evaluate obj with transposed output and print AoS/transposed parity.
Yt = obj(Xgrid, 'transposed', true);   % 3×900
fprintf('transposed vs AoS match: %.3e\n', max(max(abs(Yt' - Yhat))));
fprintf('Memory: %.1f KiB\n', obj.memory_usage()/1024);
delete(obj);
