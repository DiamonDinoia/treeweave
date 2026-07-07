% example_1d.m — 1-D scalar fit: exp(0.5*x)+sin(3*x) on [0,1]
%
% Before running: the build tree (where treeweave_mex.mex/mexa64 and the
% tw_*.m stubs live) must already be on the MATLAB/Octave path, e.g.:
%   addpath('/path/to/build/bindings-matlab/bindings/matlab/mwrap_gen')
% This script adds only the source dir (treeweave.m classdef).
addpath(fileparts(fileparts(mfilename('fullpath'))));   % add bindings/matlab/ (treeweave.m)

f   = @(x) exp(0.5*x(1)) + sin(3*x(1));
% Fit f(x) on [0, 1] syntax is
% treeweave(callback, lower_bound, upper_bound, tolerance, name/value options).
obj = treeweave(f, [0], [1], 1e-8, 'dim', 1, 'out_dim', 1);

Xtest = linspace(0, 1, 500)';
% Evaluate obj on 500 points and print the maximum error.
Yhat  = obj.eval(Xtest);
Yref  = exp(0.5*Xtest) + sin(3*Xtest);
fprintf('1D max abs error: %.3e\n', max(abs(Yhat - Yref)));
fprintf('Memory: %.1f KiB\n', obj.memory_usage()/1024);

delete(obj);
