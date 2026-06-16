// Post-install smoke for the published @flatironinstitute/treeweave npm
// package (WASM-only, so it runs on any OS with no native toolchain): dynamic
// import, fit x^2 on [0, 2), eval at 1.5, assert ~= 2.25. Invoked by
// .github/workflows/release-install.yml via `node npm_smoke.mjs`.
import assert from "node:assert/strict";

const { Treeweave } = await import("@flatironinstitute/treeweave");

const approx = await Treeweave.fit((x) => x[0] * x[0], 0.0, 2.0, 1e-8);
const y = approx.eval(1.5);
approx.free();

assert.ok(Math.abs(y - 2.25) < 1e-6, `bad eval: ${y}`);
console.log(`OK: treeweave(x^2)(1.5) = ${y}`);
