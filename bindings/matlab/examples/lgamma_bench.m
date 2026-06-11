% lgamma_bench.m — treeweave vs the native gammaln (log-Gamma).
%
% The MATLAB/Octave member of the cross-language lgamma benchmark family (see
% examples/c++/lgamma_bench.cpp for the rationale). log-Gamma is fit on [3, 50)
% — smooth, positive, monotone, so relative error is well defined — with
% treeweave's default RelativeMax tolerance, then compared to the built-in
% gammaln (available in both MATLAB and Octave) on max relative error,
% throughput, and speedup.
%
% Before running: the build tree (treeweave_mex + the tw_*.m stubs) must be on
% the path. This script adds only the source dir (treeweave.m classdef).
addpath(fileparts(fileparts(mfilename('fullpath'))));   % bindings/matlab (treeweave.m)

a = 3;
b = 50;
obj = treeweave(@(x) gammaln(x(1)), [a], [b], 1e-10, 'dim', 1, 'out_dim', 1);

n = 1e6;
X = a + (b - a) * rand(n, 1);

% --- accuracy vs the library --------------------------------------------------
Yhat    = obj.eval(X);
Yref    = gammaln(X);
max_rel = max(abs(Yhat - Yref) ./ abs(Yref));

% --- throughput: treeweave vs gammaln ----------------------------------------
obj.eval(X);                    % warm-up
t = tic; Yhat = obj.eval(X); tw_s = toc(t);
t = tic; Yref = gammaln(X);  lib_s = toc(t);

fprintf('lgamma fit on [%.1f, %.1f), relative tol %.0e\n', a, b, 1e-10);
fprintf('  max rel err: %.3e\n', max_rel);
fprintf('  treeweave:  %.1f Mevals/s\n', n / (tw_s * 1e6));
fprintf('  library: %.1f Mevals/s\n', n / (lib_s * 1e6));
fprintf('  speedup: %.2fx\n', lib_s / tw_s);

delete(obj);
