% zeta_bench.m: treeweave vs a fair brute-force Riemann-zeta eval.
% See benchmarks/zeta_bench.cpp for the rationale. zeta(s) = sum_k k^-s summed
% until the tail is negligible (rel 1e-10, <=160 terms) yet smooth on [2,10]: fit
% once, eval a polynomial. Times single/multi/sorted; the native rate is sampled
% over n_native and reused. Octave spline, pchip and interp1 'linear' are the
% native interpolator baselines.
% No in-place out= (copy-on-write); every mode allocates.
% TREEWEAVE_BENCH_YAML=path emits YAML.
addpath(fileparts(fileparts(mfilename('fullpath'))));   % bindings/matlab (treeweave.m)

a = 2;
b = 10;
tol = 1e-10;

% Fair baseline: the first 160 terms (the cap), beyond that each k^-s adds less
% than 1e-10 relative on [2,10], so this matches a stop-early sum to tolerance.
% Vectorized (no early break) is the Octave/MATLAB idiom and stays a plain handle
% so the MEX fit callback can resolve it (a script-local function cannot).
zeta_partial = @(s) sum((1:160) .^ (-s));

fn = treeweave(@(x) zeta_partial(x(1)), [a], [b], tol, 'dim', 1, 'out_dim', 1);

n = 1e6;            % batch / sorted points
n_scalar = 10000;   % scalar-API points (interpreter per-point loops are slow)
n_native = 256;     % brute-force sample (<=160-term sum each)
X = a + (b - a) * rand(n, 1);
Xsorted = sort(X);

% --- accuracy of every method, treeweave included, on one check sample -------
% The criterion is treeweave's default RelativeMax, max|p - f| <= tol * max|f|;
% the report also prints the pointwise max |p - f| / |f| for every method.
Xcheck    = X(1:4096);
Refcheck  = arrayfun(zeta_partial, Xcheck);
err       = abs(fn.eval(Xcheck) - Refcheck);
tw_relmax = max(err) / max(abs(Refcheck));
max_rel   = max(err ./ abs(Refcheck));
Xnative   = X(1:n_native);

% --- native rate: brute-force sum over the small sample (mode-independent) ----
s = 0; for i = 1:n_native, s = s + zeta_partial(Xnative(i)); end       % warm-up
t = tic; s = 0; for i = 1:n_native, s = s + zeta_partial(Xnative(i)); end; nat_s = toc(t);
assert(isfinite(s));
nat_rate = n_native / (nat_s * 1e6);   % Mevals/s, reused in every mode

% --- single-eval: the scalar API, one point at a time ------------------------
Xs = X(1:n_scalar);
s = 0; for i = 1:n_scalar, s = s + fn.eval(Xs(i)); end                % warm-up
t = tic; s = 0; for i = 1:n_scalar, s = s + fn.eval(Xs(i)); end; tw_single_s = toc(t);
assert(isfinite(s));

% --- multi-eval: the unsorted batch ------------------------------------------
Yt = fn.eval(X);                       % warm-up
t = tic; Yt = fn.eval(X); tw_multi_s = toc(t);
assert(isfinite(sum(Yt)));

% --- sorted-eval: the 1-D ascending fast path --------------------------------
Yt = fn.eval(Xsorted, 'sorted', true);                  % warm-up
t = tic; Yt = fn.eval(Xsorted, 'sorted', true); tw_sorted_s = toc(t);
assert(isfinite(sum(Yt)));

