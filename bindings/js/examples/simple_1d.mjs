// simple_1d.mjs — minimal 1D fit and evaluation (smoke test).
// Fits Math.sin on [0, 1], evals a single point and a batch, checks accuracy.
// Exits nonzero if max abs error > 1e-6.

import { Treeweave } from "../dist/index.js";

const fn = await Treeweave.fit((x) => Math.sin(x[0]), 0.0, 1.0, 1e-10, {
    backend: "native",
});

// Single-point eval.
const single = fn.eval(0.5);
const singleExact = Math.sin(0.5);
console.log(`sin(0.5) approx=${single.toFixed(12)} exact=${singleExact.toFixed(12)}`);

// Batch eval over 11 points.
const xs = new Float64Array(11);
for (let i = 0; i < 11; ++i) xs[i] = i / 10;
const ys = fn.batch(xs);
let maxErr = 0;
for (let i = 0; i < 11; ++i) maxErr = Math.max(maxErr, Math.abs(ys[i] - Math.sin(xs[i])));
console.log(`max |approx - sin| over 11 points: ${maxErr.toExponential(3)}`);

fn.free();

if (maxErr > 1e-6) {
    console.error(`error too large: ${maxErr}`);
    process.exit(1);
}
console.log("OK");
