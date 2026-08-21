# @flatironinstitute/treeweave

JavaScript/TypeScript binding for
[treeweave](https://github.com/DiamonDinoia/treeweave): adaptive
piecewise-polynomial function approximation over the treeweave C ABI.

## Install

```sh
npm install @flatironinstitute/treeweave
```

Prebuilt native N-API binaries for Linux x64/arm64, macOS arm64/x64, and Windows x64 ship in `prebuilds/` (resolved by [`node-gyp-build`](https://github.com/prebuild/node-gyp-build)); a bundled WASM build serves browsers and hosts without a matching prebuild. N-API is ABI-stable, so one binary per platform covers every Node version. Force a backend with `{ backend: "native" | "wasm" }`.

A local `cmake --preset bindings-js` build drops `treeweave.node` into `dist/` (the development case; CI assembles `prebuilds/`).

## Usage

```js
// simple_1d.mjs: minimal 1D fit and evaluation (smoke test).
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
for (let i = 0; i < 11; ++i)
    maxErr = Math.max(maxErr, Math.abs(ys[i] - Math.sin(xs[i])));
console.log(`max |approx - sin| over 11 points: ${maxErr.toExponential(3)}`);

fn.free();

if (maxErr > 1e-6) {
    console.error(`error too large: ${maxErr}`);
    process.exit(1);
}
console.log("OK");
```

See [`examples/simple_1d.mjs`](examples/simple_1d.mjs) for a complete runnable example.

See the top-level [README](../../README.md) and the
[guides](https://diamondinoia.github.io/treeweave/) for the full API.

## Options

Pass a `FitOptions` object as the fifth argument to `Treeweave.fit`:

```ts
const approx = await Treeweave.fit(f, 0.0, 10.0, 1e-10, {
    tolKind: "absolute_max",
    maxDepth: 30,
    maxMemoryMib: 64,
});
```

`FitOptions` fields (all optional):

| Field | Default | Meaning |
|-------|---------|---------|
| `dim` | inferred | Input dimension; inferred from `a`/`b` length. |
| `outDim` | inferred | Output dimension; inferred by probing `f` at the box midpoint. |
| `dtype` | `"f64"` | Floating-point precision: `"f64"` or `"f32"`. |
| `tolKind` | `"relative_max"` | Tolerance interpretation. One of `"relative_max"`, `"absolute_max"`, `"relative_l2"`, `"absolute_l2"`, `"relative_tail"`, `"absolute_tail"`. |
| `maxDepth` | `50` | Tree-depth ceiling. |
| `maxMemoryMib` | `-1` (auto) | Memory budget in MiB. `-1` = auto (4/8/16 MiB for dim 1/2/3); `0` = no cap. |
| `allowMaxDepthLeaves` | `false` | Keep non-converged panels at max depth instead of throwing. |
| `minUniformDepth` | `0` | Force uniform refinement to this depth before adaptivity. |
| `backend` | `"auto"` | Force `"native"` or `"wasm"` backend (default: native under Node, WASM in browser). |

See the [guides](https://diamondinoia.github.io/treeweave/) for the full API reference.

## Publishing (maintainers)

The Release workflow publishes the package (`.github/workflows/release.yml`,
job `publish-npm`).

> **First publish is a manual bootstrap.** npm OIDC trusted publishing cannot
> *create* a brand-new package name. It can only publish to a package that
> already exists. So the first `npm publish` of `@flatironinstitute/treeweave`
> must authenticate with an `NPM_TOKEN` secret (the workflow already uses
> `NODE_AUTH_TOKEN` when present). Once the package exists on the registry,
> configure OIDC trusted publishing and drop the token.
