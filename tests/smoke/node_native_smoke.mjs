// Build-time smoke for a freshly built native N-API addon (treeweave.node):
// load the .node directly, fit x^2 on [0, 2), eval at 1.5, assert ~= 2.25.
// Confirms the binary loads and computes on its target platform before it is
// bundled into the npm package. Invoked by .github/workflows/_build-node-prebuilds.yml
// as `node node_native_smoke.mjs <path-to-addon.node>`.
//
// This pokes the raw addon ABI (the same call native.ts makes): the opts array
// is [tolKind, maxDepth, maxMemoryMib, allowMaxDepthLeaves, minUniformDepth].
import assert from "node:assert/strict";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);
const addonPath = process.argv[2];
assert.ok(addonPath, "usage: node node_native_smoke.mjs <path-to-addon.node>");

const addon = require(addonPath);
const opts = Int32Array.of(/*relative_max*/ 2, /*maxDepth*/ 50, /*maxMemoryMib*/ -1, 0, 0);
const fn = addon.fit(
    (x) => x[0] * x[0],
    1,
    1,
    Float64Array.of(0.0),
    Float64Array.of(2.0),
    1e-8,
    opts,
    "f64",
);
const y = fn.evalOne(Float64Array.of(1.5));
fn.free();

assert.ok(Math.abs(y - 2.25) < 1e-6, `bad eval: ${y}`);
console.log(`OK native addon ${addonPath}: version ${addon.versionString}, eval(1.5) = ${y}`);
