// native.ts — the "native" backend: a thin TypeScript wrapper over the
// Node-API addon (treeweave.node, built from treeweave_napi.cpp). The addon
// already returns objects shaped like BackendFunction, so this mostly marshals
// the fit request (typed a/b arrays + the packed opts) and casts the result.

import { createRequire } from "node:module";

import type { Backend, BackendFunction, FitRequest } from "./backend.js";

interface Addon {
  fit(
    callback: FitRequest["callback"],
    inputDim: number,
    outputDim: number,
    a: Float64Array | Float32Array,
    b: Float64Array | Float32Array,
    tol: number,
    opts: Int32Array,
    dtype: string,
  ): BackendFunction;
  versionString: string;
  version: number;
}

export function makeNativeBackend(): Backend {
  const require = createRequire(import.meta.url);
  // Built next to the compiled JS (dist/treeweave.node) by CMake.
  const addon = require("./treeweave.node") as Addon;

  return {
    name: "native",
    versionString: addon.versionString,
    fit(req: FitRequest): BackendFunction {
      const a = req.dtype === "f32" ? Float32Array.from(req.a) : Float64Array.from(req.a);
      const b = req.dtype === "f32" ? Float32Array.from(req.b) : Float64Array.from(req.b);
      const opts = Int32Array.of(
        req.tolKind,
        req.maxDepth,
        req.maxMemoryMib,
        req.allowMaxDepthLeaves,
        req.minUniformDepth,
      );
      return addon.fit(req.callback, req.inputDim, req.outputDim, a, b, req.tol, opts, req.dtype);
    },
  };
}
