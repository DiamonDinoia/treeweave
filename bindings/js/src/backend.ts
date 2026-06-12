// backend.ts — the backend-agnostic contract plus runtime backend selection.
//
// treeweave's JS binding has two interchangeable backends over the same C ABI:
//   * "native": a Node-API `.node` addon (fast, Node-only) — treeweave_napi.cpp
//   * "wasm":   an Emscripten module (browser + Node) — wasm.ts / wasm_glue.cpp
// Both implement the `Backend` interface below; `index.ts` (the Treeweave
// class) is written entirely against this interface and never imports a backend
// directly. `loadBackend()` chooses one at runtime.

export type DType = "f64" | "f32";
export type FloatArray = Float64Array | Float32Array;

/** Numeric `tol_kind` values — mirror treeweave_tol_kind_t in treeweave.h. */
export const TOL_KIND = {
  relative_tail: 0,
  absolute_tail: 1,
  relative_max: 2,
  absolute_max: 3,
  relative_l2: 4,
  absolute_l2: 5,
} as const;
export type TolKind = keyof typeof TOL_KIND;

/** Fully-resolved fit request handed to a backend (defaults already applied). */
export interface FitRequest {
  callback: (x: FloatArray) => number | FloatArray | number[];
  inputDim: number;
  outputDim: number;
  a: number[];
  b: number[];
  tol: number;
  tolKind: number;
  maxDepth: number;
  maxMemoryMib: number;
  allowMaxDepthLeaves: number;
  minUniformDepth: number;
  dtype: DType;
}

/** A fitted function. Eval inputs/outputs are typed arrays of the fit's dtype. */
export interface BackendFunction {
  readonly inputDim: number;
  readonly outputDim: number;
  readonly dtype: DType;
  evalOne(x: FloatArray): number | FloatArray;
  batch(x: FloatArray, out?: FloatArray): FloatArray;
  sorted(x: FloatArray, out?: FloatArray): FloatArray;
  transposed(x: FloatArray): FloatArray[];
  memoryUsage(): number;
  printStats(): void;
  free(): void;
}

export interface Backend {
  readonly name: "native" | "wasm";
  readonly versionString: string;
  fit(req: FitRequest): BackendFunction;
}

export type BackendChoice = "auto" | "native" | "wasm";

/**
 * Load a backend. `"auto"` (the default) prefers the native addon under Node and
 * falls back to WASM if the addon is unavailable; in non-Node environments it
 * always uses WASM. `"native"` / `"wasm"` force a specific backend (used by the
 * test suite to exercise both under Node).
 */
export async function loadBackend(choice: BackendChoice = "auto"): Promise<Backend> {
  const isNode =
    typeof process !== "undefined" && !!(process as { versions?: { node?: string } }).versions?.node;

  if (choice === "native" || (choice === "auto" && isNode)) {
    try {
      const { makeNativeBackend } = await import("./native.js");
      return makeNativeBackend();
    } catch (err) {
      if (choice === "native") throw err;
      // fall through to WASM
    }
  }

  const { makeWasmBackend } = await import("./wasm.js");
  return makeWasmBackend();
}
