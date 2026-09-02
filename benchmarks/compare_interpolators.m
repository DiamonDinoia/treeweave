function status = compare_interpolators(varargin)
% compare_interpolators: treeweave against the interpolators an Octave user reaches for.
%
% Same protocol as benchmarks/compare_interpolators.py, .jl and .cpp: for each
% target and each requested accuracy, every method is grown until it MEETS that
% accuracy on a dense test grid, and only then measured. The table compares like
% with like: same achieved error, different cost.
%
% Reported per method:
%   f-evals   calls to the target needed to build the approximation
%   memory    bytes the approximation's knots and coefficients occupy
%   Meval/s   evaluation throughput on a shuffled batch of 1e6 points
%   max err   achieved max error relative to max|f| on the test grid
%
% The comparison set is what a stock Octave or MATLAB install offers:
%   spline (not-a-knot cubic)  spline(x, y) + ppval, the default reach
%   pchip (monotone cubic)     shape-preserving Hermite, third order
%
% interp1(..., 'spline') and interp1(..., 'pchip') build the same piecewise
% polynomials and are left out as duplicates. Octave has no Chebyshev package in
% core; the spectral peer is measured in the Python, Julia and C++ tables.
%
% Run (repo root, with the mex binding and bindings/matlab on the path):
%   octave --eval "exit(compare_interpolators())"
%   octave --eval "exit(compare_interpolators('--self-test'))"
%   octave --eval "exit(compare_interpolators('--rst'))"
%   octave --eval "exit(compare_interpolators('--check-docs'))"

  if any(strcmp(varargin, '--self-test'))
    status = self_test();
    return;
  end
  k = consts();
  rand('twister', 0);
  rows = containers.Map();
  methods = method_list();
  order = methods(:, 1)';
  for t = target_list()'
    ctx = make_ctx(t{1});
    printf('\n%s\n', ctx.title);
    printf('  %6s  %-26s %10s %11s %9s %9s\n', ...
           'tol', 'method', 'f-evals', 'memory', 'Meval/s', 'max err');
    for tol = k.TOLERANCES
      for m = 1:size(methods, 1)
        r = methods{m, 2}(ctx, tol);
        rows(row_key(ctx.title, tol, r.name)) = r;
        if isnan(r.rate); rate = 'n/a'; else; rate = sprintf('%.1f', r.rate); end
        printf('  %6s  %-26s %10d %11s %9s %9.1e\n', ...
               fmt_tol(tol), r.name, r.evals, fmt_memory(r.memory), rate, r.err);
        fflush(stdout);
      end
    end
  end
  if any(strcmp(varargin, '--rst'))
    printf('\n%s\n', as_rst(rows, order));
  end
  status = check(rows);
  if any(strcmp(varargin, '--check-docs'))
    status = max(status, check_docs(rows, order));
    status = max(status, check_scipy_agreement(rows));
  end
end

% --- constants ---------------------------------------------------------------

function K = consts()
  persistent k;
  if isempty(k)
    k.N_TEST = 200001;
    k.N_BENCH = 1000000;
    k.REPEATS = 5;
    k.MAX_SIZE = 2^20;
    k.TREEWEAVE = 'treeweave';
    k.TOLERANCES = [1e-6, 1e-10];
    k.DOCS_TABLE = 'docs/guides/performance.rst';
    % The per-language subsections of "Against the alternatives". Every one
    % carries the same target labels and a treeweave row, so the parser is
    % scoped to one of them.
    k.SECTIONS = {'In Python', 'In Julia', 'In C++', 'In Octave'};
    k.SECTION = 'In Octave';
    % The cross-language claim the Octave table settles: Octave's spline and
    % scipy's CubicSpline are both not-a-knot, so the two must agree row for row.
    k.PYTHON_SECTION = 'In Python';
    k.SCIPY = 'scipy CubicSpline';
    k.NOT_A_KNOT = 'spline (not-a-knot cubic)';
    k.POLE = '1/(x - 1.05) on [-1, 1]';
    % The methods the near-pole gate holds treeweave against: piecewise
    % polynomials on a uniform knot grid, which is what refinement has to beat.
    k.UNIFORM = {'spline (not-a-knot cubic)', 'pchip (monotone cubic)'};
    % How each target is written in docs/guides/performance.rst.
    k.TITLES = containers.Map( ...
      {'zeta(s), 1000 terms, on [2, 10]', '1/(x - 1.05) on [-1, 1]', 'sin(30 x) on [0, 1]'}, ...
      {'``zeta(s)``, 1000 terms, on [2, 10]', '``1/(x - 1.05)`` on [-1, 1]', '``sin(30 x)`` on [0, 1]'});
  end
  K = k;
