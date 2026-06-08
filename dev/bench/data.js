window.BENCHMARK_DATA = {
  "lastUpdate": 1780953755942,
  "repoUrl": "https://github.com/DiamonDinoia/canopy",
  "entries": {
    "canopy batch eval": [
      {
        "commit": {
          "author": {
            "email": "mbarbone@flatironinstitute.org",
            "name": "Marco Barbone",
            "username": "DiamonDinoia"
          },
          "committer": {
            "email": "mbarbone@flatironinstitute.org",
            "name": "Marco Barbone",
            "username": "DiamonDinoia"
          },
          "distinct": false,
          "id": "ab1b3716c613424280baf320680076661386ef3e",
          "message": "docs: unified callable + inferring fit; add the Fortran wrapper\n\nTop-level and bindings READMEs now show the called-object surface (point\nor batch, sorted=/transposed= flags) and that fit infers dim & out_dim, so\nthe common call is fit(f, a, b, tol). Add the Fortran wrapper to the\nbindings list, fix a stale Julia example (eval_multi -> b(...)), correct\nthe mechanism note (the opaque pointer is `context`, not `data`), and wire\nCANOPY_BUILD_FORTRAN / fortran_canopy into the CMake examples. Add\nbindings/fortran/README.md to the Doxyfile INPUT.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-05T11:07:39-04:00",
          "tree_id": "a24f8591c75e1995680d05d31cb735ad392017e3",
          "url": "https://github.com/DiamonDinoia/canopy/commit/ab1b3716c613424280baf320680076661386ef3e"
        },
        "date": 1780677219892,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000649947222222222,
            "unit": "s/batch",
            "extra": "MdAPE=0.00418461399013149; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.00108439825,
            "unit": "s/batch",
            "extra": "MdAPE=0.00294488583947772; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.002069823,
            "unit": "s/batch",
            "extra": "MdAPE=0.0207140480674144; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.001115416125,
            "unit": "s/batch",
            "extra": "MdAPE=0.00947186360972252; batch=65536 pts/call"
          }
        ]
      }
    ],
    "treeweave batch eval": [
      {
        "commit": {
          "author": {
            "email": "mbarbone@flatironinstitute.org",
            "name": "Marco Barbone",
            "username": "DiamonDinoia"
          },
          "committer": {
            "email": "mbarbone@flatironinstitute.org",
            "name": "Marco Barbone",
            "username": "DiamonDinoia"
          },
          "distinct": true,
          "id": "a4d35c56213c40ae55cd9c7637822f9ea2dc1e6e",
          "message": "treeweave: fast N-D function approximation\n\ntreeweave builds piecewise low-order-polynomial surrogates of expensive\nsmooth functions and evaluates them with runtime-dispatched SIMD. It ships\na header-only C++ core plus a C ABI and Python / Julia / Fortran / MATLAB\nbindings.\n\nThis project began as a clean-break rewrite of baobzi by Robert Blackwell\n(https://github.com/flatironinstitute/baobzi); the approximation approach\nand several design ideas originate there. See NOTICE for attribution.\n\nCo-Authored-By: Robert Blackwell <rblackwell@flatironinstitute.org>\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-08T17:21:24-04:00",
          "tree_id": "4efc02f6776089a91553f49f19fbe6718941a081",
          "url": "https://github.com/DiamonDinoia/canopy/commit/a4d35c56213c40ae55cd9c7637822f9ea2dc1e6e"
        },
        "date": 1780953755338,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000712311777777778,
            "unit": "s/batch",
            "extra": "MdAPE=0.00490638991744568; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.00172878644444444,
            "unit": "s/batch",
            "extra": "MdAPE=0.00672690002331275; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.00286582255555556,
            "unit": "s/batch",
            "extra": "MdAPE=0.00430721601682984; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.0016560854,
            "unit": "s/batch",
            "extra": "MdAPE=0.0128049906810706; batch=65536 pts/call"
          }
        ]
      }
    ]
  }
}