% zeta_bench.m — treeweave vs a fair brute-force Riemann-zeta eval.
% See examples/c++/zeta_bench.cpp for the rationale. zeta(s) = sum_k k^-s summed
% until the tail is negligible (rel 1e-10, <=160 terms) yet smooth on [2,10]: fit
% once, eval a polynomial. Times single/multi/sorted; the native rate is sampled
% over n_native and reused. No in-place out= (copy-on-write); every mode allocates.
% TREEWEAVE_BENCH_YAML=path emits YAML.
addpath(fileparts(fileparts(mfilename('fullpath'))));   % bindings/matlab (treeweave.m)

a = 2;
b = 10;
tol = 1e-10;

% Fair baseline: the first 160 terms (the cap) — beyond that each k^-s adds less
% than 1e-10 relative on [2,10], so this matches a stop-early sum to tolerance.
% Vectorized (no early break) is the Octave/MATLAB idiom and stays a plain handle
% so the MEX fit callback can resolve it (a script-local function cannot).
zeta_partial = @(s) sum((1:160) .^ (-s));

obj = treeweave(@(x) zeta_partial(x(1)), [a], [b], tol, 'dim', 1, 'out_dim', 1);

n = 1e6;            % batch / sorted points
n_scalar = 10000;   % scalar-API points (interpreter per-point loops are slow)
n_native = 256;     % brute-force sample (<=160-term sum each)
X = a + (b - a) * rand(n, 1);
Xsorted = sort(X);

% --- accuracy vs the brute-force sum, on the n_native sample ------------------
Xnative = X(1:n_native);
Yhat    = obj.eval(Xnative);
Yref    = arrayfun(zeta_partial, Xnative);
max_rel = max(abs(Yhat - Yref) ./ abs(Yref));

% --- native rate: brute-force sum over the small sample (mode-independent) ----
s = 0; for i = 1:n_native, s = s + zeta_partial(Xnative(i)); end       % warm-up
t = tic; s = 0; for i = 1:n_native, s = s + zeta_partial(Xnative(i)); end; nat_s = toc(t);
assert(isfinite(s));
nat_rate = n_native / (nat_s * 1e6);   % Mevals/s, reused in every mode

% --- single-eval: the scalar API, one point at a time ------------------------
Xs = X(1:n_scalar);
s = 0; for i = 1:n_scalar, s = s + obj.eval(Xs(i)); end                % warm-up
t = tic; s = 0; for i = 1:n_scalar, s = s + obj.eval(Xs(i)); end; tw_single_s = toc(t);
assert(isfinite(s));

% --- multi-eval: the unsorted batch ------------------------------------------
Yt = obj.eval(X);                       % warm-up
t = tic; Yt = obj.eval(X); tw_multi_s = toc(t);
assert(isfinite(sum(Yt)));

% --- sorted-eval: the 1-D ascending fast path --------------------------------
Yt = obj.eval(Xsorted, 'sorted', true);                  % warm-up
t = tic; Yt = obj.eval(Xsorted, 'sorted', true); tw_sorted_s = toc(t);
assert(isfinite(sum(Yt)));

% --- throughput (Mevals/s) and speedup per mode ------------------------------
tw_single = n_scalar / (tw_single_s * 1e6);
tw_multi  = n / (tw_multi_s * 1e6);
tw_sorted = n / (tw_sorted_s * 1e6);

fprintf('zeta(s) = sum_k k^-s (<=160 terms, stop at 1e-10), fit on [%.1f, %.1f], relative tol %.0e\n', a, b, tol);
fprintf('  max rel err: %.3e\n', max_rel);
fprintf('  single-eval  treeweave %.1f  native %.4f Mevals/s  speedup %.1fx\n', tw_single, nat_rate, tw_single / nat_rate);
fprintf('  multi-eval   treeweave %.1f  native %.4f Mevals/s  speedup %.1fx\n', tw_multi, nat_rate, tw_multi / nat_rate);
fprintf('  sorted-eval  treeweave %.1f  native %.4f Mevals/s  speedup %.1fx\n', tw_sorted, nat_rate, tw_sorted / nat_rate);

% --- machine-readable YAML (optional) ----------------------------------------
% %.17e always carries a dot, so a YAML 1.1 parser reads each value as a float.
yaml_path = getenv('TREEWEAVE_BENCH_YAML');
if ~isempty(yaml_path)
    fid = fopen(yaml_path, 'w');
    if fid >= 0
        fprintf(fid, 'language: "octave"\n');
        fprintf(fid, 'domain: [%.17e, %.17e]\n', a, b);
        fprintf(fid, 'tol: %.17e\n', tol);
        fprintf(fid, 'n_pts: %d\n', n);
        fprintf(fid, 'max_rel_err: %.17e\n', max_rel);
        fprintf(fid, 'single_eval:\n  treeweave_mevals_s: %.17e\n  native_mevals_s: %.17e\n  speedup: %.17e\n', tw_single, nat_rate, tw_single / nat_rate);
        fprintf(fid, 'multi_eval:\n  treeweave_mevals_s: %.17e\n  native_mevals_s: %.17e\n  speedup: %.17e\n', tw_multi, nat_rate, tw_multi / nat_rate);
        fprintf(fid, 'sorted_eval:\n  treeweave_mevals_s: %.17e\n  native_mevals_s: %.17e\n  speedup: %.17e\n', tw_sorted, nat_rate, tw_sorted / nat_rate);
        fclose(fid);
    end
end

delete(obj);
