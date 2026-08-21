// native.ts: the "native" backend, a thin TypeScript wrapper over the
// Node-API addon (treeweave.node, built from treeweave_napi.cpp). The addon
// already returns objects shaped like BackendFunction, so this mostly marshals
// the fit request (typed a/b arrays + the packed opts) and casts the result.

import { createRequire } from "node:module";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

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

// Resolve the N-API addon. Published packages carry per-platform prebuilt
// binaries under prebuilds/<platform>-<arch>/ (node-gyp-build picks the one
// matching the running Node/OS/arch, N-API is ABI-stable, so one binary per
// platform serves every Node version). A local CMake build instead drops
// treeweave.node next to this file in dist/, so fall back to that.
function loadAddon(require: NodeRequire): Addon {
    const here = dirname(fileURLToPath(import.meta.url));
    try {
        const gypBuild = require("node-gyp-build") as (dir: string) => Addon;
        return gypBuild(join(here, "..")); // package root holds prebuilds/
    } catch {
        return require("./treeweave.node") as Addon;
    }
}

export function makeNativeBackend(): Backend {
    const require = createRequire(import.meta.url);
    const addon = loadAddon(require);

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
            return addon.fit(
                req.callback,
                req.inputDim,
                req.outputDim,
                a,
                b,
                req.tol,
                opts,
                req.dtype,
            );
        },
    };
}