end

function t = target_list()
  t = { ...
    % An expensive smooth function: the case treeweave is built for.
    struct('title', 'zeta(s), 1000 terms, on [2, 10]', 'f', @zeta_n, 'a', 2.0, 'b', 10.0); ...
    % A pole just outside the domain: the case adaptivity is built for.
    struct('title', '1/(x - 1.05) on [-1, 1]', 'f', @(x) 1.0 ./ (x - 1.05), 'a', -1.0, 'b', 1.0); ...
    % Oscillation: nobody's favourite, included so the table is not cherry-picked.
    struct('title', 'sin(30 x) on [0, 1]', 'f', @(x) sin(30.0 * x), 'a', 0.0, 'b', 1.0)};
end

% Order here is the order of the rows in the printed table and in the docs.
function m = method_list()
  m = { consts().TREEWEAVE, @m_treeweave; ...
        'spline (not-a-knot cubic)', @m_spline; ...
        'pchip (monotone cubic)', @m_pchip };
end

function y = zeta_n(s)
  y = sum((1:1000) .^ (-s));
end

% --- the harness -------------------------------------------------------------

function ctx = make_ctx(t)
  k = consts();
  ctx = t;
  ctx.x_test = linspace(t.a, t.b, k.N_TEST)';
  ctx.y_test = arrayfun(t.f, ctx.x_test);
  ctx.scale = max(abs(ctx.y_test));
  ctx.xs = t.a + (t.b - t.a) * rand(k.N_BENCH, 1);
end

function e = max_error(ctx, ev)
  e = max(abs(ev(ctx.x_test) - ctx.y_test)) / ctx.scale;
end

% Mevals/s, minimum over repeats (the least contaminated run).
function r = throughput(ev, xs)
  ev(xs);   % warm up: first touch of the coefficient array
  best = Inf;
  for i = 1:consts().REPEATS
    t0 = tic();
    ev(xs);
    best = min(best, toc(t0));
  end
  r = numel(xs) / best / 1e6;
end

function r = row(name, evals, memory, rate, err)
  r = struct('name', name, 'evals', evals, 'memory', memory, 'rate', rate, 'err', err);
end

% A method that never reached the tolerance. memory 0 marks the failure.
function r = missed(name, evals, err)
  r = row(name, evals, 0, NaN, err);
end

% Double the size until the approximation meets tol, then measure it.
% build(n) returns a struct with fields ev, memory, evals.
function r = grow(name, build, ctx, tol, start)
  n = start;
  err = Inf;
  while n <= consts().MAX_SIZE
    b = build(n);
    err = max_error(ctx, b.ev);
    if err <= tol
      r = row(name, b.evals, b.memory, throughput(b.ev, ctx.xs), err);
      return;
    end
    n = n * 2;
  end
  r = missed(name, n, err);
end

% Ask an adaptive method for a tighter tolerance until it delivers tol.
% build(requested) returns a struct with fields ev, memory, evals and obj.
function r = tighten(name, build, ctx, tol)
  requested = tol;
  err = Inf;
  for i = 1:6
    b = build(requested);
    err = max_error(ctx, b.ev);
    if err <= tol
      r = row(name, b.evals, b.memory, throughput(b.ev, ctx.xs), err);
      delete(b.obj);
      return;
    end
    delete(b.obj);
    requested = requested / 100;
  end
  r = missed(name, 0, err);
