// zeta_bench.mjs: treeweave vs a fair brute-force Riemann-zeta eval.
// See benchmarks/zeta_bench.cpp for the rationale. ζ(s) = Σ_k k^-s summed until
// the tail is negligible (rel 1e-10, ≤160 terms) yet smooth on [2,10]: fit once.
// Uses the NATIVE .node backend (the WASM path is covered by the tests). Times
// single/multi/sorted; the native rate is sampled over nNative and reused.
// TREEWEAVE_BENCH_YAML=path emits YAML (toExponential(17) => floats carry a '.').

import { writeFileSync } from "node:fs";

import { Treeweave } from "../dist/index.js";

const a = 2.0;
const b = 10.0;
const tol = 1e-10;

// Fair baseline: sum k^-s until a term is below EPS relative to the running
// total, capped at MAX_TERMS: a competent zeta stops early.
const EPS = 1e-10;
const MAX_TERMS = 160;

// ζ(s) ≈ Σ_k k^-s (early stop). The fit callback and the native baseline are
// the same function (apples-to-apples).
function zetaPartial(s) {
    let acc = 0;
    for (let k = 1; k <= MAX_TERMS; ++k) {
        const term = Math.pow(k, -s);
        acc += term;
        if (term < EPS * acc) break;
    }
    return acc;
}

const fn = await Treeweave.fit((x) => zetaPartial(x[0]), a, b, tol, { backend: "native" });

const n = 1_000_000; // batch / sorted points
const nScalar = 100_000; // scalar-API points
const nNative = 256; // brute-force sample (<=160 pows each)

// Deterministic pseudo-random points in [a, b) (a small LCG; no deps).
const xs = new Float64Array(n);
let seed = 7;
for (let i = 0; i < n; ++i) {
    seed = (1103515245 * seed + 12345) & 0x7fffffff;
    xs[i] = a + (seed / 0x7fffffff) * (b - a);
}
const xsSorted = Float64Array.from(xs).sort();

// --- accuracy vs the brute-force sum, on the n_native sample -----------------
let maxRel = 0;
for (let i = 0; i < nNative; ++i) {
    const ref = zetaPartial(xs[i]);
    maxRel = Math.max(maxRel, Math.abs(fn.eval(xs[i]) - ref) / Math.abs(ref));
}

const mevals = (count, seconds) => count / (seconds * 1e6);
const now = () => performance.now() / 1000;

// --- native rate: brute-force sum over the small sample (mode-independent) ---
let sink = 0;
for (let i = 0; i < nNative; ++i) sink += zetaPartial(xs[i]); // warm-up
let t0 = now();
for (let i = 0; i < nNative; ++i) sink += zetaPartial(xs[i]);
const natS = now() - t0;
if (!Number.isFinite(sink)) throw new Error("sink went non-finite");
const natRate = mevals(nNative, natS); // Mevals/s, reused in every mode

// --- single-eval -------------------------------------------------------------
for (let i = 0; i < nScalar; ++i) sink += fn.eval(xs[i]); // warm-up
t0 = now();
for (let i = 0; i < nScalar; ++i) sink += fn.eval(xs[i]);
const twSingleS = now() - t0;
if (!Number.isFinite(sink)) throw new Error("sink went non-finite");

// --- multi-eval (in place) ---------------------------------------------------
const twBuf = new Float64Array(n);
fn.batch(xs, { out: twBuf }); // warm-up
t0 = now();
fn.batch(xs, { out: twBuf });
const twMultiS = now() - t0;

// --- sorted-eval -------------------------------------------------------------
fn.sorted(xsSorted, { out: twBuf }); // warm-up
t0 = now();
fn.sorted(xsSorted, { out: twBuf });
const twSortedS = now() - t0;

// --- throughput (Mevals/s) and speedup per mode ------------------------------
const twSingle = mevals(nScalar, twSingleS);
const twMulti = mevals(n, twMultiS);
const twSorted = mevals(n, twSortedS);

console.log(
    `zeta(s) = sum_k k^-s (<=${MAX_TERMS} terms, stop at ${EPS} rel), fit on [${a}, ${b}], relative tol ${tol}`,
);
console.log(`  max rel err: ${maxRel.toExponential(3)}`);
console.log(
    `  single-eval  treeweave ${twSingle.toFixed(1)}  native ${natRate.toFixed(4)} Mevals/s  speedup ${(twSingle / natRate).toFixed(1)}x`,
);
console.log(
    `  multi-eval   treeweave ${twMulti.toFixed(1)}  native ${natRate.toFixed(4)} Mevals/s  speedup ${(twMulti / natRate).toFixed(1)}x`,
);
console.log(
    `  sorted-eval  treeweave ${twSorted.toFixed(1)}  native ${natRate.toFixed(4)} Mevals/s  speedup ${(twSorted / natRate).toFixed(1)}x`,
);

// --- machine-readable YAML (optional) ----------------------------------------
const yamlPath = process.env.TREEWEAVE_BENCH_YAML;
if (yamlPath) {
    const e = (x) => x.toExponential(17); // always carries a '.', so YAML reads a float
    const block = (tw, nat) =>
        `  treeweave_mevals_s: ${e(tw)}\n  native_mevals_s: ${e(nat)}\n  speedup: ${e(tw / nat)}\n`;
    const doc =
        `language: "js"\n` +
        `domain: [${e(a)}, ${e(b)}]\n` +
        `tol: ${e(tol)}\n` +
        `n_pts: ${n}\n` +
        `max_rel_err: ${e(maxRel)}\n` +
        `single_eval:\n${block(twSingle, natRate)}` +
        `multi_eval:\n${block(twMulti, natRate)}` +
        `sorted_eval:\n${block(twSorted, natRate)}`;
    writeFileSync(yamlPath, doc);
}

fn.free();
