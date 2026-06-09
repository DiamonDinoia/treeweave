window.BENCHMARK_DATA = {
  "lastUpdate": 1781021601607,
  "repoUrl": "https://github.com/DiamonDinoia/treeweave",
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
      },
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
          "id": "eab944140e13c9e3e00b63e4a81834ff24fb522f",
          "message": "treeweave: fast N-D function approximation\n\ntreeweave builds piecewise low-order-polynomial surrogates of expensive\nsmooth functions and evaluates them with runtime-dispatched SIMD. It ships\na header-only C++ core plus a C ABI and Python / Julia / Fortran / MATLAB\nbindings.\n\nThis project began as a clean-break rewrite of baobzi by Robert Blackwell\n(https://github.com/flatironinstitute/baobzi); the approximation approach\nand several design ideas originate there. See NOTICE for attribution.\n\nCo-Authored-By: Robert Blackwell <rblackwell@flatironinstitute.org>\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-08T17:53:49-04:00",
          "tree_id": "5e5900b6511d64f87ac7f64a60055b1267d9a6fe",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/eab944140e13c9e3e00b63e4a81834ff24fb522f"
        },
        "date": 1780955674402,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000650762,
            "unit": "s/batch",
            "extra": "MdAPE=0.00415679901958186; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.00108068533333333,
            "unit": "s/batch",
            "extra": "MdAPE=0.0056608673071928; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.0020292182,
            "unit": "s/batch",
            "extra": "MdAPE=0.00493453244771734; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.0010160432,
            "unit": "s/batch",
            "extra": "MdAPE=0.00189834269123009; batch=65536 pts/call"
          }
        ]
      },
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
          "id": "48184ab1004d7ed055d70e8f1875fa5bf9f0fe30",
          "message": "treeweave: fast N-D function approximation\n\ntreeweave builds piecewise low-order-polynomial surrogates of expensive\nsmooth functions and evaluates them with runtime-dispatched SIMD. It ships\na header-only C++ core plus a C ABI and Python / Julia / Fortran / MATLAB\nbindings.\n\nThis project began as a clean-break rewrite of baobzi by Robert Blackwell\n(https://github.com/flatironinstitute/baobzi); the approximation approach\nand several design ideas originate there. See NOTICE for attribution.\n\nCo-Authored-By: Robert Blackwell <rblackwell@flatironinstitute.org>\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-09T11:05:37-04:00",
          "tree_id": "046e2aaa630bc1068fe2b9718e72b8b577eee304",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/48184ab1004d7ed055d70e8f1875fa5bf9f0fe30"
        },
        "date": 1781018007984,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.0005126097,
            "unit": "s/batch",
            "extra": "MdAPE=0.00216776864330367; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.000899529666666667,
            "unit": "s/batch",
            "extra": "MdAPE=0.00422741979793321; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.001617328,
            "unit": "s/batch",
            "extra": "MdAPE=0.00814746137601525; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.000818308125,
            "unit": "s/batch",
            "extra": "MdAPE=0.00167984436475904; batch=65536 pts/call"
          }
        ]
      },
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
          "id": "95f0f0a3fa2b0bb1be50745caed8c4cfb45bc59f",
          "message": "treeweave: fast N-D function approximation\n\ntreeweave builds piecewise low-order-polynomial surrogates of expensive\nsmooth functions and evaluates them with runtime-dispatched SIMD. It ships\na header-only C++ core plus a C ABI and Python / Julia / Fortran / MATLAB\nbindings.\n\nThis project began as a clean-break rewrite of baobzi by Robert Blackwell\n(https://github.com/flatironinstitute/baobzi); the approximation approach\nand several design ideas originate there. See NOTICE for attribution.\n\nCo-Authored-By: Robert Blackwell <rblackwell@flatironinstitute.org>\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-09T11:27:53-04:00",
          "tree_id": "f7753539699d5b50c8266ecbaf540e3fe670b20b",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/95f0f0a3fa2b0bb1be50745caed8c4cfb45bc59f"
        },
        "date": 1781019008750,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.001128675125,
            "unit": "s/batch",
            "extra": "MdAPE=0.00121797431229831; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.001112223,
            "unit": "s/batch",
            "extra": "MdAPE=0.0184375733813773; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.0020144536,
            "unit": "s/batch",
            "extra": "MdAPE=0.00116988903299858; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.00103253288888889,
            "unit": "s/batch",
            "extra": "MdAPE=0.00141707991878138; batch=65536 pts/call"
          }
        ]
      },
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
          "id": "bbc611287a3982a834dd11f8852a16a3b0c38e60",
          "message": "treeweave: fast N-D function approximation\n\ntreeweave builds piecewise low-order-polynomial surrogates of expensive\nsmooth functions and evaluates them with runtime-dispatched SIMD. It ships\na header-only C++ core plus a C ABI and Python / Julia / Fortran / MATLAB\nbindings.\n\nThis project began as a clean-break rewrite of baobzi by Robert Blackwell\n(https://github.com/flatironinstitute/baobzi); the approximation approach\nand several design ideas originate there. See NOTICE for attribution.\n\nCo-Authored-By: Robert Blackwell <rblackwell@flatironinstitute.org>\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-09T12:11:40-04:00",
          "tree_id": "a5a5b74ab1004d7fc1692a5b7ac488691b074d9a",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/bbc611287a3982a834dd11f8852a16a3b0c38e60"
        },
        "date": 1781021600890,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000667472,
            "unit": "s/batch",
            "extra": "MdAPE=0.00531994708105163; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.00110294644444444,
            "unit": "s/batch",
            "extra": "MdAPE=0.00430665554068018; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.002030468,
            "unit": "s/batch",
            "extra": "MdAPE=0.00649438558455041; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.00102595711111111,
            "unit": "s/batch",
            "extra": "MdAPE=0.00177995336071298; batch=65536 pts/call"
          }
        ]
      }
    ]
  }
}