end

% --- the methods -------------------------------------------------------------

% The mex fit callback resolves a plain function handle, so the call counter
% lives in a global rather than in a closure variable.
function y = counted_call(f, x)
  global TW_NCALLS;
  TW_NCALLS = TW_NCALLS + 1;
  y = f(x(1));
end

function b = build_treeweave(ctx, requested)
  global TW_NCALLS;
  TW_NCALLS = 0;
  obj = treeweave(@(x) counted_call(ctx.f, x), ctx.a, ctx.b, requested);
  b = struct('ev', @(x) obj.eval(x), 'memory', obj.memory_usage(), ...
             'evals', TW_NCALLS, 'obj', obj);
end

function r = m_treeweave(ctx, tol)
  r = tighten(consts().TREEWEAVE, @(q) build_treeweave(ctx, q), ctx, tol);
end

function b = build_pp(ctx, n, kind)
  knots = linspace(ctx.a, ctx.b, n);
  y = arrayfun(ctx.f, knots);
  if strcmp(kind, 'spline')
    pp = spline(knots, y);
  else
    pp = pchip(knots, y);
  end
  % breaks and coefs are what an evaluation reads; both are double.
  b = struct('ev', @(x) ppval(pp, x), ...
             'memory', (numel(pp.coefs) + numel(pp.breaks)) * 8, 'evals', n);
end

function r = m_spline(ctx, tol)
  r = grow('spline (not-a-knot cubic)', @(n) build_pp(ctx, n, 'spline'), ctx, tol, 16);
end

function r = m_pchip(ctx, tol)
  r = grow('pchip (monotone cubic)', @(n) build_pp(ctx, n, 'pchip'), ctx, tol, 16);
end

% --- formatting --------------------------------------------------------------

function s = fmt_memory(memory)
  if memory == 0
    s = 'n/a';
  else
    s = sprintf('%.1f KiB', memory / 1024);
  end
end

function s = fmt_tol(tol)
  s = strrep(sprintf('%.0e', tol), 'e-0', 'e-');
end

function key = row_key(title, tol, name)
  key = sprintf('%s|%s|%s', title, fmt_tol(tol), name);
end

% --- the gates ---------------------------------------------------------------

% Every published claim this benchmark can settle.
%
% Every treeweave row must have reached the accuracy it was asked for, and near
% the pole refinement must beat a piecewise polynomial on a uniform knot grid on
% both f-evals and memory. Every method in this field is such a polynomial, so
% the second gate covers the whole field. It cannot cover the first: a fit that
% never converged reports zero memory, which beats any competitor.
function status = check(rows)
  k = consts();
  failures = {};
  for key = keys(rows)
    parts = strsplit(key{1}, '|');
    if ~strcmp(parts{3}, k.TREEWEAVE); continue; end
    r = rows(key{1});
    if r.err > 10 * str2double(parts{2})
      failures{end+1} = sprintf('%s @ %s: treeweave err %.2e > 10x tol', ...
                                parts{1}, parts{2}, r.err);
    end
    if r.memory == 0
      failures{end+1} = sprintf('%s @ %s: treeweave never reached the tolerance', ...
                                parts{1}, parts{2});
    end
  end
  for tol = k.TOLERANCES
    tw = rows(row_key(k.POLE, tol, k.TREEWEAVE));
    for u = 1:numel(k.UNIFORM)
      name = k.UNIFORM{u};
      key = row_key(k.POLE, tol, name);
      if ~isKey(rows, key); continue; end
      other = rows(key);
      if other.memory == 0; continue; end
      cols = {'f-evals', tw.evals, other.evals; 'memory', tw.memory, other.memory};
      for c = 1:2
        if cols{c, 2} >= cols{c, 3}
          failures{end+1} = sprintf('near the pole at %s: treeweave %s %d is not below %s''s %d', ...
                                    fmt_tol(tol), cols{c, 1}, cols{c, 2}, name, cols{c, 3});
        end
      end
    end
  end
  status = report(failures, 'every claim holds', 'claim(s) failed');
