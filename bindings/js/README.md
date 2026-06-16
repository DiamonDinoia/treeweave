# @flatironinstitute/treeweave

JavaScript/TypeScript binding for [treeweave](https://github.com/DiamonDinoia/treeweave) —
adaptive piecewise-polynomial function approximation over the treeweave C ABI.

## Install

```sh
npm install @flatironinstitute/treeweave
```

The package ships **prebuilt native N-API binaries** (`prebuilds/<platform>-<arch>/`,
resolved by [`node-gyp-build`](https://github.com/prebuild/node-gyp-build)) for
Linux x64/arm64, macOS arm64/x64, and Windows x64 — picked up automatically under
Node for full speed — plus a bundled **WASM** build that runs in the browser and
acts as the fallback on any host without a matching prebuild. No native toolchain
is required: N-API is ABI-stable, so one binary per platform covers every Node
version. (`backend.ts` prefers the native addon under Node and falls back to WASM;
force one with `Treeweave.fit(..., { backend: "native" | "wasm" })`.)

A local `cmake --preset bindings-js` build drops `treeweave.node` straight into
`dist/`; the loader falls back to it when no `prebuilds/` directory is present
(the development case — `prebuilds/` is assembled only in CI).

## Usage

```ts
import { Treeweave } from "@flatironinstitute/treeweave";

// Async: the WASM backend loads on demand.
const approx = await Treeweave.fit((x) => x[0] * x[0], 0.0, 2.0, 1e-8);

approx.eval(1.5);                                    // single point -> 2.25
const xs = Float64Array.from({ length: 1000 }, (_, i) => (i / 1000) * 2);
approx.batch(xs);                                    // batch: many points, any order
approx.sorted(xs);                                   // promise xs is non-decreasing (1-D); ~3-4x faster
approx.free();                                       // or: using approx = await Treeweave.fit(...)
```

See the top-level [README](../../README.md) and the
[guides](https://diamondinoia.github.io/treeweave/) for the full API.

## Publishing (maintainers)

The package is published by the Release workflow (`.github/workflows/release.yml`,
job `publish-npm`).

> **First publish is a manual bootstrap.** npm OIDC trusted publishing cannot
> *create* a brand-new package name — it can only publish to a package that
> already exists. So the very first `npm publish` of `@flatironinstitute/treeweave`
> must be authenticated with an `NPM_TOKEN` secret (the workflow already uses
> `NODE_AUTH_TOKEN` when present). Once the package exists on the registry,
> configure OIDC trusted publishing and the token can be removed.
