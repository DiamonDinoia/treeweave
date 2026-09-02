// simple_1d.mjs: minimal 1D fit and evaluation (smoke test).
// Fits Math.sin on [0, 1], evals a single point and a batch, checks accuracy.
// Exits nonzero if max abs error > 1e-6.

// BEGIN DOCS_USAGE
// In an installed package this import is "@flatironinstitute/treeweave".
import { Treeweave } from "../dist/index.js";

// Fit sin(x) on [0, 1] syntax is fit(callback, lower_bound, upper_bound, tolerance, options).
const fn = await Treeweave.fit((x) => Math.sin(x[0]), 0.0, 1.0, 1e-10, {
    backend: "native",
});

// Evaluate fn on (0.5) and print the result.
const single = fn.eval(0.5);
console.log(`sin(0.5) approx=${single.toFixed(12)}`);
// END DOCS_USAGE

const singleExact = Math.sin(0.5);
console.log(`sin(0.5) exact=${singleExact.toFixed(12)}`);

// Evaluate fn on 11 points and print the maximum error.
const xs = new Float64Array(11);
for (let i = 0; i < 11; ++i) xs[i] = i / 10;
const ys = fn.batch(xs);
let maxErr = 0;
for (let i = 0; i < 11; ++i) maxErr = Math.max(maxErr, Math.abs(ys[i] - Math.sin(xs[i])));
console.log(`max |approx - sin| over 11 points: ${maxErr.toExponential(3)}`);

fn.free();

// Every fit option rides in the trailing object, in lower camel case.
// BEGIN DOCS_OPTIONS
const tuned = await Treeweave.fit((x) => Math.sin(x[0]), 0.0, 10.0, 1e-10, {
    tolKind: "absolute_max",
    maxDepth: 30,
    maxMemoryMib: 64,
    backend: "native",
});
// END DOCS_OPTIONS
const tunedErr = Math.abs(tuned.eval(3.5) - Math.sin(3.5));
console.log(`absolute-max fit at x=3.5: |approx - sin| = ${tunedErr.toExponential(3)}`);
tuned.free();

if (maxErr > 1e-6 || tunedErr > 1e-6) {
    console.error(`error too large: ${Math.max(maxErr, tunedErr)}`);
    process.exit(1);
}
console.log("OK");