end

function status = report(failures, ok, what)
  for i = 1:numel(failures)
    printf('FAIL: %s\n', failures{i});
  end
  if isempty(failures)
    printf('%s\n', ok);
    status = 0;
  else
    printf('%d %s\n', numel(failures), what);
    status = 1;
  end
end

% Octave's spline and scipy's CubicSpline must agree, row for row.
%
% Both are the not-a-knot cubic on a uniform knot grid, so the same target at
% the same tolerance must need the same knots and the same bytes in both. This
% is what makes the Julia table's 262144 knots a statement about
% `cubic_spline_interpolation`'s natural boundary condition and not about the
% language. The published Python table is the reference: it is itself held to a
% fresh measurement by benchmarks/compare_interpolators.py --check-docs.
function status = check_scipy_agreement(rows, path, section)
  k = consts();
  if nargin < 2; path = k.DOCS_TABLE; end
  if nargin < 3; section = k.PYTHON_SECTION; end
  scipy = parse_docs_table(fileread(path), {k.SCIPY}, section);
  failures = {};
  if isempty(keys(scipy))
    failures{end+1} = sprintf('%s carries no %s rows under %s', path, k.SCIPY, section);
  end
  for t = target_list()'
    for tol = k.TOLERANCES
      mine = rows(row_key(t{1}.title, tol, k.NOT_A_KNOT));
      key = sprintf('%s|%s|%s', t{1}.title, fmt_tol(tol), k.SCIPY);
      if ~isKey(scipy, key)
        failures{end+1} = sprintf('%s has no %s row for %s', path, k.SCIPY, key);
        continue;
      end
      was = scipy(key);
      cols = {'f-evals', sprintf('%d', mine.evals), was{1}; ...
              'memory', fmt_memory(mine.memory), was{2}};
      for c = 1:2
        if ~strcmp(cols{c, 2}, cols{c, 3})
          failures{end+1} = sprintf('%s @ %s: Octave spline %s is %s, %s is %s', ...
                                    t{1}.title, fmt_tol(tol), cols{c, 1}, cols{c, 2}, ...
                                    k.SCIPY, cols{c, 3});
        end
      end
    end
  end
  status = report(failures, 'Octave spline agrees with scipy CubicSpline row for row', ...
                  'not-a-knot disagreement(s)');
end

% Emit the docs table for the Octave field. Paste over the table in performance.rst.
function s = as_rst(rows, order)
  k = consts();
  out = {};
  for t = target_list()'
    label = k.TITLES(t{1}.title);
    out(end+1:end+13) = {label, repmat('^', 1, numel(label)), '', ...
                         '.. list-table::', '   :header-rows: 1', ...
                         '   :widths: 7 26 9 11 8 9', '', ...
                         '   * - tol', '     - method', '     - f-evals', ...
                         '     - memory', '     - Meval/s', '     - max err'};
    for tol = k.TOLERANCES
      for o = 1:numel(order)
        key = row_key(t{1}.title, tol, order{o});
        if ~isKey(rows, key); continue; end
        r = rows(key);
        if isnan(r.rate); rate = 'n/a'; else; rate = sprintf('%.0f', r.rate); end
        out(end+1:end+6) = {sprintf('   * - %s', fmt_tol(tol)), ...
                            sprintf('     - %s', r.name), ...
                            sprintf('     - %d', r.evals), ...
                            sprintf('     - %s', fmt_memory(r.memory)), ...
                            sprintf('     - %s', rate), ...
                            sprintf('     - %.1e', r.err)};
      end
    end
    out{end+1} = '';
  end
  s = strjoin(out, "\n");
end

