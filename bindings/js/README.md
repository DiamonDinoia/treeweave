# @flatironinstitute/treeweave

JavaScript/TypeScript binding for
[treeweave](https://github.com/DiamonDinoia/treeweave): adaptive
piecewise-polynomial function approximation over the treeweave C ABI.

The API, the options and the worked examples live in the
[JavaScript guide](https://diamondinoia.github.io/treeweave/guides/js.html).

## Install

<!-- literalinclude: tools/ci/docs-recipes.sh start-after: # BEGIN DOCS_NPM end-before: # END DOCS_NPM dedent: 4 -->
```sh
npm install @flatironinstitute/treeweave
```

Prebuilt native N-API binaries for Linux x64/arm64, macOS arm64/x64, and
Windows x64 ship in `prebuilds/`, resolved by
[`node-gyp-build`](https://github.com/prebuild/node-gyp-build). A bundled WASM
build serves browsers and hosts without a matching prebuild. N-API is
ABI-stable, so one binary per platform covers every Node version. Force a
backend with `{ backend: "native" | "wasm" }`.

## Usage

<!-- literalinclude: bindings/js/examples/simple_1d.mjs start-after: // BEGIN DOCS_USAGE end-before: // END DOCS_USAGE -->
```js
// In an installed package this import is "@flatironinstitute/treeweave".
import { Treeweave } from "../dist/index.js";

// Fit sin(x) on [0, 1] syntax is fit(callback, lower_bound, upper_bound, tolerance, options).
const fn = await Treeweave.fit((x) => Math.sin(x[0]), 0.0, 1.0, 1e-10, {
    backend: "native",
});

// Evaluate fn on (0.5) and print the result.
const single = fn.eval(0.5);
console.log(`sin(0.5) approx=${single.toFixed(12)}`);
```

`FitOptions` rides in the trailing object, in lower camel case. The
[options guide](https://diamondinoia.github.io/treeweave/guides/options.html)
documents every field and its default.

## Build from source

The preset builds the native addon into `dist/` and compiles the TypeScript
layer; CI assembles `prebuilds/` separately.

<!-- literalinclude: tools/ci/docs-recipes.sh start-after: # BEGIN DOCS_JS_DEV end-before: # END DOCS_JS_DEV dedent: 4 -->
```bash
cmake --preset bindings-js
cmake --build build/bindings-js -j
```

## Publishing (maintainers)

The Release workflow publishes the package (`.github/workflows/release.yml`,
job `publish-npm`).

> **First publish is a manual bootstrap.** npm OIDC trusted publishing cannot
> *create* a brand-new package name. It can only publish to a package that
> already exists. So the first `npm publish` of `@flatironinstitute/treeweave`
> must authenticate with an `NPM_TOKEN` secret (the workflow already uses
> `NODE_AUTH_TOKEN` when present). Once the package exists on the registry,
> configure OIDC trusted publishing and drop the token.
