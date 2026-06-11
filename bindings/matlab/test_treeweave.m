function test_treeweave()
% test_treeweave -- smoke/parity tests for the treeweave MATLAB wrapper.
% Exits via error() on any failure.
% Run headless:  matlab -batch "addpath('.'); test_treeweave"

passed = 0;
failed = 0;

    function check(cond, name)
        if cond
            fprintf('PASS  %s\n', name);
            passed = passed + 1;
        else
            fprintf('FAIL  %s\n', name);
            failed = failed + 1;
        end
    end

    % Substring test that works on both MATLAB and Octave (Octave has no
    % `contains`); strfind is available on both.
    function tf = has_substr(s, sub)
        tf = ~isempty(strfind(s, sub)); %#ok<STREMP>
    end

% ================================================================== %
% 1. 1-D scalar fit: exp(0.5*x)+sin(3*x) on [0,1]
% ================================================================== %
f1   = @(x) exp(0.5*x(1)) + sin(3*x(1));
obj1 = treeweave(f1, [0], [1], 1e-8, 'dim', 1, 'out_dim', 1);

% The fit domain is [a, b); evaluating exactly at the upper corner b is allowed
% as a convenience (it returns the boundary value), but this accuracy sweep
% stays inside [a, b).
Xtest = linspace(0, 1, 201)';
Xtest(end) = [];
Yhat  = obj1.eval(Xtest);
Yref  = exp(0.5*Xtest) + sin(3*Xtest);
err1  = max(abs(Yhat(:) - Yref(:)));
check(err1 < 1e-5, sprintf('1D accuracy (max err = %.2e)', err1));

Yhat2 = obj1(Xtest);
check(isnumeric(Yhat2) && max(abs(Yhat2(:) - Yhat(:))) < 1e-12, ...
      '1D subsref syntax obj(X)');

y1    = obj1.eval([0.5]);
yref1 = exp(0.25) + sin(1.5);
check(abs(y1 - yref1) < 1e-5, '1D single-point eval');

delete(obj1);

% ================================================================== %
% 2. 2-D -> 3-D vector fit
% ================================================================== %
f2   = @(x) [sin(x(1)+x(2)); cos(x(1)-x(2)); x(1)*x(2)];
% out_dim omitted -> inferred by probing f2 at the box midpoint.
obj2 = treeweave(f2, [-1,-1], [1,1], 1e-6, 'dim', 2, 'max_memory_mib', 64);
check(obj2.output_dim() == 3, '2D->3D out_dim inferred == 3');

[gx, gy] = meshgrid(linspace(-1,1,50), linspace(-1,1,50));
Xgrid    = [gx(:), gy(:)];
Ygrid    = obj2.eval(Xgrid);
Yref2    = [sin(Xgrid(:,1)+Xgrid(:,2)), ...
             cos(Xgrid(:,1)-Xgrid(:,2)), ...
             Xgrid(:,1).*Xgrid(:,2)];

err2 = max(max(abs(Ygrid - Yref2)));
check(err2 < 1e-4, sprintf('2D->3D accuracy (max err = %.2e)', err2));
check(size(Ygrid,1)==2500 && size(Ygrid,2)==3, '2D->3D output shape 2500x3');
check(obj2.memory_usage() > 0, '2D->3D memory_usage > 0');

% Transposed (SoA) layout: out_dim×N, equal to the AoS result transposed.
Yt = obj2(Xgrid, 'transposed', true);
check(size(Yt,1)==3 && size(Yt,2)==2500, 'transposed shape 3x2500');
check(max(max(abs(Yt' - Ygrid))) < 1e-12, 'transposed == AoS (transposed)');

delete(obj2);

% ================================================================== %
% 3. NaN out-of-domain
% ================================================================== %
f3   = @(x) sin(x(1));
obj3 = treeweave(f3, [0], [1], 1e-8);
y_out = obj3.eval([-5]);
check(isnan(y_out), 'Out-of-domain returns NaN');
delete(obj3);

% ================================================================== %
% 4. Erroring callback is caught and surfaced (no crash/segfault)
% ================================================================== %
f_bad  = @(x) error('boom: intentional test error');
caught = false;
msg_ok = false;
try
    % out_dim given so the raise happens inside the fit trampoline (not the
    % inference probe), exercising the reverse-exception path.
    obj_bad = treeweave(f_bad, [0], [1], 1e-8, 'out_dim', 1);
    delete(obj_bad);
catch ME
    caught = true;
    msg_ok = has_substr(ME.message, 'boom') || ...
             strcmp(ME.identifier, 'treeweave:callback');
end
check(caught,  'Erroring callback: constructor threw');
check(msg_ok,  'Erroring callback: message surfaced');

% ================================================================== %
% 5. Too-tight tol + low max_depth -> fit error
% ================================================================== %
f5      = @(x) sin(100*x(1));
caught5 = false;
msg5_ok = false;
try
    obj5 = treeweave(f5, [0], [1], 1e-15, ...
                  'max_depth', 2, 'max_memory_mib', 1);
    delete(obj5);
catch ME5
    caught5 = true;
    msg5_ok = has_substr(ME5.message, 'MaxDepth')     || ...
              has_substr(ME5.message, 'MemoryBudget')  || ...
              has_substr(ME5.message, 'depth')          || ...
              has_substr(ME5.message, 'memory')         || ...
              strcmp(ME5.identifier, 'treeweave:fit');
end
check(caught5,  'Tight-tol/low-depth: error thrown');
check(msg5_ok,  'Tight-tol/low-depth: message mentions MaxDepth/MemoryBudget or treeweave:fit');

% ================================================================== %
% 6. sorted == general batch for 1-D (bit-exact on sorted input)
% ================================================================== %
f6   = @(x) sin(x(1)) .* exp(-0.2*x(1));
obj6 = treeweave(f6, [0], [5], 1e-9, 'dim', 1, 'out_dim', 1);
xs6  = sort(rand(256,1) * 5);
ybatch = obj6(xs6);
ysort  = obj6(xs6, 'sorted', true);
check(isequal(ybatch, ysort), 'sorted == general batch (bit-exact)');
delete(obj6);

% ================================================================== %
% 7. Strict size / flag validation
% ================================================================== %
f7a = @(x) exp(x(1));
o1  = treeweave(f7a, [0], [1], 1e-6, 'dim', 1, 'out_dim', 1);          % dim1 out1
f7b = @(x) sin(x(1)+x(2));
o2  = treeweave(f7b, [0,0], [1,1], 1e-6, 'dim', 2, 'out_dim', 1);      % dim2 out1

ok = false;
try
    o2([0.5]);
catch
    ok = true;
end
check(ok, 'point with wrong column count errors');

ok = false;
try
    o2(zeros(5,3));
catch
    ok = true;
end
check(ok, 'batch with wrong column count errors');

ok = false;
try
    o2(zeros(5,2), 'sorted', true);
catch
    ok = true;
end
check(ok, 'sorted with dim~=1 errors');

ok = false;
try
    o1(linspace(0,1,10)', 'transposed', true);
catch
    ok = true;
end
check(ok, 'transposed with out_dim==1 errors');

ok = false;
try
    o1(linspace(0,1,10)', 'sorted', true, 'transposed', true);
catch
    ok = true;
end
check(ok, 'both flags errors');

delete(o1); delete(o2);

% ================================================================== %
% Summary
% ================================================================== %
fprintf('\n--- Results: %d passed, %d failed ---\n', passed, failed);
if failed > 0
    error('test_treeweave:failures', '%d test(s) failed.', failed);
end

end  % function test_treeweave
