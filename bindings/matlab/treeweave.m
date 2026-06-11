classdef treeweave < handle
% TREEWEAVE  MATLAB handle class wrapping a piecewise-polynomial approximator.
%
% This is a thin classdef over the mwrap-generated tw_*.m stubs (see
% treeweave.mw). The opaque treeweave_t handle lives in the `mwptr` property, which
% mwrap reads/writes via its R2008OO object-pointer convention.
%
% CONSTRUCTION
%   obj = treeweave(f, a, b, tol)
%   obj = treeweave(f, a, b, tol, Name, Value, ...)
%
%   f   — function_handle  @(x) y   (x: 1×dim row, y: 1×out_dim row)
%   a,b — 1×dim double domain corners (lower/upper)
%   tol — scalar double tolerance
%
%   Name/Value options:
%     'dim'                   input dimension (default: numel(a))
%     'out_dim'               output dimension (default: inferred by probing f
%                             once at the box midpoint)
%     'tol_kind'              0=REL_TAIL..5=ABS_L2 (default: 2=REL_MAX)
%     'max_depth'             max tree depth (default: 50)
%     'max_memory_mib'        memory budget in MiB (default: -1 → auto: 4/8/16 MiB by dim)
%     'allow_max_depth_leaves' int bool (default: 0)
%     'min_uniform_depth'     (default: 0)
%
% USAGE
%   y = obj.eval(X)                  X is N×dim, y is N×out_dim
%   y = obj(X)                       same (via subsref)
%   y = obj(X, 'sorted', true)       1-D ascending fast path (dim==1)
%   y = obj(X, 'transposed', true)   y is out_dim×N (out_dim>1)
%   n = obj.memory_usage()
%   obj.print_stats()
%   delete(obj)                      frees C-side memory

    properties (SetAccess = private)
        mwptr        % opaque treeweave_t handle (mwrap object pointer)
    end
    properties (Access = private)
        input_dim_
        output_dim_
    end

    methods

        function obj = treeweave(f, a, b, tol, varargin)
            p = inputParser;
            addRequired(p, 'f');
            addRequired(p, 'a');
            addRequired(p, 'b');
            addRequired(p, 'tol');
            addParameter(p, 'dim',                    numel(a));
            addParameter(p, 'out_dim',                1);
            addParameter(p, 'tol_kind',               2);
            addParameter(p, 'max_depth',              50);
            addParameter(p, 'max_memory_mib',         -1); % -1 = C ABI auto (4/8/16 MiB by dim)
            addParameter(p, 'allow_max_depth_leaves', 0);
            addParameter(p, 'min_uniform_depth',      0);
            parse(p, f, a, b, tol, varargin{:});
            r = p.Results;

            a = double(r.a(:)');
            b = double(r.b(:)');

            % Infer out_dim by probing f once at the box midpoint, unless the
            % caller gave it explicitly. (Octave-safe: scan the Name/Value names
            % rather than relying on inputParser's UsingDefaults.)
            if any(strcmpi(varargin(1:2:end), 'out_dim'))
                out_dim = r.out_dim;
            else
                probe   = r.f((a + b) / 2);   % f takes a 1×dim row
                out_dim = numel(probe);
            end

            % The generated gateway reads scalars as mxDOUBLE_CLASS, so pass
            % every numeric option as a plain double (no int32 casts).
            obj.mwptr = tw_fit(r.f, a, b, double(r.tol), ...
                double(r.dim), double(out_dim), ...
                double(r.tol_kind), ...
                double(r.max_depth), double(r.max_memory_mib), ...
                double(r.allow_max_depth_leaves), double(r.min_uniform_depth));

            obj.input_dim_  = r.dim;
            obj.output_dim_ = out_dim;
        end

        function y = eval(obj, X, varargin)
        % EVAL  Evaluate at a point or a batch.
        %   y = eval(obj, X)                  X: N×dim, y: N×out_dim (point: 1×dim)
        %   y = eval(obj, X, 'sorted', true)  1-D ascending fast path (dim==1)
        %   y = eval(obj, X, 'transposed', true)  y: out_dim×N (out_dim>1)
            p = inputParser;
            addRequired(p, 'X');
            addParameter(p, 'sorted', false);
            addParameter(p, 'transposed', false);
            parse(p, X, varargin{:});
            do_sorted     = logical(p.Results.sorted);
            do_transposed = logical(p.Results.transposed);

            if do_sorted && do_transposed
                error('treeweave:eval', '''sorted'' and ''transposed'' are mutually exclusive.');
            end

            dim = obj.input_dim_;
            od  = obj.output_dim_;
            X   = double(X);

            % Validate shape and normalize to N×dim (column for dim==1).
            if dim == 1
                if ~isvector(X)
                    error('treeweave:eval', 'for dim == 1, X must be a vector; got size [%s].', num2str(size(X)));
                end
                X = X(:);              % N×1
                N = numel(X);
            else
                if ~ismatrix(X) || size(X, 2) ~= dim
                    error('treeweave:eval', 'a point/batch must have %d columns (dim); got size [%s].', ...
                          dim, num2str(size(X)));
                end
                N = size(X, 1);
            end

            if do_sorted
                if dim ~= 1
                    error('treeweave:eval', '''sorted'' requires dim == 1 (got %d).', dim);
                end
                % tw_eval_sorted returns Yflat as out_dim×N.
                Yflat = tw_eval_sorted(obj, X, od, N);
                y = Yflat';                       % N×out_dim
                return;
            end

            if do_transposed
                if od == 1
                    error('treeweave:eval', '''transposed'' requires out_dim > 1.');
                end
                Xflat = X';                        % dim×N, point-major
                % tw_eval_soa (the treeweave_transposed C path) returns N×out_dim
                % with column d = component d; transpose for the SoA layout.
                Ysoa = tw_eval_soa(obj, Xflat(:), od, N);
                y = Ysoa';                         % out_dim×N
                return;
            end

            if N == 1
                % Single-point fast path. y comes back out_dim×1.
                y = tw_eval1(obj, X(1, :), od);
                y = reshape(y, 1, od);
            else
                % Batch: X is N×dim (row-per-point). Transpose to dim×N so
                % column-major storage = AoS point-major.
                Xflat = X';                        % dim×N, point-major
                % tw_eval_multi returns Yflat as out_dim×N.
                Yflat = tw_eval_multi(obj, Xflat(:), od, N);
                y = Yflat';                        % N×out_dim
            end
        end

        function varargout = subsref(obj, S)
            if numel(S) == 1 && strcmp(S.type, '()')
                % Guard against obj('sorted', true) / obj('transposed', true)
                % with no leading X argument — gives a confusing error without this.
                subs = S.subs;
                if ~isempty(subs) && ischar(subs{1})
                    error('treeweave:subsref', ...
                        ['obj(''%s'', ...) is missing the point/batch argument X. ' ...
                         'Use obj(X, ''%s'', true) instead.'], subs{1}, subs{1});
                end
                varargout{1} = obj.eval(subs{:});
            else
                [varargout{1:nargout}] = builtin('subsref', obj, S);
            end
        end

        function bytes = memory_usage(obj)
            bytes = tw_memory(obj);
        end

        function print_stats(obj)
            tw_stats(obj);
        end

        function d = input_dim(obj)
            d = obj.input_dim_;
        end

        function d = output_dim(obj)
            d = obj.output_dim_;
        end

        function delete(obj)
            if ~isempty(obj.mwptr)
                tw_free(obj);
                obj.mwptr = [];
            end
        end

    end
end