% --- native interpolators, each grown until it meets tol on the check sample -
% Uniform knots, doubled from 16 until the method meets tol. spline is the
% not-a-knot cubic that interp1(..., 'spline') builds too; pchip is the
% shape-preserving cubic; interp1 'linear' is the lookup-table method.
methods = {'spline', 'pchip', 'interp1 linear'};
bl = struct('name', methods, 'size', '', 'relmax', 0, 'max_rel', 0, 'single', 0, 'multi', 0, 'sorted', 0);
for m = 1:numel(methods)
    nknots = 16;
    while true
        knots = linspace(a, b, nknots);
        y = arrayfun(zeta_partial, knots);
        switch methods{m}
            case 'spline', pp = spline(knots, y); ev = @(q) ppval(pp, q);
            case 'pchip', pp = pchip(knots, y); ev = @(q) ppval(pp, q);
            otherwise, ev = @(q) interp1(knots, y, q, 'linear');
        end
        err = abs(ev(Xcheck) - Refcheck);
        bl(m).relmax = max(err) / max(abs(Refcheck));
        if bl(m).relmax <= tol
            break;
        end
        nknots = 2 * nknots;
        assert(nknots <= 2^22, [methods{m} ' never met tol']);
    end
    bl(m).size = sprintf('%d knots', nknots);
    bl(m).max_rel = max(err ./ abs(Refcheck));
    s = 0; for i = 1:n_scalar, s = s + ev(Xs(i)); end                  % warm-up
    t = tic; s = 0; for i = 1:n_scalar, s = s + ev(Xs(i)); end; bl(m).single = n_scalar / (toc(t) * 1e6);
    assert(isfinite(s));
    Yt = ev(X);                                                        % warm-up
    t = tic; Yt = ev(X); bl(m).multi = n / (toc(t) * 1e6);
    assert(isfinite(sum(Yt)));
    Yt = ev(Xsorted);                                                  % warm-up
    t = tic; Yt = ev(Xsorted); bl(m).sorted = n / (toc(t) * 1e6);
    assert(isfinite(sum(Yt)));
end

% --- throughput (Mevals/s) and speedup per mode ------------------------------
tw_single = n_scalar / (tw_single_s * 1e6);
tw_multi  = n / (tw_multi_s * 1e6);
tw_sorted = n / (tw_sorted_s * 1e6);

fprintf('zeta(s) = sum_k k^-s (<=160 terms, stop at 1e-10), fit on [%.1f, %.1f], relative tol %.0e\n', a, b, tol);
fprintf('  max rel err: %.3e\n', max_rel);
fprintf('  single-eval  treeweave %.1f  native %.4f Mevals/s  speedup %.1fx\n', tw_single, nat_rate, tw_single / nat_rate);
fprintf('  multi-eval   treeweave %.1f  native %.4f Mevals/s  speedup %.1fx\n', tw_multi, nat_rate, tw_multi / nat_rate);
fprintf('  sorted-eval  treeweave %.1f  native %.4f Mevals/s  speedup %.1fx\n', tw_sorted, nat_rate, tw_sorted / nat_rate);
for m = 1:numel(bl)
    fprintf('  %s (%s, max rel err %.3e)  single %.2g  multi %.2f  sorted %.2f Mevals/s\n', bl(m).name, bl(m).size, bl(m).max_rel, bl(m).single, bl(m).multi, bl(m).sorted);
end

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
        fprintf(fid, 'relmax_err: %.17e\n', tw_relmax);
        fprintf(fid, 'max_rel_err: %.17e\n', max_rel);
        fprintf(fid, 'single_eval:\n  treeweave_mevals_s: %.17e\n  native_mevals_s: %.17e\n  speedup: %.17e\n', tw_single, nat_rate, tw_single / nat_rate);
        fprintf(fid, 'multi_eval:\n  treeweave_mevals_s: %.17e\n  native_mevals_s: %.17e\n  speedup: %.17e\n', tw_multi, nat_rate, tw_multi / nat_rate);
        fprintf(fid, 'sorted_eval:\n  treeweave_mevals_s: %.17e\n  native_mevals_s: %.17e\n  speedup: %.17e\n', tw_sorted, nat_rate, tw_sorted / nat_rate);
        fprintf(fid, 'interp:\n');
        for m = 1:numel(bl)
            fprintf(fid, '  - name: "%s"\n    size: "%s"\n', bl(m).name, bl(m).size);
            fprintf(fid, '    relmax_err: %.17e\n    max_rel_err: %.17e\n', bl(m).relmax, bl(m).max_rel);
            fprintf(fid, '    single_eval: %.17e\n    multi_eval: %.17e\n    sorted_eval: %.17e\n', bl(m).single, bl(m).multi, bl(m).sorted);
        end
        fclose(fid);
    end
end

delete(fn);