% Read the published table back as a map of row_key -> {f-evals, memory}.
%
% Each target's table is introduced by a line holding exactly that target's
% label, so the parser keys rows on the most recent such line. Only rows under
% `section` are read, and only for methods in `order`: the other language
% sections repeat both the labels and the treeweave row.
function table = parse_docs_table(text, order, section)
  k = consts();
  labels = containers.Map(values(k.TITLES), keys(k.TITLES));
  table = containers.Map();
  title = '';
  cells = {};
  % A document with no section heading at all is one table (the self-test).
  inside = true;
  for line = strsplit(text, "\n")
    s = strtrim(line{1});
    if any(strcmp(s, k.SECTIONS))
      [table, cells] = flush(table, title, cells, order);
      inside = strcmp(s, section);
      title = '';
      cells = {};
    elseif ~inside
      continue;
    elseif isKey(labels, s)
      [table, cells] = flush(table, title, cells, order);
      title = labels(s);
      cells = {};
    elseif strncmp(s, '* -', 3)
      [table, cells] = flush(table, title, cells, order);
      cells = {strtrim(s(4:end))};
    elseif strncmp(s, '- ', 2) && ~isempty(cells)
      cells{end+1} = strtrim(s(3:end));
    elseif isempty(s)
      [table, cells] = flush(table, title, cells, order);
      cells = {};
    end
  end
  table = flush(table, title, cells, order);
end

function [table, cells] = flush(table, title, cells, order)
  if ~isempty(title) && numel(cells) == 6 && any(strcmp(cells{2}, order))
    table(sprintf('%s|%s|%s', title, cells{1}, cells{2})) = {cells{3}, cells{4}};
  end
  cells = {};
end

% The published table must be the one this script measures.
%
% Only the deterministic columns are compared: f-evals and memory are set by the
% algorithms, not by the machine. Regenerate with --rst after a change that
% moves them.
function status = check_docs(rows, order, path, section)
  k = consts();
  if nargin < 3; path = k.DOCS_TABLE; end
  if nargin < 4; section = k.SECTION; end
  published = parse_docs_table(fileread(path), order, section);
  failures = {};
  mine = {};
  for key = sort(keys(rows))
    r = rows(key{1});
    if ~any(strcmp(r.name, order)); continue; end
    mine{end+1} = key{1};
    if ~isKey(published, key{1})
      failures{end+1} = sprintf('%s has no row for %s', path, key{1});
      continue;
    end
    was = published(key{1});
    cols = {'f-evals', sprintf('%d', r.evals), was{1}; ...
            'memory', fmt_memory(r.memory), was{2}};
    for c = 1:2
      if ~strcmp(cols{c, 2}, cols{c, 3})
        failures{end+1} = sprintf('%s: %s is %s, docs say %s', ...
                                  key{1}, cols{c, 1}, cols{c, 2}, cols{c, 3});
      end
    end
  end
  for key = keys(published)
    if ~any(strcmp(key{1}, mine))
      failures{end+1} = sprintf('%s has a row this run did not produce: %s', path, key{1});
    end
  end
  status = report(failures, 'docs table matches', 'docs-table mismatch(es)');
end

% --- self-test ---------------------------------------------------------------

