// Smoke for the raw standalone WASM assets (treeweave.mjs + treeweave.wasm),
// the emscripten module attached to the GitHub Release for direct web/Node use,
// independent of the npm package's TypeScript wrapper. Loads the module, fits
// x^2 on [0, 2) and evals at 1.5 (~= 2.25), driving the C ABI through the
// Emscripten runtime exactly as wasm.ts does (heap marshaling + an addFunction
// trampoline). Invoked as `node wasm_smoke.mjs <path-to-treeweave.mjs>`.
import assert from "node:assert/strict";
import { pathToFileURL } from "node:url";

const mjsPath = process.argv[2];
assert.ok(mjsPath, "usage: node wasm_smoke.mjs <path-to-treeweave.mjs>");

const mod = await import(pathToFileURL(mjsPath).href);
const M = await mod.default();

const version = M.ccall("treeweave_version_string", "string", [], []);

// f64 path: 8-byte elements; pointers are i32 in wasm32. Re-read HEAPF64 after
// every _malloc: growth detaches the old view.
const ELT = 8;
const f = (x) => x[0] * x[0];

const trampoline = (xPtr, yPtr) => {
    const x = M.HEAPF64.subarray(xPtr / ELT, xPtr / ELT + 1);
    M.HEAPF64[yPtr / ELT] = f(x);
};
const fnPtr = M.addFunction(trampoline, "viii");

const aPtr = M._malloc(ELT);
const bPtr = M._malloc(ELT);
M.HEAPF64[aPtr / ELT] = 0.0;
M.HEAPF64[bPtr / ELT] = 2.0;
// opts: [tolKind, maxDepth, maxMemoryMib, allowMaxDepthLeaves, minUniformDepth]
// Same as the native smoke (relative_max tol, depth 50, no memory cap).
const optsPtr = M._malloc(20);
M.HEAP32.set(Int32Array.of(2, 50, -1, 0, 0), optsPtr / 4);

const handle = M._treeweave_fit(fnPtr, 1, 1, aPtr, bPtr, 1e-8, 0, optsPtr);
M._free(aPtr);
M._free(bPtr);
M._free(optsPtr);
M.removeFunction(fnPtr);
assert.notEqual(
    handle,
    0,
    M.ccall("treeweave_last_error", "string", [], []) || "fit returned NULL",
);

const xPtr = M._malloc(ELT);
const yPtr = M._malloc(ELT);
M.HEAPF64[xPtr / ELT] = 1.5;
M._treeweave_eval(handle, xPtr, yPtr);
const y = M.HEAPF64[yPtr / ELT];
M._free(xPtr);
M._free(yPtr);
M._treeweave_free(handle);

assert.ok(Math.abs(y - 2.25) < 1e-6, `bad eval: ${y}`);
console.log(`OK raw WASM ${mjsPath}: version ${version}, eval(1.5) = ${y}`);
