window.BENCHMARK_DATA = {
  "lastUpdate": 1781299683871,
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
          "id": "870148f0a0ee05066181cd9c433a0d74b2828241",
          "message": "style: gersemi the -mtune if() condition\n\ngersemi 0.27.1 reflows the multi-clause if() onto separate lines once\nthe OR makes it exceed the line budget. Latent since the -mtune commit\n(its CI lint was cancelled by concurrency). No semantic change.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-09T17:22:58-04:00",
          "tree_id": "6e8fe8d1fc5886b6aacc7273392ee3eb4e5ad87b",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/870148f0a0ee05066181cd9c433a0d74b2828241"
        },
        "date": 1781040241762,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000661064666666667,
            "unit": "s/batch",
            "extra": "MdAPE=0.00195299648390177; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.00110158977777778,
            "unit": "s/batch",
            "extra": "MdAPE=0.00826540690076879; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.002030495,
            "unit": "s/batch",
            "extra": "MdAPE=0.00794381313703233; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.001029819375,
            "unit": "s/batch",
            "extra": "MdAPE=0.00522410993592663; batch=65536 pts/call"
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
          "id": "cbc7e1e8cc59dfbcd79eee063c581d399961cf91",
          "message": "fix: build the C ABI single-arch on MSVC even when MULTIARCH=ON\n\nThe new family-driven fan-out fired on any x86 target, including MSVC\n(AMD64 matches the x86 regex). But the per-variant flags are GCC/Clang\n`-march=`/`-mtune=` spellings cl.exe does not accept, so the flag probe\nfailed and configure aborted with FATAL_ERROR — breaking the Windows\nwheel (cmake-args carry TREEWEAVE_C_MULTIARCH=ON). The code comment\nalready claimed MSVC falls through to single-arch; enforce it with a\n`NOT MSVC` guard on the multi-arch family gate, matching the documented\nintent. No effect on the GCC/Clang x86 ladder or the aarch64/riscv paths.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-09T17:30:31-04:00",
          "tree_id": "abf9c9a99d8016d97661ae967a02b66971962e1a",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/cbc7e1e8cc59dfbcd79eee063c581d399961cf91"
        },
        "date": 1781040752721,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000666867,
            "unit": "s/batch",
            "extra": "MdAPE=0.0131938989612312; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.00170207488888889,
            "unit": "s/batch",
            "extra": "MdAPE=0.0330741488750267; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.00263716725,
            "unit": "s/batch",
            "extra": "MdAPE=0.00278810267564313; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.00150485375,
            "unit": "s/batch",
            "extra": "MdAPE=0.00517091458468628; batch=65536 pts/call"
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
          "id": "16e5232f8bb09505c529577e131e5f87996f7957",
          "message": "analysis: scope static analysis to the library, fix all warnings, enforce -Werror\n\nStatic analysis should target treeweave's own code, not the test suite or\nfetched third-party sources. Clear CMAKE_CXX_CLANG_TIDY / CMAKE_CXX_CPPCHECK for\nthe tests/ subdirectory (deterministic RNG seeds, C arrays driving the C ABI,\nand mutable instrumentation globals are legitimate test idioms) and around the\ndependency fetch in treeweave_deps.cmake (Catch2/poet/polyfit are not ours to\nlint).\n\nWith the noise gone, fix every remaining library warning:\n  - treeweave.cpp: trailing return types; <cstddef>/<treeweave/treeweave.hpp>\n    direct includes; NOLINT the deliberate noexcept-boundary empty catch.\n  - arch_single.cpp: direct includes for treeweave_func_t / options.\n  - dispatch_variant.cpp.in: NOLINT the c_binding/arch_degree_table includes\n    used only in the explicit-instantiation declaration.\n  - c_binding_detail.hpp: value-initialize the callback output buffer.\n  - function.hpp: rule-of-5 on Buf (delete moves); [[nodiscard]] leaf_id_of;\n    std::ranges::all_of in has_fast_quantize.\n  - polytree.hpp: default-member-init for max_depth_; auto for the cast;\n    NOLINT the const-correctness FP on a value mutated only in the ND branch.\n  - tol_kind.hpp: TolKind base type std::uint8_t.\n  - .clang-tidy: ignore the xsimd umbrella for include-cleaner;\n    AllowSoleDefaultDtor so a virtual-dtor-only interface (IEval) is fine.\n\nThen enforce it: WarningsAsErrors '*' for clang-tidy and --error-exitcode=1 for\ncppcheck, so the Static Analysis CI job fails on any future regression instead\nof silently accumulating warnings.\n\nVerified: clean clang analysis build with PCH disabled (the superset that\nanalyzes every header) is warning-free under -Werror, and all 62 tests pass.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-10T11:08:37-04:00",
          "tree_id": "115b66928e15c669239d3052a518f01dd6c71f39",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/16e5232f8bb09505c529577e131e5f87996f7957"
        },
        "date": 1781104257886,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.0006661803,
            "unit": "s/batch",
            "extra": "MdAPE=0.00511637435263059; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.001276386375,
            "unit": "s/batch",
            "extra": "MdAPE=0.00808663682548839; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.00201792044444444,
            "unit": "s/batch",
            "extra": "MdAPE=0.00231107846103874; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.00103551044444444,
            "unit": "s/batch",
            "extra": "MdAPE=0.0026577199454579; batch=65536 pts/call"
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
          "id": "de03a69e1292c99835e0d4ebe3933869929e7bc1",
          "message": "bench: add CodSpeed (Google Benchmark, simulation mode) CI\n\nAdds deterministic, instruction-count perf gating via CodSpeed.io's\n`simulation` mode (free for OSS, OIDC — no token), complementing the\nexisting nanobench → gh-pages wall-time dashboard (bench.yml stays as-is).\n\nCodSpeed C++ supports only Google Benchmark, so the new bench\n(examples/c++/treeweave_codspeed_bench.cpp) is the Google Benchmark twin\nof treeweave_ci_bench: same four batch-eval cases, names, seeds, and\nN = 1<<16, so the two dashboards line up. Google Benchmark is fetched\nfrom CodSpeedHQ/codspeed-cpp (v2.4.0, SOURCE_SUBDIR google_benchmark)\nbehind the new TREEWEAVE_BUILD_CODSPEED option; their compat layer swaps\nin the instrumented runtime when CODSPEED_MODE is set.\n\nThe `codspeed` preset builds RelWithDebInfo at the x86-64-v3 (AVX2)\nbaseline — simulation runs under Valgrind/Cachegrind, which handles AVX2\nbut not AVX-512 (same constraint as the valgrind preset). codspeed.yml\nbuilds + runs the bench under CodSpeedHQ/action@v4 on push/PR; uploading\na report needs the repo connected at app.codspeed.io (one-time\nGitHub-App install), until then it is still a build+run smoke test.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-10T12:20:23-04:00",
          "tree_id": "d0060698b63b2b5bff4a793a157d8d1dc618f173",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/de03a69e1292c99835e0d4ebe3933869929e7bc1"
        },
        "date": 1781108508699,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.00066355825,
            "unit": "s/batch",
            "extra": "MdAPE=0.00180469215767273; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.001084565125,
            "unit": "s/batch",
            "extra": "MdAPE=0.00363170660611257; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.002016042875,
            "unit": "s/batch",
            "extra": "MdAPE=0.000272637958931536; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.001029631375,
            "unit": "s/batch",
            "extra": "MdAPE=0.00390468607423289; batch=65536 pts/call"
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
          "id": "77d7edd8276c7d40766366f15baccd4048733e52",
          "message": "ci: make CodSpeed upload non-blocking until repo is connected\n\nThe CodSpeed action builds + runs the bench under Valgrind simulation\n(measuring all four cases) and then uploads. Until the repo is connected\nat app.codspeed.io (a one-time GitHub-App install the maintainer must\nauthorize), the upload 401s (\"Repository not found\") and the action\nexits 1, turning the job red on every push/PR.\n\nMark the run step continue-on-error so the build+run smoke test reports\ngreen in the meantime. Remove this once the app is connected to restore\ninstruction-count perf gating.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-10T12:39:07-04:00",
          "tree_id": "e8ba3afe1fd6c44b7d159189ee1e60b1cfcc998a",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/77d7edd8276c7d40766366f15baccd4048733e52"
        },
        "date": 1781109677870,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.00066041875,
            "unit": "s/batch",
            "extra": "MdAPE=0.00267948007291839; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.00110513188888889,
            "unit": "s/batch",
            "extra": "MdAPE=0.00532978512970755; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.002026479125,
            "unit": "s/batch",
            "extra": "MdAPE=0.00362017673727824; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.001029628625,
            "unit": "s/batch",
            "extra": "MdAPE=0.00130277091362452; batch=65536 pts/call"
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
          "id": "f69b9e5836fae9280d75ac92c22507af94747a47",
          "message": "analysis: suppress opinionated cppcheck style checks (noExplicitConstructor, useStlAlgorithm)\n\nCI's cppcheck (Ubuntu 24.04) is newer than the version used for the\nlocal -Werror analysis verification and enables two `style` checks the\nolder one did not, both of which conflict with this library's design:\n\n  - noExplicitConstructor on detail::Value's single-arg constructors,\n    which are intentionally implicit (scalar/array/pointer construction\n    is ergonomic and used throughout the eval pipeline; making them\n    explicit would be an API/behaviour change).\n  - useStlAlgorithm on six hot-path raw loops (value/function/polytree).\n    Rewriting them as std::accumulate/all_of/copy in a SIMD library\n    obscures intent for no measured gain and risks codegen regressions.\n\nSuppress both at the cppcheck invocation, matching the existing NOLINT\npattern for clang-tidy checks the project rejects. shadowFunction stays\nenabled (a real smell — fixed by renaming in the previous commit). No\ncodepath changes, so no asm/perf impact.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-10T12:53:16-04:00",
          "tree_id": "c2f369f097727405c11e0b3bf5e00fccdeae01c1",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/f69b9e5836fae9280d75ac92c22507af94747a47"
        },
        "date": 1781110448313,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.00066242625,
            "unit": "s/batch",
            "extra": "MdAPE=0.00172614319969394; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.001102894875,
            "unit": "s/batch",
            "extra": "MdAPE=0.00961076644460974; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.002013699,
            "unit": "s/batch",
            "extra": "MdAPE=0.00182180716962894; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.00102908044444444,
            "unit": "s/batch",
            "extra": "MdAPE=0.00673998799837607; batch=65536 pts/call"
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
          "id": "a9ba2049003c6c08a8196dbe8d06c66b77132887",
          "message": "analysis: const-qualify C-ABI eval locals (cppcheck constVariable*)\n\nCI's cppcheck (2.13.0) flags two more checks in the C-ABI eval shims\nthat the local-verification version did not:\n\n  - constVariablePointer: the `impl` pointer in the eval/batch/sorted/\n    transposed shims is only dereferenced for const IEval methods →\n    `const auto *impl`.\n  - constVariable: the small `x[]` input arrays in the by-value scalar\n    eval_{1,2,3}d entry points are passed to a `const`-pointer parameter\n    and never mutated → `const`.\n\nBoth are pure const-correctness on locals (compile-time qualifiers, no\ncodegen/asm change). Verified the whole C-ABI library is now cppcheck-\nclean by running cppcheck 2.13.0 (same version CI installs on Ubuntu\n24.04) in a container over the analysis compile DB.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-10T13:05:29-04:00",
          "tree_id": "319b35a66bf41f7324b32d51ab3cdae6fc346b43",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/a9ba2049003c6c08a8196dbe8d06c66b77132887"
        },
        "date": 1781111307315,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000660229777777778,
            "unit": "s/batch",
            "extra": "MdAPE=0.00286032501816969; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.001096705875,
            "unit": "s/batch",
            "extra": "MdAPE=0.00911378900093026; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.00202788811111111,
            "unit": "s/batch",
            "extra": "MdAPE=0.00190104264220985; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.00102981011111111,
            "unit": "s/batch",
            "extra": "MdAPE=0.00260443964144065; batch=65536 pts/call"
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
          "id": "3d354d6eb4d553e7b967ed20fe623ee7cc2204e0",
          "message": "ci: restore CodSpeed perf gating now that the repo is connected\n\nThe treeweave repo is now connected at app.codspeed.io, so the upload\nsucceeds (OIDC auth). Drop the temporary continue-on-error so a failed\nrun / regression once again fails the job.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-10T14:28:22-04:00",
          "tree_id": "307c5d7568c0aec4a9df8fd491ec63f957b5feaa",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/3d354d6eb4d553e7b967ed20fe623ee7cc2204e0"
        },
        "date": 1781116158913,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000662080555555556,
            "unit": "s/batch",
            "extra": "MdAPE=0.0049131542271374; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.00107860744444444,
            "unit": "s/batch",
            "extra": "MdAPE=0.00533592334431226; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.00204362275,
            "unit": "s/batch",
            "extra": "MdAPE=0.0144128115154793; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.001031050625,
            "unit": "s/batch",
            "extra": "MdAPE=0.00408407171916857; batch=65536 pts/call"
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
          "id": "83511e2c056a659b6d193e95cc1c8e27566f6dcc",
          "message": "chore: -Werror static analysis, clang-format 22, BSD relicense, docs, CodSpeed, macOS runner modernization\n\nConsolidates the post-rename hardening effort into a single commit:\n\n- Static analysis: finish the -Werror clang-tidy 22 pass (library-only\n  scope, WarningsAsErrors='*'), plus cppcheck-version-skew fixes\n  (shadowFunction renames, constVariable const-qualification, and\n  suppression of opinionated style checks noExplicitConstructor /\n  useStlAlgorithm).\n- clang-format: bump to clang-format 22 in pre-commit and CI; reformat.\n- License: relicense Apache-2.0 -> BSD-3-Clause.\n- Docs: add function-approximation background from Barnett's FWAM7 talk.\n- CI: add CodSpeed (Google Benchmark, simulation mode) with perf gating.\n- CI: modernize macOS runners (macos-13 -> macos-15-intel) across\n  release, wheels, testpypi, and julia-smoke; free GitHub-hosted\n  runners only (arm64 stays macos-14).\n- Julia release prep: align the Julia README repo URL with\n  DiamonDinoia/treeweave so Pkg.add(url=...) and the build.jl default\n  download repo agree; deps.jl stays untracked/gitignored.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-10T14:49:17-04:00",
          "tree_id": "9d063b6003ac914f1112e93d71198349f42dfb2c",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/83511e2c056a659b6d193e95cc1c8e27566f6dcc"
        },
        "date": 1781117482118,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000657131,
            "unit": "s/batch",
            "extra": "MdAPE=0.00363252893078259; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.001722436,
            "unit": "s/batch",
            "extra": "MdAPE=0.0297377336902127; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.00268155025,
            "unit": "s/batch",
            "extra": "MdAPE=0.0136347989318634; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.00148062855555556,
            "unit": "s/batch",
            "extra": "MdAPE=0.00243819782370832; batch=65536 pts/call"
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
          "id": "9c5719e719eff57054a96d0e8f376941a131603c",
          "message": "ci+build: expand compiler matrix, NaN/Inf-safe fast-math, doc + macOS fixes\n\n- ci.yml: broaden to finufft-level coverage — gcc-11/12, clang-16/17,\n  macos-15, gcc-on-macOS, and a Windows clang-cl row.\n- treeweave_toolchain.cmake: GCC-on-Apple-Silicon uses -mcpu=native\n  (GCC rejects apple-m1); add NaN/Inf-preserving fast-math (finufft's\n  curated subset, probed, no -ffinite-math-only) behind TREEWEAVE_FAST_MATH.\n- CMakePresets.json: ci-windows-clang (ClangCL toolset).\n- README: correct the domain limitation (best-effort outside [x0, x1),\n  no OOB access, no accuracy guarantee).\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-10T15:18:33-04:00",
          "tree_id": "591a73e3b4d5841a444701cb26b8e275875e29f9",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/9c5719e719eff57054a96d0e8f376941a131603c"
        },
        "date": 1781119183487,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000656488666666667,
            "unit": "s/batch",
            "extra": "MdAPE=0.00422978656285571; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.00165253425,
            "unit": "s/batch",
            "extra": "MdAPE=0.00502399784210588; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.002725585,
            "unit": "s/batch",
            "extra": "MdAPE=0.00914188127541105; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.00154751644444444,
            "unit": "s/batch",
            "extra": "MdAPE=0.00210038620567255; batch=65536 pts/call"
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
          "id": "1903d5cd405ba078235359094018ccd7795b17b5",
          "message": "treeweave: fast N-dimensional function approximation\n\ntreeweave fits a scalar function of 1-N real variables once, then evaluates the\napproximation many times, very fast. It adaptively partitions the domain into a\nk-d tree of axis-aligned panels and fits a low-order polynomial on each\n(Chebyshev-sampled, stored in the monomial basis, evaluated by tensor-product\nHorner), refining only where the local error exceeds the requested tolerance.\nThe fit is the slow phase; evaluation is a branch-light descent plus an FMA-bound\nHorner kernel, batched via a counting sort by leaf.\n\nHighlights:\n- Header-only C++20 core (include/treeweave) plus a stable C ABI (libtreeweave_c)\n  with runtime SIMD dispatch (SSE->AVX-512, NEON) via xsimd, so one binary runs\n  across a whole CPU family.\n- Bindings for Python (nanobind), Julia, Fortran, and MATLAB/Octave, all layered\n  on the C ABI.\n- Built on polyfit (the per-panel fit) and POET (compile-time unrolling); the\n  fit/eval pipeline is original. Inspired by Robert Blackwell's baobzi; shares no\n  code with it.\n- CMake build with presets; broad compiler/OS CI matrix, static analysis\n  (clang-tidy + cppcheck under -Werror), sanitizers, Valgrind, coverage, CodSpeed\n  perf gating, and Sphinx/Doxygen docs.\n\nBSD-3-Clause. See docs/how-treeweave-works.md for the design and the math.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-10T15:32:24-04:00",
          "tree_id": "c92a3c08aa4809fb3312e5cbc0d6c51278042794",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/1903d5cd405ba078235359094018ccd7795b17b5"
        },
        "date": 1781120010768,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000661783111111111,
            "unit": "s/batch",
            "extra": "MdAPE=0.00139882258308458; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.00111628444444444,
            "unit": "s/batch",
            "extra": "MdAPE=0.0164364604603683; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.0020194955,
            "unit": "s/batch",
            "extra": "MdAPE=0.00293785766522292; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.00102540233333333,
            "unit": "s/batch",
            "extra": "MdAPE=0.00277185483786014; batch=65536 pts/call"
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
          "id": "6172e6510fb0de34a220de02b9bddd95e59f2964",
          "message": "treeweave: fast N-dimensional function approximation\n\ntreeweave fits a scalar function of 1-N real variables once, then evaluates the\napproximation many times, very fast. It adaptively partitions the domain into a\nk-d tree of axis-aligned panels and fits a low-order polynomial on each\n(Chebyshev-sampled, stored in the monomial basis, evaluated by tensor-product\nHorner), refining only where the local error exceeds the requested tolerance.\nThe fit is the slow phase; evaluation is a branch-light descent plus an FMA-bound\nHorner kernel, batched via a counting sort by leaf.\n\nHighlights:\n- Header-only C++20 core (include/treeweave) plus a stable C ABI (libtreeweave_c)\n  with runtime SIMD dispatch (SSE->AVX-512, NEON) via xsimd, so one binary runs\n  across a whole CPU family.\n- Bindings for Python (nanobind), Julia, Fortran, and MATLAB/Octave, all layered\n  on the C ABI.\n- Built on polyfit (the per-panel fit) and POET (compile-time unrolling); the\n  fit/eval pipeline is original. Inspired by Robert Blackwell's baobzi; shares no\n  code with it.\n- CMake build with presets; broad compiler/OS CI matrix, static analysis\n  (clang-tidy + cppcheck under -Werror), sanitizers, Valgrind, coverage, CodSpeed\n  perf gating, and Sphinx/Doxygen docs.\n\nBSD-3-Clause. See docs/how-treeweave-works.md for the design and the math.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-10T15:53:29-04:00",
          "tree_id": "fd8587e394d9987d57ebc4407d4b25b6b18a6ed5",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/6172e6510fb0de34a220de02b9bddd95e59f2964"
        },
        "date": 1781121267038,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000659695111111111,
            "unit": "s/batch",
            "extra": "MdAPE=0.00589805042526699; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.001626530375,
            "unit": "s/batch",
            "extra": "MdAPE=0.009843618407308; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.00265504988888889,
            "unit": "s/batch",
            "extra": "MdAPE=0.0010606560977818; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.0015061931,
            "unit": "s/batch",
            "extra": "MdAPE=0.00393913829958185; batch=65536 pts/call"
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
          "id": "fb95a5829d1e7ff7a7e2fc16c875ab538e1885ed",
          "message": "treeweave: fast N-dimensional function approximation\n\ntreeweave fits a scalar function of 1-N real variables once, then evaluates the\napproximation many times, very fast. It adaptively partitions the domain into a\nk-d tree of axis-aligned panels and fits a low-order polynomial on each\n(Chebyshev-sampled, stored in the monomial basis, evaluated by tensor-product\nHorner), refining only where the local error exceeds the requested tolerance.\nThe fit is the slow phase; evaluation is a branch-light descent plus an FMA-bound\nHorner kernel, batched via a counting sort by leaf.\n\nHighlights:\n- Header-only C++20 core (include/treeweave) plus a stable C ABI (libtreeweave_c)\n  with runtime SIMD dispatch (SSE->AVX-512, NEON) via xsimd, so one binary runs\n  across a whole CPU family.\n- Bindings for Python (nanobind), Julia, Fortran, and MATLAB/Octave, all layered\n  on the C ABI.\n- Built on polyfit (the per-panel fit) and POET (compile-time unrolling); the\n  fit/eval pipeline is original. Inspired by Robert Blackwell's baobzi; shares no\n  code with it.\n- CMake build with presets; broad compiler/OS CI matrix, static analysis\n  (clang-tidy + cppcheck under -Werror), sanitizers, Valgrind, coverage, CodSpeed\n  perf gating, and Sphinx/Doxygen docs.\n\nBSD-3-Clause. See docs/how-treeweave-works.md for the design and the math.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-10T16:12:04-04:00",
          "tree_id": "3ffd5ded660484646bf2360538f763248e4d266e",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/fb95a5829d1e7ff7a7e2fc16c875ab538e1885ed"
        },
        "date": 1781122460741,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000656459444444444,
            "unit": "s/batch",
            "extra": "MdAPE=0.00287639031948938; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.001657544125,
            "unit": "s/batch",
            "extra": "MdAPE=0.0029544348432458; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.00265403,
            "unit": "s/batch",
            "extra": "MdAPE=0.00225542685534589; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.00149673522222222,
            "unit": "s/batch",
            "extra": "MdAPE=0.00259725548037209; batch=65536 pts/call"
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
          "id": "af763a154f2dd9334c5b3719b44585bfc41d6e50",
          "message": "treeweave: fast N-dimensional function approximation\n\ntreeweave fits a scalar function of 1-N real variables once, then evaluates the\napproximation many times, very fast. It adaptively partitions the domain into a\nk-d tree of axis-aligned panels and fits a low-order polynomial on each\n(Chebyshev-sampled, stored in the monomial basis, evaluated by tensor-product\nHorner), refining only where the local error exceeds the requested tolerance.\nThe fit is the slow phase; evaluation is a branch-light descent plus an FMA-bound\nHorner kernel, batched via a counting sort by leaf.\n\nHighlights:\n- Header-only C++20 core (include/treeweave) plus a stable C ABI (libtreeweave_c)\n  with runtime SIMD dispatch (SSE->AVX-512, NEON) via xsimd, so one binary runs\n  across a whole CPU family.\n- Bindings for Python (nanobind), Julia, Fortran, and MATLAB/Octave, all layered\n  on the C ABI.\n- Built on polyfit (the per-panel fit) and POET (compile-time unrolling); the\n  fit/eval pipeline is original. Inspired by Robert Blackwell's baobzi; shares no\n  code with it.\n- CMake build with presets; broad compiler/OS CI matrix, static analysis\n  (clang-tidy + cppcheck under -Werror), sanitizers, Valgrind, coverage, CodSpeed\n  perf gating, and Sphinx/Doxygen docs.\n\nBSD-3-Clause. See docs/how-treeweave-works.md for the design and the math.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-10T16:20:50-04:00",
          "tree_id": "dc046e26cc73c631c596233a6cdc91598786ed1c",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/af763a154f2dd9334c5b3719b44585bfc41d6e50"
        },
        "date": 1781122954979,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.00065548675,
            "unit": "s/batch",
            "extra": "MdAPE=0.00538585301161996; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.00161710077777778,
            "unit": "s/batch",
            "extra": "MdAPE=0.00258059031804333; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.002658647875,
            "unit": "s/batch",
            "extra": "MdAPE=0.00490908255158845; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.001485254625,
            "unit": "s/batch",
            "extra": "MdAPE=0.00537878513526452; batch=65536 pts/call"
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
          "id": "72e08dc0ed3b2bb9e7357b6a660039926029eb0e",
          "message": "treeweave: fast N-dimensional function approximation\n\ntreeweave fits a scalar function of 1-N real variables once, then evaluates the\napproximation many times, very fast. It adaptively partitions the domain into a\nk-d tree of axis-aligned panels and fits a low-order polynomial on each\n(Chebyshev-sampled, stored in the monomial basis, evaluated by tensor-product\nHorner), refining only where the local error exceeds the requested tolerance.\nThe fit is the slow phase; evaluation is a branch-light descent plus an FMA-bound\nHorner kernel, batched via a counting sort by leaf.\n\nHighlights:\n- Header-only C++20 core (include/treeweave) plus a stable C ABI (libtreeweave_c)\n  with runtime SIMD dispatch (SSE->AVX-512, NEON) via xsimd, so one binary runs\n  across a whole CPU family.\n- Bindings for Python (nanobind), Julia, Fortran, and MATLAB/Octave, all layered\n  on the C ABI.\n- Built on polyfit (the per-panel fit) and POET (compile-time unrolling); the\n  fit/eval pipeline is original. Inspired by Robert Blackwell's baobzi; shares no\n  code with it.\n- CMake build with presets; broad compiler/OS CI matrix, static analysis\n  (clang-tidy + cppcheck under -Werror), sanitizers, Valgrind, coverage, CodSpeed\n  perf gating, and Sphinx/Doxygen docs.\n\nBSD-3-Clause. See docs/how-treeweave-works.md for the design and the math.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-10T16:31:39-04:00",
          "tree_id": "f59551e169fbee6b6db048625d147453cb22dde7",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/72e08dc0ed3b2bb9e7357b6a660039926029eb0e"
        },
        "date": 1781123539453,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000662746888888889,
            "unit": "s/batch",
            "extra": "MdAPE=0.00146882160798296; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.001139029,
            "unit": "s/batch",
            "extra": "MdAPE=0.0336385267229743; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.002011812875,
            "unit": "s/batch",
            "extra": "MdAPE=0.00209646673347925; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.001031018125,
            "unit": "s/batch",
            "extra": "MdAPE=0.00122964084844121; batch=65536 pts/call"
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
          "id": "1db5b6800107e94fb30c82c4fb928409a89e449f",
          "message": "treeweave: fast N-dimensional function approximation\n\ntreeweave fits a scalar function of 1-N real variables once, then evaluates the\napproximation many times, very fast. It adaptively partitions the domain into a\nk-d tree of axis-aligned panels and fits a low-order polynomial on each\n(Chebyshev-sampled, stored in the monomial basis, evaluated by tensor-product\nHorner), refining only where the local error exceeds the requested tolerance.\nThe fit is the slow phase; evaluation is a branch-light descent plus an FMA-bound\nHorner kernel, batched via a counting sort by leaf.\n\nHighlights:\n- Header-only C++20 core (include/treeweave) plus a stable C ABI (libtreeweave_c)\n  with runtime SIMD dispatch (SSE->AVX-512, NEON) via xsimd, so one binary runs\n  across a whole CPU family.\n- Bindings for Python (nanobind), Julia, Fortran, and MATLAB/Octave, all layered\n  on the C ABI.\n- Built on polyfit (the per-panel fit) and POET (compile-time unrolling); the\n  fit/eval pipeline is original. Inspired by Robert Blackwell's baobzi; shares no\n  code with it.\n- CMake build with presets; broad compiler/OS CI matrix, static analysis\n  (clang-tidy + cppcheck under -Werror), sanitizers, Valgrind, coverage, CodSpeed\n  perf gating, and Sphinx/Doxygen docs.\n\nBSD-3-Clause. See docs/how-treeweave-works.md for the design and the math.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-10T16:42:23-04:00",
          "tree_id": "17c8fd5e597a00c77e020c5181c07521dbb00460",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/1db5b6800107e94fb30c82c4fb928409a89e449f"
        },
        "date": 1781124255710,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.0006648782,
            "unit": "s/batch",
            "extra": "MdAPE=0.00105105358804116; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.00109812677777778,
            "unit": "s/batch",
            "extra": "MdAPE=0.0143755377535171; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.00201581588888889,
            "unit": "s/batch",
            "extra": "MdAPE=0.00159969684248693; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.00102598175,
            "unit": "s/batch",
            "extra": "MdAPE=0.00249862080704342; batch=65536 pts/call"
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
          "id": "284bd8a343f05ea12d42eb7a4d820ba6b6ed8b05",
          "message": "treeweave: public-header hygiene (version macros, noexcept, MSVC inline, NULL docs)\n\n- Version surface: TREEWEAVE_VERSION_{MAJOR,MINOR,PATCH}/_VERSION/_STRING\n  C macros in treeweave.h and inline constexpr version_{major,minor,patch}\n  in treeweave.hpp (all 0, mirroring project(VERSION 0.0.0)).\n- Legacy shim include/treeweave.hpp now pulls the public\n  <treeweave/treeweave.hpp> (a superset) instead of the detail impl header.\n- noexcept on the two allocation-free members: Function::memory_usage() and\n  the scalar operator()(const input_type&). The batch operator() allocates\n  and stays potentially-throwing.\n- compiler_macros.hpp: add an _MSC_VER branch mapping ALWAYS_INLINE to\n  __forceinline (FLATTEN stays empty); purely additive.\n- IWYU: treeweave.hpp directly includes detail/tol_kind.hpp (TolKind is used\n  in options).\n- treeweave.h: document C-ABI NULL-handle behavior per the actual code in\n  src/capi/treeweave.cpp -- eval/batch/sorted/transposed/eval_Nd and\n  memory_usage/print_stats/free are NULL-safe; dtype/input_dim/output_dim\n  dereference the handle unguarded and are documented \"must not be NULL\".\n- AUDIT.md: record applied items and the deferred follow-ups (iostream/\n  sstream removal, explicit Value conversions, c_binding_detail .inc rename).\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-10T19:05:23-04:00",
          "tree_id": "54a91f1f660f8961285a485a5bc8813fd1bb0bbd",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/284bd8a343f05ea12d42eb7a4d820ba6b6ed8b05"
        },
        "date": 1781132773216,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000666980666666667,
            "unit": "s/batch",
            "extra": "MdAPE=0.0107986669127363; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.00181038,
            "unit": "s/batch",
            "extra": "MdAPE=0.0135254865676912; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.00266369366666667,
            "unit": "s/batch",
            "extra": "MdAPE=0.0047874558463615; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.00151762822222222,
            "unit": "s/batch",
            "extra": "MdAPE=0.00540610857466723; batch=65536 pts/call"
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
          "id": "959407058b9451523a3cc53c74972ca3f7a87ad6",
          "message": "ci: fix pre-existing CI failures (shfmt, MATLAB link, module scan, install-smoke libm)\n\nFour failures inherited from 4671fa6, all now green locally:\n\n- Lint & format (shfmt): reformat .github/scripts/smoke_find_package.sh and\n  bindings/parity/run_parity.sh to spaces per the pinned shfmt hook + .editorconfig.\n- MATLAB bindings: bindings/matlab/CMakeLists.txt mixed keyword/plain\n  target_link_libraries on treeweave_mex_matlab (matlab_add_mex links the MATLAB\n  libs with the plain signature; our line used PRIVATE). Use the plain form, as\n  the adjacent comment already prescribes. Verified: configure + MEX build pass.\n- llvm-16 Debug/Release: clang-scan-deps-16 (system) rejected the PCH produced by\n  setup-cpp's clang (\"PCH file built from a different branch\"). treeweave uses no\n  C++20 modules, so disable the module dependency scan\n  (CMAKE_CXX_SCAN_FOR_MODULES OFF) in the toolchain — removes the scan and the clash.\n- install-smoke find_package consumer: main.c uses libm (exp) but linked only\n  treeweave::treeweave_c; modern ld --as-needed needs explicit libm. Add\n  find_library(MATH_LIBRARY m) linkage. (Latent: the job is skipped on the\n  workflow_run trigger, so it first surfaced on a push.)\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-10T19:29:06-04:00",
          "tree_id": "205710693bac8292d1726e8ba1ab3885c0da22bc",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/959407058b9451523a3cc53c74972ca3f7a87ad6"
        },
        "date": 1781134191595,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000535915555555556,
            "unit": "s/batch",
            "extra": "MdAPE=0.00120830593850779; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.001059591,
            "unit": "s/batch",
            "extra": "MdAPE=0.00360561355220056; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.00184179166666667,
            "unit": "s/batch",
            "extra": "MdAPE=0.00255457155630911; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.00100489211111111,
            "unit": "s/batch",
            "extra": "MdAPE=0.00632977899162297; batch=65536 pts/call"
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
          "id": "d04633db360d0e97c94d6eada20f35c138ab7d94",
          "message": "ci: green up Lint (clang-format macro, gersemi) and MATLAB licensing fallback\n\nSurfaced once the earlier lint hooks / build steps went green:\n\n- clang-format (pinned clang-format==22): the new TREEWEAVE_VERSION macro's\n  line-continuation was unformatted. Make it a single line (117 < 120 col limit).\n- gersemi==0.27.1 (full-files): two pre-existing files were not gersemi-formatted\n  (treeweave_c_api.cmake, treeweave_install.cmake) -- masked until now because\n  bash -e stopped the lint step at shfmt/clang-format. Reformat (cosmetic only).\n- MATLAB: the CMake link fix made configure + MEX build pass; the test then hit\n  \"MathWorks Licensing Error 1\". setup-matlab@v3 hosted licensing isn't reliably\n  granted to bare `matlab -batch`. Keep the build as the hard gate; mark the\n  license-gated suite continue-on-error and rely on the Octave job (identical\n  matlab_treeweave test) as the blocking runtime gate.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-10T20:11:40-04:00",
          "tree_id": "48cc54c59636c472904008a61c25532c5b581dcf",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/d04633db360d0e97c94d6eada20f35c138ab7d94"
        },
        "date": 1781136739034,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000663694111111111,
            "unit": "s/batch",
            "extra": "MdAPE=0.00327251003808396; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.00128990066666667,
            "unit": "s/batch",
            "extra": "MdAPE=0.00285108632150592; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.00202319022222222,
            "unit": "s/batch",
            "extra": "MdAPE=0.000916396632711391; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.001027934,
            "unit": "s/batch",
            "extra": "MdAPE=0.00359819345065628; batch=65536 pts/call"
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
          "id": "6f40b7926a5ef4388da0bd905d397d3525ec6679",
          "message": "ci+docs: green up Lint (clang-format, gersemi), MATLAB licensing fallback, MyST xref\n\nSurfaced once the earlier lint hooks / build steps went green:\n\n- clang-format (pinned clang-format==22): the new TREEWEAVE_VERSION macro's\n  line-continuation was unformatted. Make it a single line (117 < 120 col limit).\n- gersemi==0.27.1 (full-files): two pre-existing files were not gersemi-formatted\n  (treeweave_c_api.cmake, treeweave_install.cmake) -- masked until now because\n  bash -e stopped the lint step at shfmt/clang-format. Reformat (cosmetic only).\n- MATLAB: the CMake link fix made configure + MEX build pass; the test then hit\n  \"MathWorks Licensing Error 1\". setup-matlab@v3 hosted licensing isn't reliably\n  granted to bare `matlab -batch`. Keep the build as the hard gate; mark the\n  license-gated suite continue-on-error and rely on the Octave job (identical\n  matlab_treeweave test) as the blocking runtime gate.\n- docs: set myst_heading_anchors=3 so in-page `[text](#slug)` links resolve\n  (fixes the \"background-and-further-reading\" xref). `sphinx -W` now builds clean.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-11T10:16:41-04:00",
          "tree_id": "49954a138a8b03ca36afafe5c49e08ef27818a84",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/6f40b7926a5ef4388da0bd905d397d3525ec6679"
        },
        "date": 1781187520330,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.00065987425,
            "unit": "s/batch",
            "extra": "MdAPE=0.0027468723246678; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.00108875133333333,
            "unit": "s/batch",
            "extra": "MdAPE=0.00622968240688305; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.00201759722222222,
            "unit": "s/batch",
            "extra": "MdAPE=0.00314422151583362; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.001030117,
            "unit": "s/batch",
            "extra": "MdAPE=0.00114980779650744; batch=65536 pts/call"
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
          "id": "0579da91b5fdbb7ad91412a5588eff8c1c0a0f84",
          "message": "docs: define batch/sorted eval routes; document from-source + from-release installs\n\n- README + Python/Julia/MATLAB guides: the eval-route comments said only\n  \"1-D ascending fast path\" / \"batch\", which define nothing. Spell them out:\n  batch = many points in one call, any order; sorted = a batch the caller\n  promises is non-decreasing (x[i] <= x[i+1]), 1-D only, ~3-4x faster.\n- install.rst / Julia guide: every language now shows BOTH a from-source path\n  (works today, what CI exercises) and a from-release path (prebuilt). Added an\n  honest note that no v* release is published yet, so the release paths (PyPI,\n  Julia Pkg.add prebuilt download, C/Fortran release archives) are not live —\n  Julia's documented Pkg.add fell straight through to download_prebuilt() and\n  could not succeed without a release. Documented the Julia/Python from-source\n  builds (sibling build dir / LIBTREEWEAVE_C; pip install ./bindings/python).\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-11T10:50:43-04:00",
          "tree_id": "9e7c0dc7c8baaa4727dcef11f825ee6b11278d52",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/0579da91b5fdbb7ad91412a5588eff8c1c0a0f84"
        },
        "date": 1781189748239,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000661779666666667,
            "unit": "s/batch",
            "extra": "MdAPE=0.00227171404280142; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.00111204311111111,
            "unit": "s/batch",
            "extra": "MdAPE=0.00870813370590526; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.002027937875,
            "unit": "s/batch",
            "extra": "MdAPE=0.00650344978706699; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.00103123966666667,
            "unit": "s/batch",
            "extra": "MdAPE=0.00685236003676305; batch=65536 pts/call"
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
          "id": "d9fadc76c5e6adcd31626366c94abdf5d81bb090",
          "message": "ci: gated one-click release automation (mirror simdref)\n\nReplace the three tag-triggered release workflows with a manual, fully\ngated release button modeled on DiamonDinoia/simdref:\n\n- scripts/bump_version.py + check_version_sync.py keep the 4 hand-synced\n  version files (CMakeLists, treeweave.h, treeweave.hpp, Julia Project.toml)\n  in lockstep; the wheel version follows CMakeLists via scikit-build-core.\n- bump-version.yml: dispatchable version bump + commit to main.\n- release.yml: orchestrator. preflight (version sync + tag/PyPI guards +\n  green ci/bindings/install-smoke/testpypi on HEAD) -> reusable C-ABI +\n  wheel/sdist builders -> smoke against real artifacts (Python/C/C++/\n  Fortran/Julia/Octave; MATLAB best-effort) -> atomic publish-and-tag with\n  rollback -> GitHub Release from CHANGELOG slice -> post-publish Julia\n  prebuilt-download verify. v<version> tag is created only after publish.\n- _build-c-abi.yml + wheels.yml: reusable workflow_call builders.\n- pypi.yml: deleted (draft publisher; publishing now lives in release.yml).\n- CHANGELOG.md: Keep a Changelog format with [0.0.0] section.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-11T11:28:52-04:00",
          "tree_id": "e16efd7fb1a570a7f1de1e249c10be60b750e295",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/d9fadc76c5e6adcd31626366c94abdf5d81bb090"
        },
        "date": 1781191878962,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.00054067675,
            "unit": "s/batch",
            "extra": "MdAPE=0.00631754413465958; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.00107380325,
            "unit": "s/batch",
            "extra": "MdAPE=0.00288393635890388; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.00184628575,
            "unit": "s/batch",
            "extra": "MdAPE=0.0010120470329353; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.001017781,
            "unit": "s/batch",
            "extra": "MdAPE=0.00278688481518332; batch=65536 pts/call"
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
          "id": "977b8dde8b58c02e7829a09ee5cc1d62d4f744e5",
          "message": "ci: gated one-click release automation (mirror simdref)\n\nReplace the three tag-triggered release workflows with a manual, fully\ngated release button modeled on DiamonDinoia/simdref:\n\n- scripts/bump_version.py + check_version_sync.py keep the 4 hand-synced\n  version files (CMakeLists, treeweave.h, treeweave.hpp, Julia Project.toml)\n  in lockstep; the wheel version follows CMakeLists via scikit-build-core.\n- bump-version.yml: dispatchable version bump + commit to main.\n- release.yml: orchestrator. preflight (version sync + tag/PyPI guards +\n  green ci/bindings/install-smoke/testpypi on HEAD) -> reusable C-ABI +\n  wheel/sdist builders -> smoke against real artifacts (Python/C/C++/\n  Fortran/Julia/Octave; MATLAB best-effort) -> atomic publish-and-tag with\n  rollback -> GitHub Release from CHANGELOG slice -> post-publish Julia\n  prebuilt-download verify. v<version> tag is created only after publish.\n- _build-c-abi.yml + wheels.yml: reusable workflow_call builders.\n- pypi.yml: deleted (draft publisher; publishing now lives in release.yml).\n- CHANGELOG.md: Keep a Changelog format with [0.0.0] section.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-11T12:29:36-04:00",
          "tree_id": "edc857f9b3cbd6fc704f2a41b0ed13da9c2b6a4c",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/977b8dde8b58c02e7829a09ee5cc1d62d4f744e5"
        },
        "date": 1781195491744,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000664590875,
            "unit": "s/batch",
            "extra": "MdAPE=0.00403475615750553; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.00108967977777778,
            "unit": "s/batch",
            "extra": "MdAPE=0.00488665984181249; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.002023712875,
            "unit": "s/batch",
            "extra": "MdAPE=0.00306561408005072; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.00102565555555556,
            "unit": "s/batch",
            "extra": "MdAPE=0.000964147330755774; batch=65536 pts/call"
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
          "id": "58d9dee91824e78785746eb2aca94018970e85f2",
          "message": "treeweave 0.0.0 — adaptive N-dimensional polynomial-tree function approximation\n\nHeader-only C++ API (treeweave::treeweave), a relocatable C ABI\n(libtreeweave_c, find_package(treeweave)), and Python, Julia, Fortran,\nMATLAB, and Octave bindings. x86-64 builds dispatch SSE4.2 / AVX2 / AVX-512\nat runtime; aarch64 uses the NEON baseline.\n\nCI covers Linux/macOS/Windows on x86-64 + aarch64, the language-wrapper test\nsuites, install-recipe smoke tests (find_package, FetchContent, pip), and a\nTestPyPI dry-run on every push to main.\n\nRelease is a gated, human-dispatched one-click workflow (mirrors\nDiamonDinoia/simdref): scripts/bump_version.py + check_version_sync.py keep\nthe four hand-synced version files in lockstep, and release.yml builds the\nC-ABI tarballs + wheels + sdist, smoke-tests them against the real artifacts,\nthen publishes to PyPI and pushes the v<version> tag only after every gate is\ngreen (with tag rollback on publish failure). Relocatable tarballs install to\nlib/ so find_package works on Debian/Ubuntu as well as RHEL.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-11T12:47:55-04:00",
          "tree_id": "942f55aea9eb521a6822403ee4e085a2f0978afa",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/58d9dee91824e78785746eb2aca94018970e85f2"
        },
        "date": 1781196601419,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000665303,
            "unit": "s/batch",
            "extra": "MdAPE=0.00442922828441104; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.001103488,
            "unit": "s/batch",
            "extra": "MdAPE=0.011520242388613; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.0020179335,
            "unit": "s/batch",
            "extra": "MdAPE=0.00187691465051924; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.00103016966666667,
            "unit": "s/batch",
            "extra": "MdAPE=0.00194922897734532; batch=65536 pts/call"
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
          "id": "74eebaa04b580781afb8c29897e8b24aad6f6f0d",
          "message": "treeweave 0.0.0 — adaptive N-dimensional polynomial-tree function approximation\n\nHeader-only C++ API (treeweave::treeweave), a relocatable C ABI\n(libtreeweave_c, find_package(treeweave)), and Python, Julia, Fortran,\nMATLAB, and Octave bindings. x86-64 builds dispatch SSE4.2 / AVX2 / AVX-512\nat runtime; aarch64 uses the NEON baseline.\n\nCI covers Linux/macOS/Windows on x86-64 + aarch64, the language-wrapper test\nsuites, install-recipe smoke tests (find_package, FetchContent, pip), and a\nTestPyPI dry-run on every push to main.\n\nRelease is a gated, human-dispatched one-click workflow (mirrors\nDiamonDinoia/simdref): scripts/bump_version.py + check_version_sync.py keep\nthe four hand-synced version files in lockstep, and release.yml builds the\nC-ABI tarballs + wheels + sdist, smoke-tests them against the real artifacts,\nthen publishes to PyPI and pushes the v<version> tag only after every gate is\ngreen (with tag rollback on publish failure). Relocatable tarballs install to\nlib/ so find_package works on Debian/Ubuntu as well as RHEL.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-11T13:36:26-04:00",
          "tree_id": "bb61a303c46b26c6c5afac8957e1ef8db96a26be",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/74eebaa04b580781afb8c29897e8b24aad6f6f0d"
        },
        "date": 1781199548564,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000661753375,
            "unit": "s/batch",
            "extra": "MdAPE=0.00703034467121209; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.00163010133333333,
            "unit": "s/batch",
            "extra": "MdAPE=0.00730743411901401; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.00265526177777778,
            "unit": "s/batch",
            "extra": "MdAPE=0.00176055841712462; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.00200497355555556,
            "unit": "s/batch",
            "extra": "MdAPE=0.00988574495955539; batch=65536 pts/call"
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
          "id": "f9e738eea77c21b27f279b708e76d689f9755478",
          "message": "treeweave 0.0.0 — adaptive N-dimensional polynomial-tree function approximation\n\nHeader-only C++ API (treeweave::treeweave), a relocatable C ABI\n(libtreeweave_c, find_package(treeweave)), and Python, Julia, Fortran,\nMATLAB, and Octave bindings. x86-64 builds dispatch SSE4.2 / AVX2 / AVX-512\nat runtime; aarch64 uses the NEON baseline.\n\nCI covers Linux/macOS/Windows on x86-64 + aarch64, the language-wrapper test\nsuites, install-recipe smoke tests (find_package, FetchContent, pip), and a\nTestPyPI dry-run on every push to main.\n\nRelease is a gated, human-dispatched one-click workflow (mirrors\nDiamonDinoia/simdref): scripts/bump_version.py + check_version_sync.py keep\nthe four hand-synced version files in lockstep, and release.yml builds the\nC-ABI tarballs + wheels + sdist, smoke-tests them against the real artifacts,\nthen publishes to PyPI and pushes the v<version> tag only after every gate is\ngreen (with tag rollback on publish failure). Relocatable tarballs install to\nlib/ so find_package works on Debian/Ubuntu as well as RHEL.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-11T14:51:01-04:00",
          "tree_id": "2d0a07269386c983fe0c84e4d4959a9bb21af5c5",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/f9e738eea77c21b27f279b708e76d689f9755478"
        },
        "date": 1781204094942,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000660475,
            "unit": "s/batch",
            "extra": "MdAPE=0.00305007865812482; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.0016490481,
            "unit": "s/batch",
            "extra": "MdAPE=0.0031324478681552; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.00266181411111111,
            "unit": "s/batch",
            "extra": "MdAPE=0.00332581676400903; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.00149097077777778,
            "unit": "s/batch",
            "extra": "MdAPE=0.00341596104549278; batch=65536 pts/call"
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
          "id": "815ea758b89dad660056b22ec4d8e376c5814e95",
          "message": "treeweave 0.0.0 — adaptive N-dimensional polynomial-tree function approximation\n\nHeader-only C++ API (treeweave::treeweave), a relocatable C ABI\n(libtreeweave_c, find_package(treeweave)), and Python, Julia, Fortran,\nMATLAB, and Octave bindings. x86-64 builds dispatch SSE4.2 / AVX2 / AVX-512\nat runtime; aarch64 uses the NEON baseline.\n\nCI covers Linux/macOS/Windows on x86-64 + aarch64, the language-wrapper test\nsuites, install-recipe smoke tests (find_package, FetchContent, pip), and a\nTestPyPI dry-run on every push to main.\n\nRelease is a gated, human-dispatched one-click workflow (mirrors\nDiamonDinoia/simdref): scripts/bump_version.py + check_version_sync.py keep\nthe four hand-synced version files in lockstep, and release.yml builds the\nC-ABI tarballs + wheels + sdist, smoke-tests them against the real artifacts,\nthen publishes to PyPI and pushes the v<version> tag only after every gate is\ngreen (with tag rollback on publish failure). Relocatable tarballs install to\nlib/ so find_package works on Debian/Ubuntu as well as RHEL.\n\nThis tree includes a full cross-surface hardening pass and a cross-language\nbenchmark:\n- Unified out-of-domain contract — a finite x > b returns NaN on every eval\n  path (scalar, batch, sorted, leaf-table, descent), implemented branchlessly\n  (positive-logic SIMD domain mask + unsigned-min clamp), with no f64\n  throughput regression.\n- Fixes for float->int cast UB on non-finite inputs and the aarch64\n  fcvtzs(NaN)=0 mis-classification; C-ABI NULL-safety; Julia GC.@preserve and\n  max_memory_mib default; by-value Fortran eval interfaces; mwrap stub prefix\n  bz_ -> tw_.\n- Build/CI hardening: CPM bootstrap fixed on reconfigure, xsimd SHA-pinned,\n  GitHub Actions SHA-pinned, workflow permissions scoped.\n- lgamma benchmark on [3, 50) comparing the native log-gamma against treeweave\n  in C, C++, Fortran, Python, Julia, and MATLAB/Octave (max relative error,\n  throughput, speedup).\n\nVerified: C++/C-ABI release 74/74 and ASan/UBSan 64/64; Python, Julia,\nFortran, and Octave bindings green; all six lgamma benches at ~1e-10 max\nrelative error.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-12T11:11:48-04:00",
          "tree_id": "1d10c6c806647dc1b0ed39c8b0b2ee60ae5b3b3f",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/815ea758b89dad660056b22ec4d8e376c5814e95"
        },
        "date": 1781277683807,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000668458125,
            "unit": "s/batch",
            "extra": "MdAPE=0.00391733858650226; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.00122425822222222,
            "unit": "s/batch",
            "extra": "MdAPE=0.0145280042193105; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.00217106577777778,
            "unit": "s/batch",
            "extra": "MdAPE=0.00171148723881548; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.00112831588888889,
            "unit": "s/batch",
            "extra": "MdAPE=0.0045094317524455; batch=65536 pts/call"
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
          "id": "70f62d38ea7e766b2444ce1c2cc700cb8926a41b",
          "message": "treeweave 0.0.0 — adaptive N-dimensional polynomial-tree function approximation\n\nHeader-only C++ API (treeweave::treeweave), a relocatable C ABI\n(libtreeweave_c, find_package(treeweave)), and Python, Julia, Fortran,\nMATLAB, and Octave bindings. x86-64 builds dispatch SSE4.2 / AVX2 / AVX-512\nat runtime; aarch64 uses the NEON baseline.\n\nCI covers Linux/macOS/Windows on x86-64 + aarch64, the language-wrapper test\nsuites, install-recipe smoke tests (find_package, FetchContent, pip), and a\nTestPyPI dry-run on every push to main.\n\nRelease is a gated, human-dispatched one-click workflow (mirrors\nDiamonDinoia/simdref): scripts/bump_version.py + check_version_sync.py keep\nthe four hand-synced version files in lockstep, and release.yml builds the\nC-ABI tarballs + wheels + sdist, smoke-tests them against the real artifacts,\nthen publishes to PyPI and pushes the v<version> tag only after every gate is\ngreen (with tag rollback on publish failure). Relocatable tarballs install to\nlib/ so find_package works on Debian/Ubuntu as well as RHEL.\n\nThis tree includes a full cross-surface hardening pass and a cross-language\nbenchmark:\n- Unified out-of-domain contract — a finite x > b returns NaN on every eval\n  path (scalar, batch, sorted, leaf-table, descent), implemented branchlessly\n  (positive-logic SIMD domain mask + unsigned-min clamp), with no f64\n  throughput regression.\n- Fixes for float->int cast UB on non-finite inputs and the aarch64\n  fcvtzs(NaN)=0 mis-classification; C-ABI NULL-safety; Julia GC.@preserve and\n  max_memory_mib default; by-value Fortran eval interfaces; mwrap stub prefix\n  bz_ -> tw_.\n- Build/CI hardening: CPM bootstrap fixed on reconfigure, xsimd SHA-pinned,\n  GitHub Actions SHA-pinned, workflow permissions scoped.\n- lgamma benchmark on [3, 50) comparing the native log-gamma against treeweave\n  in C, C++, Fortran, Python, Julia, and MATLAB/Octave (max relative error,\n  throughput, speedup).\n\nVerified: C++/C-ABI release 74/74 and ASan/UBSan 64/64; Python, Julia,\nFortran, and Octave bindings green; all six lgamma benches at ~1e-10 max\nrelative error.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-12T12:01:51-04:00",
          "tree_id": "96843ba7e9ebaad5100bee761bb44bd8d782c977",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/70f62d38ea7e766b2444ce1c2cc700cb8926a41b"
        },
        "date": 1781280161286,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000659788,
            "unit": "s/batch",
            "extra": "MdAPE=0.00520605042057785; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.001925263625,
            "unit": "s/batch",
            "extra": "MdAPE=0.00443558449711628; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.0028392981,
            "unit": "s/batch",
            "extra": "MdAPE=0.00223330111091463; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.00170777311111111,
            "unit": "s/batch",
            "extra": "MdAPE=0.00334577155157178; batch=65536 pts/call"
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
          "id": "963c3f591db58f715bf045b78217a064b7057984",
          "message": "Add cross-language lgamma benchmark showcase (speedup charts)\n\nExtend the six lgamma benchmarks (C, C++, Fortran, Python, Julia,\nOctave) to time three eval modes — single-eval (scalar), multi-eval\n(unsorted batch), and sorted-eval (1-D ascending fast path) — and to\nemit a per-language YAML document when TREEWEAVE_BENCH_YAML names a\npath. Prose stdout and ctest behavior are unchanged (the var is never\nset under ctest).\n\nAdd a zero-copy + out= in-place evaluation API to the Python and Julia\nbindings: results are written directly into a caller-supplied buffer,\ndropping the intermediate std::vector/copy. MATLAB/Octave deliberately\ndoes not offer out= (copy-on-write makes caller-owned writes unsafe);\nthis is documented.\n\nAdd scripts/generate_benchmark_plot.py (matplotlib -> three SVG speedup\ncharts from the YAML files, styled after poet) and\n.github/workflows/benchmark-showcase.yml (a per-language CI matrix whose\naggregate job force-pushes the charts to an orphaned benchmark-results\nbranch). Embed the three charts in README.md and docs/guides/performance.rst\nvia raw.githubusercontent.com URLs.\n\nNote: the embedded chart URLs resolve only after the first\nbenchmark-showcase run creates the benchmark-results branch.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-12T15:34:53-04:00",
          "tree_id": "d851940d359081bcb752e7ae4951cd3b85f52527",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/963c3f591db58f715bf045b78217a064b7057984"
        },
        "date": 1781293968280,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.000682834125,
            "unit": "s/batch",
            "extra": "MdAPE=0.0229930083989702; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.00193059766666667,
            "unit": "s/batch",
            "extra": "MdAPE=0.00588693705490061; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.00283170988888889,
            "unit": "s/batch",
            "extra": "MdAPE=0.00140894535899534; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.001692679,
            "unit": "s/batch",
            "extra": "MdAPE=0.00385052060103055; batch=65536 pts/call"
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
          "id": "cc8cc6f00661b81061217d09970cb53621daa916",
          "message": "Benchmark showcase (zeta) + JS/TS binding + version API\n\nReplace the cross-language benchmark's lgamma target with a dependency-free\nRiemann-zeta partial sum zeta_N(s) = sum_{k=1..N} k^-s (N=1e6) fit on [2, 10]:\na genuinely expensive function (N pow()s per eval), so a fitted polynomial eval\nbeats recomputing the sum decisively in every mode — including scalar. zeta is\nnow the quick-start example in all seven languages. lgamma and its bench-only\ndeps (scipy, pyyaml, SpecialFunctions, @stdlib/gammaln) are removed.\n\n- Charts: horizontal grouped Mevals/s bars (treeweave vs native, log x,\n  languages on y). README embeds the three throughput charts\n  (single/multi/sorted); the perf guide adds latency + sorted-vs-unsorted.\n- JS/TS binding (bindings/js): N-API addon + WASM, TS layer, node:test suite\n  (9/9), zeta example. CMake TREEWEAVE_BUILD_JS + bindings-js / bindings-js-wasm\n  presets; bindings.yml gains a `js` job (native + WASM).\n- Version API: generated <treeweave_version.h> (single source of truth =\n  project(VERSION)), runtime treeweave_version()/_string(), C++\n  treeweave::version_*, TREEWEAVE_VERSION_AT_LEAST macro.\n- benchmark-showcase.yml: zeta benches, fixed julia/octave legs, `js` leg.\n- GitHub Actions referenced by tag, not commit SHA.\n\nCo-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>",
          "timestamp": "2026-06-12T17:27:00-04:00",
          "tree_id": "feedce2614b69b10f35cb3e821c654845078e93b",
          "url": "https://github.com/DiamonDinoia/treeweave/commit/cc8cc6f00661b81061217d09970cb53621daa916"
        },
        "date": 1781299682293,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "eval/1d/runge/f64",
            "value": 0.0006706152,
            "unit": "s/batch",
            "extra": "MdAPE=0.00780521216879591; batch=65536 pts/call"
          },
          {
            "name": "eval/2d/bump/f64",
            "value": 0.001943212375,
            "unit": "s/batch",
            "extra": "MdAPE=0.00538760190818233; batch=65536 pts/call"
          },
          {
            "name": "eval/3d/smooth/f64",
            "value": 0.00283853944444444,
            "unit": "s/batch",
            "extra": "MdAPE=0.00624470809647054; batch=65536 pts/call"
          },
          {
            "name": "eval/2d->3d/vector/f64",
            "value": 0.001569795,
            "unit": "s/batch",
            "extra": "MdAPE=0.0251693158427711; batch=65536 pts/call"
          }
        ]
      }
    ]
  }
}