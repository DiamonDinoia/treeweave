// treeweave.test.mjs: node:test suite, run by CTest as `js_treeweave`.
//
// Exercises BOTH backends under Node: the native .node addon (built by the
// bindings-js preset) and the WASM module (built by bindings-js-wasm). The WASM
// block is skipped when its artifacts are absent (e.g. a native-only local build
// without emsdk).

import assert from "node:assert/strict";
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { after, describe, test } from "node:test";

import { Treeweave } from "../dist/index.js";

const A = 2.0;
const B = 10.0;
const TOL = 1e-10;

// Brute-force Riemann-zeta partial sum, the gold-standard reference. A small N
// keeps the fit (which calls this at each node) fast for a unit test.
const N = 2000;
function zeta(s) {
    let acc = 0;
    for (let k = 1; k <= N; ++k) acc += Math.pow(k, -s);
    return acc;
}

const hasNative = existsSync(fileURLToPath(new URL("../dist/treeweave.node", import.meta.url)));
const hasWasm = existsSync(fileURLToPath(new URL("../dist/treeweave.mjs", import.meta.url)));
const backends = [...(hasNative ? ["native"] : []), ...(hasWasm ? ["wasm"] : [])];

test("at least one backend was built", () => {
    assert.ok(
        backends.length > 0,
        "no backend artifacts found in dist/ (build bindings-js and/or bindings-js-wasm)",
    );
});

function maxRelErr(xs, got, refFn) {
    let m = 0;
    for (let i = 0; i < xs.length; ++i) {
        const ref = refFn(xs[i]);
        m = Math.max(m, Math.abs(got[i] - ref) / Math.abs(ref));
    }
    return m;
}