% Positive control: every gate must fire on the thing it is supposed to catch.
function status = self_test()
  k = consts();
  order = {k.TREEWEAVE, 'spline (not-a-knot cubic)'};
  failures = {};
  good = containers.Map();
  for tol = k.TOLERANCES
    good(row_key(k.POLE, tol, k.TREEWEAVE)) = row(k.TREEWEAVE, 100, 1024, 200.0, tol / 2);
    good(row_key(k.POLE, tol, 'spline (not-a-knot cubic)')) = ...
      row('spline (not-a-knot cubic)', 1000, 8192, 20.0, tol / 2);
    good(row_key(k.POLE, tol, 'pchip (monotone cubic)')) = ...
      row('pchip (monotone cubic)', 900, 4096, 20.0, tol / 2);
  end
  if check(good) ~= 0
    failures{end+1} = 'a winning table was reported as failing';
  end
  % Zero memory beats every competitor, so only the per-row gate sees that one.
  drift = {'more f-evals than the cubic spline', 5000, 1024, 1e-11; ...
           'more memory than the cubic spline', 100, 2^20, 1e-11; ...
           'treeweave never converged', 100, 0, 1e-11; ...
           'an achieved error above 10x the tolerance', 100, 1024, 1e-8};
  for d = 1:size(drift, 1)
    bad = containers.Map(keys(good), values(good));
    bad(row_key(k.POLE, 1e-10, k.TREEWEAVE)) = ...
      row(k.TREEWEAVE, drift{d, 2}, drift{d, 3}, 200.0, drift{d, 4});
    if check(bad) == 0
      failures{end+1} = sprintf('%s: not detected', drift{d, 1});
    end
  end

  rows = containers.Map();
  for key = keys(good)
    r = good(key{1});
    if any(strcmp(r.name, order)); rows(key{1}) = r; end
  end
  path = tempname();
  table = as_rst(rows, order);
  write_file(path, table);
  if check_docs(rows, order, path) ~= 0
    failures{end+1} = 'the table as emitted did not match itself';
  end
  drifted = containers.Map(keys(rows), values(rows));
  drifted(row_key(k.POLE, 1e-10, k.TREEWEAVE)) = row(k.TREEWEAVE, 101, 1024, 200.0, 1e-11);
  if check_docs(drifted, order, path) == 0
    failures{end+1} = 'an f-eval drift against the docs was not detected';
  end
  write_file(path, '');
  if check_docs(rows, order, path) == 0
    failures{end+1} = 'an empty docs table was accepted';
  end

  % The other language sections repeat the labels and the treeweave rows. Their
  % numbers must not be read as this section's.
  other = strrep(table, "     - 100\n", "     - 4096\n");
  write_file(path, sprintf('%s\n\n%s\n%s\n\n%s', k.SECTIONS{1}, other, k.SECTION, table));
  if check_docs(rows, order, path) ~= 0
    failures{end+1} = 'the other section''s rows were read as this section''s';
  end
  write_file(path, sprintf('%s\n\n%s\n%s\n\n%s', k.SECTIONS{1}, table, k.SECTION, other));
  if check_docs(rows, order, path) == 0
    failures{end+1} = 'a drift in this section''s rows was not detected';
  end

  % The not-a-knot cross-check: an agreeing Python section must pass, a drifted
  % one must fail, and a page with no scipy rows at all must not read as agreement.
  agree = containers.Map();
  scipy_rows = containers.Map();
  for tt = target_list()'
    for tol = k.TOLERANCES
      agree(row_key(tt{1}.title, tol, k.NOT_A_KNOT)) = row(k.NOT_A_KNOT, 2048, 81920, 10.0, 1e-11);
      scipy_rows(row_key(tt{1}.title, tol, k.SCIPY)) = row(k.SCIPY, 2048, 81920, 10.0, 1e-11);
    end
  end
  python_table = as_rst(scipy_rows, {k.SCIPY});
  write_file(path, sprintf('%s\n\n%s', k.PYTHON_SECTION, python_table));
  if check_scipy_agreement(agree, path) ~= 0
    failures{end+1} = 'an agreeing not-a-knot table was reported as disagreeing';
  end
  write_file(path, sprintf('%s\n\n%s', k.PYTHON_SECTION, ...
                           strrep(python_table, "     - 2048\n", "     - 4096\n")));
  if check_scipy_agreement(agree, path) == 0
    failures{end+1} = 'a not-a-knot disagreement with scipy was not detected';
  end
  write_file(path, sprintf('%s\n\n%s', k.SECTION, python_table));
  if check_scipy_agreement(agree, path) == 0
    failures{end+1} = 'a page with no scipy rows was read as agreement';
  end
  delete(path);

  status = report(failures, 'self-test passed', 'self-test case(s) failed');
end

function write_file(path, text)
  fid = fopen(path, 'w');
  fputs(fid, text);
  fclose(fid);
end
