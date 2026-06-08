% example_1d.m — 1-D scalar fit: exp(0.5*x)+sin(3*x) on [0,1]
addpath(fileparts(fileparts(mfilename('fullpath'))));   % add bindings/matlab/

f   = @(x) exp(0.5*x(1)) + sin(3*x(1));
obj = treeweave(f, [0], [1], 1e-8, 'dim', 1, 'out_dim', 1);

Xtest = linspace(0, 1, 500)';
Yhat  = obj.eval(Xtest);
Yref  = exp(0.5*Xtest) + sin(3*Xtest);
fprintf('1D max abs error: %.3e\n', max(abs(Yhat - Yref)));
fprintf('Memory: %.1f KiB\n', obj.memory_usage()/1024);

delete(obj);
