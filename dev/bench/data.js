window.BENCHMARK_DATA = {
  "lastUpdate": 1781040198328,
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
          "id": "1a01b6852c3110e739e083b5e4d5083e90272749",
          "message": "treeweave: fast N-D function approximation\n\ntreeweave builds piecewise low-order-polynomial surrogates of expensive\nsmooth functions and evaluates them with runtime-dispatched SIMD. It ships\na header-only C++ core plus a C ABI and Python / Julia / Fortran / MATLAB\nbindings.\n\nThis project began as a clean-break rewrite of baobzi by Robert Blackwell\n(https://github.com/flatironinstitute/baobzi); the approximation approach\nand several design ideas originate there. See NOTICE for attribution.\n\nCo-Authored-By: Robert Blackwell <rblackwell@flatironinstitute.org>\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-09T13:02:12-04:00",
          "tree_id": "31e15fef4b165ee48ae126bc7c8ce1040ed9da30",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/1a01b6852c3110e739e083b5e4d5083e90272749"
        },
        "date": 1781024653282,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.0006626087,
            "unit": "s/batch",
            "extra": "MdAPE=0.00495634781339666; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.00109827866666667,
            "unit": "s/batch",
            "extra": "MdAPE=0.00214732230698192; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.002008289625,
            "unit": "s/batch",
            "extra": "MdAPE=0.00135205785106963; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.001020998875,
            "unit": "s/batch",
            "extra": "MdAPE=0.00170306288136908; batch=65536 pts/call"
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
          "distinct": false,
          "id": "873530a1038947a0a9cf9191197477f44b75cc07",
          "message": "ci: TestPyPI dev publishing, Julia prebuilt smoke test, docs box-art font\n\n- testpypi.yml: every push to the default branch builds the full wheel\n  matrix + sdist and publishes to TestPyPI via OIDC trusted publishing.\n  Versioned X.Y.Z.devN (N = commits since last v* tag) so each commit is a\n  unique, non-colliding upload. set_dev_version.py pins the wheel version\n  since CMake's numeric project(VERSION) can't carry a .devN suffix.\n\n- julia-smoke.yml: workflow_dispatch job exercising the Julia distribution\n  path (deps/build.jl downloads the prebuilt libtreeweave_c from a Release,\n  dlopens it, runs Pkg.test) across all five shipped platforms — the path\n  bindings.yml never covers (it only tests the in-repo sibling build).\n\n- docs: force JetBrains Mono (full cell-width box-drawing glyphs) on code\n  blocks via _static/custom.css so the ASCII-art diagrams stay aligned in\n  the browser instead of drifting under per-glyph font fallback.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-09T16:37:45-04:00",
          "tree_id": "7cb2a898429e826660a4b09debb43b76651cddff",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/873530a1038947a0a9cf9191197477f44b75cc07"
        },
        "date": 1781037719851,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000663313625,
            "unit": "s/batch",
            "extra": "MdAPE=0.00614162799563558; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.001775850875,
            "unit": "s/batch",
            "extra": "MdAPE=0.00618751996065562; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.002637635,
            "unit": "s/batch",
            "extra": "MdAPE=0.00189344476364592; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.00148927955555556,
            "unit": "s/batch",
            "extra": "MdAPE=0.00315012662399097; batch=65536 pts/call"
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
          "id": "fda39ee89fdec884009b35af1689eae31eb9d438",
          "message": "ci: pin cibuildwheel to v3.4.1 (no bare @v3 tag exists)\n\npypa/cibuildwheel publishes v3.4.1 / v3.4 etc. but no moving @v3 major tag,\nso @v3 fails to resolve. Fixes the just-broken TestPyPI run and the same\nlatent bug in wheels.yml (tag-only, so never previously exercised).\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-09T16:43:26-04:00",
          "tree_id": "c8a9941837916c56b9a04084c0ccfeec0f1b6b80",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/fda39ee89fdec884009b35af1689eae31eb9d438"
        },
        "date": 1781038000100,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000659387111111111,
            "unit": "s/batch",
            "extra": "MdAPE=0.00944310245727987; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.001643080375,
            "unit": "s/batch",
            "extra": "MdAPE=0.00411849573608322; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.00263143355555556,
            "unit": "s/batch",
            "extra": "MdAPE=0.00107141249381009; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.00149765575,
            "unit": "s/batch",
            "extra": "MdAPE=0.00357015969902413; batch=65536 pts/call"
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
          "id": "bfbfe471a032ba6912a810e2cdc02e64a935c18a",
          "message": "cmake: map armv* arch names to -mtune=generic\n\ngcc rejects -mtune=armv8-a (an -march= arch name, not a CPU name), so\naarch64 builds fail at the nanobind step with \"unknown value 'armv8-a'\nfor '-mtune'\". Extend the x86-64-v* tune fallback to also cover armv*\nbaselines, deriving -mtune=generic for them.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-09T17:02:41-04:00",
          "tree_id": "f1e6e1ccbed1a5b9f7c7c6e0f6d0e7f3f93a522f",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/bfbfe471a032ba6912a810e2cdc02e64a935c18a"
        },
        "date": 1781039069122,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000668546333333333,
            "unit": "s/batch",
            "extra": "MdAPE=0.00326506839001317; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.00111672355555556,
            "unit": "s/batch",
            "extra": "MdAPE=0.00903438524808362; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.00203896725,
            "unit": "s/batch",
            "extra": "MdAPE=0.00633203930038972; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.001033656875,
            "unit": "s/batch",
            "extra": "MdAPE=0.00438323289225915; batch=65536 pts/call"
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
          "id": "94fc442859550cf45c6a8bca7242a339e4ba44bb",
          "message": "treeweave: generalize C-ABI runtime SIMD dispatch to ARM and RISC-V\n\nThe C-ABI runtime ISA dispatcher (TREEWEAVE_C_MULTIARCH) was x86-only:\ncmake gated the multi-variant fan-out behind an x86 regex and\narch_dispatch.cpp hardcoded an x86 xsimd::arch_list. On ARM/RISC-V the\nbuild silently fell back to a single compile-time variant.\n\nMake the dispatch path family-portable, mirroring DiamonDinoia/simdrng:\n\n- New include/treeweave/detail/dispatch_arch.hpp selects the dispatch\n  arch_list at compile time from xsimd::best_arch's inheritance:\n  x86 -> {avx512bw, fma3<avx2>, sse4_2, sse2}; aarch64 -> {neon64};\n  riscv64 -> {rvv128}. arch_dispatch.cpp now includes it instead of a\n  hardcoded list; the xsimd::dispatch / available_architectures().has /\n  TREEWEAVE_FORCE_ARCH machinery is unchanged and generic over the list.\n\n- treeweave_c_dispatch.cmake drives the per-variant OBJECT-library fan-out\n  for any multi-arch family, not just x86: x86 keeps its 4-level ladder,\n  non-Apple aarch64 builds a single neon64 variant (-march=armv8-a),\n  riscv64 a single rvv variant (-march=rv64gcv, best-effort/untested).\n  Apple aarch64 / MSVC / unknown stay single-arch.\n\n- SVE deliberately excluded: xsimd's sve<N> bakes width at compile time\n  but the runtime has(sve<N>) probe only checks the presence bit, so a\n  fixed-width SVE variant would mis-run on mismatched-width hardware.\n\nCI/build wiring: new multiarch-arm preset; ubuntu-24.04-arm multiarch\nsmoke job; TREEWEAVE_C_MULTIARCH=ON on the linux-aarch64 wheels/release\nrows. Docs: new guides/dispatch.rst + how-treeweave-works update.\n\nVerified on x86: all five FORCE_ARCH variants + test_c_abi pass.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-09T17:17:09-04:00",
          "tree_id": "c1ae01f7973302ef70b13bcb983a6373fb01bd61",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/94fc442859550cf45c6a8bca7242a339e4ba44bb"
        },
        "date": 1781039947186,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000664544111111111,
            "unit": "s/batch",
            "extra": "MdAPE=0.00245629145523468; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.00163574722222222,
            "unit": "s/batch",
            "extra": "MdAPE=0.00487724733028858; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.00263694433333333,
            "unit": "s/batch",
            "extra": "MdAPE=0.002088758202491; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.001501324,
            "unit": "s/batch",
            "extra": "MdAPE=0.00252802251501363; batch=65536 pts/call"
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
          "id": "d3d456366536bac292867c32109b4b01ff95a34b",
          "message": "style: clang-format + gersemi the dispatch sources\n\nApply the pinned clang-format (18.1.8) and gersemi (0.27.1) formatting\nthe CI Lint & format job enforces on whole files. No semantic change.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-09T17:21:14-04:00",
          "tree_id": "4a37c3249303395a6bb5d3b92dcf51db98fb6549",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/d3d456366536bac292867c32109b4b01ff95a34b"
        },
        "date": 1781040197592,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000666082666666667,
            "unit": "s/batch",
            "extra": "MdAPE=0.00884537873230285; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.001117429875,
            "unit": "s/batch",
            "extra": "MdAPE=0.0049443566474733; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.00201518825,
            "unit": "s/batch",
            "extra": "MdAPE=0.00152344627062207; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.0010295961,
            "unit": "s/batch",
            "extra": "MdAPE=0.00130216834778519; batch=65536 pts/call"
          }
        ]
      }
    ]
  }
}