# Post-install smoke for the published `treeweave` PyPI wheel: import, fit
# exp on [0, 1), eval a batch, assert max abs error < 1e-8. Standalone (no
# bash heredoc) so it runs identically under PowerShell on Windows via
# `python pip_smoke.py`. Invoked by .github/workflows/release-install.yml.
import importlib.metadata
import math

import numpy as np
import treeweave

print("treeweave", importlib.metadata.version("treeweave"))
fn = treeweave.fit(lambda x: math.exp(x[0]), 0.0, 1.0, tol=1e-10)
xs = np.linspace(0.0, 1.0, 11, endpoint=False)
err = float(np.max(np.abs(fn(xs) - np.exp(xs))))
assert err < 1e-8, err
print(f"OK: max abs error {err:.2e}")
