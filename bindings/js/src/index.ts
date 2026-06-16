// index.ts — the public treeweave JS/TS API.
//
// `Treeweave.fit(f, a, b, tol, opts?)` fits a JS callback and resolves to a
// callable evaluator, mirroring the Python/Julia ergonomics. The backend
// (native `.node` addon or WASM) is chosen at runtime by backend.ts and is
// invisible here. Eval inputs/outputs are flat typed arrays of the fit's dtype;
// batches are row-major ((N, dim) flattened, or (N,) for dim == 1).

import {
    type Backend,
    type BackendChoice,
    type BackendFunction,
    type DType,
    type FloatArray,
    TOL_KIND,
    type TolKind,
    loadBackend,
} from "./backend.js";

export { loadBackend, TOL_KIND };
export type { BackendChoice, DType, TolKind };

export interface FitOptions {
    /** Input dimension; inferred from `a`/`b` length when omitted. */
    dim?: number;
    /** Output dimension; inferred by probing `f` at the box midpoint when omitted. */
    outDim?: number;
    dtype?: DType;
    tolKind?: TolKind;
    maxDepth?: number;
    /** MiB; -1 auto-selects a dimension-scaled budget, 0 disables the cap. */
    maxMemoryMib?: number;
    allowMaxDepthLeaves?: boolean;
    minUniformDepth?: number;
    /** Force a backend; defaults to "auto" (native under Node, else WASM). */
    backend?: BackendChoice;
}

// One backend instance per choice, reused across fits (WASM instantiation is
// not free; the native addon is a singleton anyway).
const backendCache = new Map<BackendChoice, Promise<Backend>>();
function getBackend(choice: BackendChoice): Promise<Backend> {
    let cached = backendCache.get(choice);
    if (!cached) {
        cached = loadBackend(choice);
        backendCache.set(choice, cached);
    }
    return cached;
}

function asArray(v: number | number[]): number[] {
    return Array.isArray(v) ? v : [v];
}

export class Treeweave {
    private constructor(
        private readonly fn: BackendFunction,
        readonly libVersion: string,
    ) {}

    get dim(): number {
        return this.fn.inputDim;
    }
    get outDim(): number {
        return this.fn.outputDim;
    }
    get dtype(): DType {
        return this.fn.dtype;
    }

    /** Fit `f` over the box `[a, b)` to tolerance `tol`. */
    static async fit(
        f: (x: FloatArray) => number | FloatArray | number[],
        a: number | number[],
        b: number | number[],
        tol: number,
        opts: FitOptions = {},
    ): Promise<Treeweave> {
        const aArr = asArray(a);
        const bArr = asArray(b);
        if (aArr.length !== bArr.length) throw new Error("a and b must have the same length");

        const dim = opts.dim ?? aArr.length;
        if (dim !== aArr.length)
            throw new Error(`dim=${dim} but len(a)=${aArr.length}; they must agree`);

        const dtype: DType = opts.dtype ?? "f64";
        const make =
            dtype === "f32"
                ? Float32Array.from.bind(Float32Array)
                : Float64Array.from.bind(Float64Array);

        // Infer outDim by probing f once at the box midpoint, like the Python binding.
        let outDim = opts.outDim;
        if (outDim === undefined) {
            const mid = make(aArr.map((av, i) => (av + bArr[i]) * 0.5));
            const probe = f(mid);
            outDim = typeof probe === "number" ? 1 : (probe as ArrayLike<number>).length;
        }
        if (dim < 1 || dim > 3) throw new Error(`dim must be 1-3; got ${dim}`);
        if (outDim < 1 || outDim > 3) throw new Error(`outDim must be 1-3; got ${outDim}`);

        const tolKind = TOL_KIND[opts.tolKind ?? "relative_max"];

        const backend = await getBackend(opts.backend ?? "auto");
        const fn = backend.fit({
            callback: f,
            inputDim: dim,
            outputDim: outDim,
            a: aArr,
            b: bArr,
            tol,
            tolKind,
            maxDepth: opts.maxDepth ?? 50,
            maxMemoryMib: opts.maxMemoryMib ?? -1,
            allowMaxDepthLeaves: opts.allowMaxDepthLeaves ? 1 : 0,
            minUniformDepth: opts.minUniformDepth ?? 0,
            dtype,
        });
        return new Treeweave(fn, backend.versionString);
    }

    // ---- evaluation -------------------------------------------------------

    /** Evaluate a single point. Returns a scalar (outDim == 1) or a typed array. */
    eval(x: number | number[] | FloatArray): number | FloatArray {
        return this.fn.evalOne(this.toInput(x, this.dim));
    }

    /** Unsorted batch over `x` (row-major; (N,) for dim == 1). */
    batch(x: number[] | FloatArray, opts: { out?: FloatArray } = {}): FloatArray {
        return this.fn.batch(this.toInput(x), opts.out);
    }

    /** 1-D sorted fast path (dim must be 1; caller promises ascending x). */
    sorted(x: number[] | FloatArray, opts: { out?: FloatArray } = {}): FloatArray {
        if (this.dim !== 1)
            throw new Error(`sorted requires dim == 1; this fit has dim=${this.dim}`);
        return this.fn.sorted(this.toInput(x), opts.out);
    }

    /** Struct-of-arrays batch: returns outDim arrays of length N (outDim must be > 1). */
    transposed(x: number[] | FloatArray): FloatArray[] {
        if (this.outDim < 2) throw new Error("transposed requires outDim > 1");
        return this.fn.transposed(this.toInput(x));
    }

    memoryUsage(): number {
        return this.fn.memoryUsage();
    }
    printStats(): void {
        this.fn.printStats();
    }
    free(): void {
        this.fn.free();
    }
    [Symbol.dispose](): void {
        this.free();
    }

    // ---- internal ---------------------------------------------------------

    /** Coerce input to a contiguous typed array of this fit's dtype. */
    private toInput(x: number | number[] | FloatArray, expectLen?: number): FloatArray {
        const wantF32 = this.dtype === "f32";
        let arr: FloatArray;
        if (typeof x === "number") {
            arr = wantF32 ? Float32Array.of(x) : Float64Array.of(x);
        } else if (x instanceof Float64Array) {
            arr = wantF32 ? Float32Array.from(x) : x;
        } else if (x instanceof Float32Array) {
            arr = wantF32 ? x : Float64Array.from(x);
        } else {
            arr = wantF32 ? Float32Array.from(x) : Float64Array.from(x);
        }
        if (expectLen !== undefined && arr.length !== expectLen) {
            throw new Error(`point has length ${arr.length} but dim == ${expectLen}`);
        }
        return arr;
    }
}

export default Treeweave;
