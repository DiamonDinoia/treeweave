// wasm.ts — the "wasm" backend: the treeweave C ABI compiled to a single
// WASM module (wasm_glue.cpp + treeweave_c_static under emcc), driven through
// the Emscripten runtime. Works in the browser and under Node.
//
// All eval paths marshal across the WASM heap: copy the input in, call the C
// function, copy the result out (always before freeing — heap views are
// invalidated by _free and by any growth). The fit callback is a JS trampoline
// installed in the function table via addFunction(fn, 'viii') — pointers are
// i32 in wasm32, so the C signature void(const T*, T*, void*) is 'viii' for
// both dtypes.

import type { Backend, BackendFunction, DType, FitRequest, FloatArray } from "./backend.js";

type CFn = (...args: number[]) => number;

interface WasmModule {
  _malloc: CFn;
  _free: (ptr: number) => void;
  HEAPF64: Float64Array;
  HEAPF32: Float32Array;
  HEAP32: Int32Array;
  HEAPU8: Uint8Array;
  addFunction(fn: (...args: number[]) => number | void, signature: string): number;
  removeFunction(ptr: number): void;
  ccall(name: string, returnType: string | null, argTypes: string[], args: unknown[]): unknown;
  // C ABI entry points (_treeweave_*, _treeweavef_*) and live heap getters.
  [key: string]: unknown;
}

class WasmFunction implements BackendFunction {
  constructor(
    private readonly M: WasmModule,
    private handle: number,
    readonly inputDim: number,
    readonly outputDim: number,
    readonly dtype: DType,
  ) {}

  private get elt(): number {
    return this.dtype === "f32" ? 4 : 8;
  }
  private heap(): FloatArray {
    return this.dtype === "f32" ? this.M.HEAPF32 : this.M.HEAPF64;
  }
  // C entry point for `base`, dtype-prefixed: e.g. 'eval' -> _treeweave_eval / _treeweavef_eval.
  private cf(base: string): CFn {
    const name = this.dtype === "f32" ? `_treeweavef_${base}` : `_treeweave_${base}`;
    return this.M[name] as CFn;
  }
  private alloc(values: FloatArray): number {
    const ptr = this.M._malloc(values.length * this.elt);
    this.heap().set(values, ptr / this.elt); // re-reads heap (may have grown)
    return ptr;
  }

  evalOne(x: FloatArray): number | FloatArray {
    const M = this.M;
    const xPtr = this.alloc(x.subarray(0, this.inputDim) as FloatArray);
    const yPtr = M._malloc(this.outputDim * this.elt);
    this.cf("eval")(this.handle, xPtr, yPtr);
    const h = this.heap();
    const yi = yPtr / this.elt;
    let result: number | FloatArray;
    if (this.outputDim === 1) {
      result = h[yi];
    } else {
      result = h.slice(yi, yi + this.outputDim);
    }
    M._free(xPtr);
    M._free(yPtr);
    return result;
  }

  private evalBatch(base: "batch" | "sorted", x: FloatArray, out?: FloatArray): FloatArray {
    const M = this.M;
    const n = x.length / this.inputDim;
    const total = n * this.outputDim;
    if (out && out.length !== total) {
      throw new Error("out has the wrong number of elements for this batch");
    }
    const xPtr = this.alloc(x);
    const resPtr = M._malloc(total * this.elt);
    this.cf(base)(this.handle, xPtr, resPtr, n);
    const h = this.heap();
    const view = h.subarray(resPtr / this.elt, resPtr / this.elt + total);
    const result = out ?? (this.dtype === "f32" ? new Float32Array(total) : new Float64Array(total));
    result.set(view); // copy out of the heap before freeing
    M._free(xPtr);
    M._free(resPtr);
    return result;
  }

  batch(x: FloatArray, out?: FloatArray): FloatArray {
    return this.evalBatch("batch", x, out);
  }
  sorted(x: FloatArray, out?: FloatArray): FloatArray {
    return this.evalBatch("sorted", x, out);
  }

