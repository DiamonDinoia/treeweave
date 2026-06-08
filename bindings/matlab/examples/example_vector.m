% example_vector.m — 2-D input, 3-D output vector fit
addpath(fileparts(fileparts(mfilename('fullpath'))));

f   = @(x) [sin(x(1)+x(2)); cos(x(1)-x(2)); x(1)*x(2)];
% out_dim is inferred by probing f at the box midpoint (=> 3 here).
obj = treeweave(f, [-1,-1], [1,1], 1e-6, 'max_memory_mib', 64);

[gx, gy] = meshgrid(linspace(-1,1,30));
Xgrid    = [gx(:), gy(:)];
Yhat     = obj.eval(Xgrid);   % 900×3
Yref     = [sin(Xgrid(:,1)+Xgrid(:,2)), ...
             cos(Xgrid(:,1)-Xgrid(:,2)), ...
             Xgrid(:,1).*Xgrid(:,2)];
fprintf('out_dim (inferred): %d\n', obj.output_dim());
fprintf('2D->3D max abs error: %.3e\n', max(max(abs(Yhat - Yref))));

% Transposed (struct-of-arrays) layout: out_dim×N instead of N×out_dim.
Yt = obj(Xgrid, 'transposed', true);   % 3×900
fprintf('transposed vs AoS match: %.3e\n', max(max(abs(Yt' - Yhat))));
fprintf('Memory: %.1f KiB\n', obj.memory_usage()/1024);
delete(obj);
