% example_1d.m: 1-D scalar fit of exp(0.5*x)+sin(3*x) on [0,1]
%
% Before running: the build tree (where treeweave_mex.mex/mexa64 and the
% tw_*.m stubs live) must already be on the MATLAB/Octave path, e.g.:
%   addpath('/path/to/build/bindings-octave/bindings/matlab/generated')
% This script adds only the source dir (treeweave.m classdef).
addpath(fileparts(fileparts(mfilename('fullpath'))));   % add bindings/matlab/ (treeweave.m)

% BEGIN DOCS_MINIMAL
f   = @(x) exp(0.5*x(1)) + sin(3*x(1));
% Fit f(x) on [0, 1] syntax is
% treeweave(callback, lower_bound, upper_bound, tolerance, name/value options).
obj = treeweave(f, 0, 1, 1e-8);

Xtest = linspace(0, 1, 500)';
% Evaluate obj on 500 points and print the maximum error.
Yhat  = obj.eval(Xtest);
Yref  = exp(0.5*Xtest) + sin(3*Xtest);
fprintf('1D max abs error: %.3e\n', max(abs(Yhat - Yref)));
% END DOCS_MINIMAL
fprintf('Memory: %.1f KiB\n', obj.memory_usage()/1024);

% Name/value options ride after tol. tol_kind is the numeric
% treeweave_tol_kind_t code, not a string.
% BEGIN DOCS_OPTIONS
capped = treeweave(f, 0, 1, 1e-10, 'tol_kind', 3, 'max_memory_mib', 64);
% END DOCS_OPTIONS
Ycap = capped.eval(Xtest);
fprintf('1D capped max abs error: %.3e\n', max(abs(Ycap - Yref)));
assert(max(abs(Ycap - Yref)) < 1e-8);

delete(capped);
delete(obj);