for (const backend of backends) {
    describe(`treeweave [${backend}]`, () => {
        test("fits zeta and reports metadata", async () => {
            const tw = await Treeweave.fit((x) => zeta(x[0]), A, B, TOL, { backend });
            assert.equal(tw.dim, 1);
            assert.equal(tw.outDim, 1);
            assert.equal(tw.dtype, "f64");
            assert.ok(tw.memoryUsage() > 0);
            assert.match(tw.libVersion, /^\d+\.\d+\.\d+$/);
            tw.free();
        });

        test("eval / batch / sorted accuracy vs zeta", async () => {
            const tw = await Treeweave.fit((x) => zeta(x[0]), A, B, TOL, { backend });

            // single point
            assert.ok(Math.abs(tw.eval(7.5) - zeta(7.5)) / zeta(7.5) < 1e-9);

            // batch (unsorted): deterministic pseudo-random points in [A, B)
            const n = 5000;
            const xs = new Float64Array(n);
            let s = 12345;
            for (let i = 0; i < n; ++i) {
                s = (1103515245 * s + 12345) & 0x7fffffff;
                xs[i] = A + (s / 0x7fffffff) * (B - A);
            }
            const ys = tw.batch(xs);
            assert.equal(ys.length, n);
            assert.ok(maxRelErr(xs, ys, zeta) < 1e-7);

            // sorted fast path over the same points, sorted ascending (untimed)
            const xsorted = Float64Array.from(xs).sort();
            const ysorted = tw.sorted(xsorted);
            assert.ok(maxRelErr(xsorted, ysorted, zeta) < 1e-7);

            tw.free();
        });

        test("out= writes in place, is returned as-is and bit-exact", async () => {
            const tw = await Treeweave.fit((x) => zeta(x[0]), A, B, TOL, { backend });
            const xs = Float64Array.from({ length: 256 }, (_, i) => A + (i / 256) * (B - A));
            const fresh = tw.batch(xs);
            const buf = new Float64Array(xs.length);
            const ret = tw.batch(xs, { out: buf });
            assert.equal(ret, buf, "out= must be returned as the same object");
            assert.deepEqual(
                Array.from(buf),
                Array.from(fresh),
                "out= must be bit-exact vs a fresh result",
            );
            tw.free();
        });

        test("transposed returns one array per output component", async () => {
            const tw = await Treeweave.fit((x) => [zeta(x[0]), Math.log(x[0])], A, B, TOL, {
                backend,
            });
            assert.equal(tw.outDim, 2);
            const xs = Float64Array.from({ length: 100 }, (_, i) => A + (i / 100) * (B - A));
            const cols = tw.transposed(xs);
            assert.equal(cols.length, 2);
            assert.equal(cols[0].length, 100);
            assert.ok(maxRelErr(xs, cols[0], zeta) < 1e-7);
            assert.ok(maxRelErr(xs, cols[1], Math.log) < 1e-7);
            tw.free();
        });

        test("out-of-domain inputs yield NaN", async () => {
            const tw = await Treeweave.fit((x) => zeta(x[0]), A, B, TOL, { backend });
            assert.ok(Number.isNaN(tw.eval(A - 1.0))); // below the domain
            assert.ok(Number.isNaN(tw.eval(B + 1.0))); // above the domain
            const ood = tw.batch(Float64Array.of(A - 1.0, 6.0, B + 1.0));
            assert.ok(Number.isNaN(ood[0]));
            assert.ok(!Number.isNaN(ood[1]));
            assert.ok(Number.isNaN(ood[2]));
            tw.free();
        });

        test("free is idempotent and Symbol.dispose works", async () => {
            const tw = await Treeweave.fit((x) => zeta(x[0]), A, B, TOL, { backend });
            tw.free();
            tw.free(); // no throw on a second free
            const tw2 = await Treeweave.fit((x) => zeta(x[0]), A, B, TOL, { backend });
            tw2[Symbol.dispose]();
        });

        test("error paths: bad point length, bad out= length", async () => {
            const tw = await Treeweave.fit((x) => zeta(x[0]), A, B, TOL, { backend });
            assert.throws(() => tw.eval([1.0, 2.0]), /length/); // dim==1 expects a single coordinate
            assert.throws(
                () => tw.batch(Float64Array.of(5, 6, 7), { out: new Float64Array(2) }),
                /elements/,
            );
            assert.throws(() => tw.transposed(Float64Array.of(5, 6)), /outDim > 1/); // scalar-output fit
            tw.free();
        });

        test("a callback that throws propagates, not a corrupt fit", async () => {
            const boom = new Error("callback boom");
            await assert.rejects(
                Treeweave.fit(
                    () => {
                        throw boom;
                    },
                    A,
                    B,
                    TOL,
                    { backend },
                ),
                /boom/,
            );
        });

        describe("filename option", () => {
            const dir = mkdtempSync(join(tmpdir(), "treeweave-js-"));
            after(() => rmSync(dir, { recursive: true, force: true }));

            // Fit twice with the same file. The second fit must not call f and must
            // match the first fit within tol * max|f| per component at `pts` (flat, dim values per point).
            async function roundTrip(name, f, a, b, dim, outDim, extra = {}) {
                const file = join(dir, `${backend}-${name}.txt`);
                const opts = { backend, outDim, filename: file, ...extra };
                const first = await Treeweave.fit(f, a, b, 1e-8, opts);
                assert.ok(existsSync(file), "the first fit writes the file");
                let calls = 0;
                const second = await Treeweave.fit(
                    (x) => {
                        ++calls;
                        return f(x);
                    },
                    a,
                    b,
                    1e-8,
                    opts,
                );
                assert.equal(calls, 0, "the second fit loads the file and never calls f");
                const lo = [].concat(a);
                const hi = [].concat(b);
                const n = 257;
                const pts = new Float64Array(n * dim);
                for (let i = 0; i < n; ++i)
                    for (let d = 0; d < dim; ++d)
                        pts[i * dim + d] = lo[d] + ((hi[d] - lo[d]) * (i * (d + 3) + 0.5)) / (n * (d + 3));
                const y1 = first.batch(pts);
                const y2 = second.batch(pts);
                assert.equal(y1.length, n * outDim);
                for (let c = 0; c < outDim; ++c) {
                    let diff = 0;
                    let peak = 0;
                    for (let i = c; i < y1.length; i += outDim) {
                        diff = Math.max(diff, Math.abs(y1[i] - y2[i]));
                        peak = Math.max(peak, Math.abs(y1[i]));
                    }
                    assert.ok(diff <= 1e-8 * peak, `component ${c}: max|loaded - fitted| ${diff} > tol * ${peak}`);
                }
                first.free();
                second.free();
                return file;
            }

            test("1D round trip stays within tol and skips the callback", async () => {
                await roundTrip("1d", (x) => Math.exp(x[0]), 0, 1, 1, 1);
            });

            test("ND round trip stays within tol and skips the callback", async () => {
                await roundTrip(
                    "nd",
                    (x) => [Math.sin(x[0]) * Math.cos(x[1]), Math.exp(-x[0] * x[1])],
                    [0, 0],
                    [1, 1],
                    2,
                    2,
                );
            });

            test("a changed tol refits", async () => {
                const f = (x) => Math.exp(x[0]);
                const file = join(dir, `${backend}-tol.txt`);
                const opts = { backend, outDim: 1, filename: file };
                (await Treeweave.fit(f, 0, 1, 1e-8, opts)).free();
                let calls = 0;
                const tw = await Treeweave.fit(
                    (x) => {
                        ++calls;
                        return f(x);
                    },
                    0,
                    1,
                    1e-10,
                    opts,
                );
                assert.ok(calls > 0, "a header mismatch refits");
                tw.free();
            });

            test("a malformed file throws", async () => {
                const f = (x) => Math.exp(x[0]);
                const file = await roundTrip("bad", f, 0, 1, 1, 1);
                const good = readFileSync(file, "utf8");
                writeFileSync(file, good.slice(0, good.indexOf("subtrees")));
                await assert.rejects(
                    Treeweave.fit(f, 0, 1, 1e-8, { backend, outDim: 1, filename: file }),
                );
            });
        });
    });
}