  transposed(x: FloatArray): FloatArray[] {
    const M = this.M;
    const od = this.outputDim;
    const n = x.length / this.inputDim;
    const xPtr = this.alloc(x);
    const compPtrs: number[] = [];
    for (let d = 0; d < od; ++d) compPtrs.push(M._malloc(n * this.elt));
    const soaPtr = M._malloc(od * 4); // wasm32: pointer == i32
    M.HEAP32.set(Int32Array.from(compPtrs), soaPtr / 4);
    this.cf("transposed")(this.handle, xPtr, soaPtr, n);
    const h = this.heap();
    const out: FloatArray[] = [];
    for (let d = 0; d < od; ++d) {
      const start = compPtrs[d] / this.elt;
      out.push(h.slice(start, start + n));
    }
    M._free(xPtr);
    M._free(soaPtr);
    for (const p of compPtrs) M._free(p);
    return out;
  }

  memoryUsage(): number {
    return (this.M._treeweave_memory_usage as CFn)(this.handle);
  }
  printStats(): void {
    (this.M._treeweave_print_stats as CFn)(this.handle);
  }
  free(): void {
    if (this.handle) {
      (this.M._treeweave_free as CFn)(this.handle);
      this.handle = 0;
    }
  }
}

function fit(M: WasmModule, req: FitRequest): BackendFunction {
  const f32 = req.dtype === "f32";
  const elt = f32 ? 4 : 8;
  const heap = (): FloatArray => (f32 ? M.HEAPF32 : M.HEAPF64);
  let cbError: unknown = null;

  const trampoline = (xPtr: number, yPtr: number, _ctx: number): void => {
    const yi = yPtr / elt;
    if (cbError !== null) {
      const h = heap();
      for (let i = 0; i < req.outputDim; ++i) h[yi + i] = NaN;
      return;
    }
    try {
      const xi = xPtr / elt;
      const xv = heap().subarray(xi, xi + req.inputDim);
      const r = req.callback(xv);
      const h = heap(); // re-read: the callback may have grown the heap
      if (req.outputDim === 1) {
        h[yi] = typeof r === "number" ? r : (r as ArrayLike<number>)[0];
      } else {
        const rr = r as ArrayLike<number>;
        for (let i = 0; i < req.outputDim; ++i) h[yi + i] = rr[i];
      }
    } catch (e) {
      cbError = e;
      const h = heap();
      for (let i = 0; i < req.outputDim; ++i) h[yi + i] = NaN;
    }
  };

  const fnPtr = M.addFunction(trampoline, "viii");
  const aArr = f32 ? Float32Array.from(req.a) : Float64Array.from(req.a);
  const bArr = f32 ? Float32Array.from(req.b) : Float64Array.from(req.b);
  const aPtr = M._malloc(aArr.length * elt);
  const bPtr = M._malloc(bArr.length * elt);
  heap().set(aArr, aPtr / elt);
  heap().set(bArr, bPtr / elt);
  const optsPtr = M._malloc(20); // 5 * int32
  M.HEAP32.set(
    Int32Array.of(req.tolKind, req.maxDepth, req.maxMemoryMib, req.allowMaxDepthLeaves, req.minUniformDepth),
    optsPtr / 4,
  );

  const fitFn = (f32 ? M._treeweavef_fit : M._treeweave_fit) as CFn;
  const handle = fitFn(fnPtr, req.inputDim, req.outputDim, aPtr, bPtr, req.tol, 0, optsPtr);

  M._free(aPtr);
  M._free(bPtr);
  M._free(optsPtr);
  M.removeFunction(fnPtr);

  if (cbError !== null) throw cbError;
  if (handle === 0) {
    const msg = M.ccall("treeweave_last_error", "string", [], []) as string;
    throw new Error(msg || "treeweave_fit returned NULL");
  }
  return new WasmFunction(M, handle, req.inputDim, req.outputDim, req.dtype);
}

export async function makeWasmBackend(): Promise<Backend> {
  // The .mjs glue is generated by emcc next to the compiled JS (dist/). Build a
  // runtime URL so tsc does not try to resolve the not-yet-existing module.
  const url = new URL("./treeweave.mjs", import.meta.url).href;
  const mod = (await import(url)) as { default: (opts?: unknown) => Promise<WasmModule> };
  const M = await mod.default();
  const versionString = M.ccall("treeweave_version_string", "string", [], []) as string;
  return {
    name: "wasm",
    versionString,
    fit: (req: FitRequest) => fit(M, req),
  };
}
