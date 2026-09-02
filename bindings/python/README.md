# treeweave Python bindings

nanobind wrapper for the [treeweave](../../README.md) piecewise-polynomial
function approximator. The
[Python guide](https://diamondinoia.github.io/treeweave/guides/python.html)
carries the API, the options and the worked examples; this page covers the
build only.

## Install

<!-- literalinclude: tools/ci/docs-recipes.sh start-after: # BEGIN DOCS_PIP_PYPI end-before: # END DOCS_PIP_PYPI dedent: 4 -->
```bash
pip install treeweave
```

## Build from source

<!-- literalinclude: tools/ci/docs-recipes.sh start-after: # BEGIN DOCS_PYTHON_DEV end-before: # END DOCS_PYTHON_DEV dedent: 4 -->
```bash
cmake --preset bindings-python
cmake --build build/bindings-python -j
ctest --test-dir build/bindings-python -R python_treeweave --output-on-failure
```

scikit-build-core fetches nanobind and builds the extension, so
`pip install ./bindings/python` works too. For an editable install, add
`--no-build-isolation`.

## Quick start

<!-- literalinclude: bindings/python/examples/simple_1d.py start-after: # BEGIN DOCS_MINIMAL end-before: # END DOCS_MINIMAL -->
```python
import math
import numpy as np
import treeweave


def func(x):
    return math.exp(x[0])


# Fit exp(x) on [0, 1] syntax is fit(callback, lower_bound, upper_bound, tol).
approx = treeweave.fit(func, 0.0, 1.0, tol=1e-10)
print(approx)  # dtype, dim, out_dim, memory_usage
```

`bindings/python/examples/` holds the rest; every file there is a ctest.
