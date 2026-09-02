.. _releasing:

Cutting a release
=================

This page is the maintainer's procedure. Every step names the workflow that
does the work and the input it takes. Nothing here is run by hand on a laptop:
the release is four ``workflow_dispatch`` clicks and two waits.

The ``VERSION`` file is the source of truth; the Julia ``Project.toml`` and the
JS ``package.json`` carry hand-synced copies, and the wheel version is derived
from ``VERSION``. ``scripts/check_sync.py`` refuses a release whose three
files disagree, and the ``Bump Version`` workflow is what keeps them in step. The
same script checks every other hand-copied fact -- the ``tol_kind`` and dtype
enumerators in each binding, and the MATLAB copy of the fit-option defaults --
and pre-commit runs it on every commit.

1. Preconditions on ``main``
----------------------------

- ``CHANGELOG.md`` has a ``## [X.Y.Z]`` section. The ``Release`` workflow copies
  that section verbatim into the GitHub Release notes, and falls back to
  generated notes when the section is missing.
- CI is green on the commit to be released.

2. Bump the version
-------------------

Dispatch ``Bump Version`` with ``version=X.Y.Z`` and ``dry_run=true``, read the
diff it prints, then dispatch it again with ``dry_run=false``. It runs
``scripts/bump_version.py`` on a fresh ``main`` checkout, rewrites the three
version-bearing files, and pushes the commit to ``main``.
``include/treeweave_version.h`` is generated from ``VERSION`` on every CMake
configure, so it is not one of them.

3. Wait for the fast gate on the new HEAD
-----------------------------------------

The ``Release`` workflow refuses to start unless each of these has a green
run on the exact commit: ``bindings.yml`` and ``install-smoke.yml``. The
authoritative list is
the one ``release.yml``'s ``preflight`` job iterates. They take about ten
minutes between them.

``ci.yml`` and ``testpypi.yml`` also run on the push but do not gate the
release, because the release pipeline rebuilds and smoke-tests every shipped
artifact itself. ``testpypi.yml`` publishes ``X.Y.Z.devN`` to TestPyPI, which is
the dry run for the PyPI publish, and it drives ``install-smoke.yml``'s
``pip-testpypi`` job: the ``pip install --index-url .../test.pypi.org/...``
command the Python guide prints, run against that staging wheel.

4. Release dry run
------------------

Dispatch ``Release`` with ``version=X.Y.Z`` and ``dry_run=true``. It builds
every artifact and smoke-tests each one against the artifact it just built:

- the C-ABI tarball per platform (``_build-c-abi.yml``), plus the
  arch-independent C++ header bundle
- the wheel matrix and the sdist (``wheels.yml``)
- the MATLAB MEX bundles (``_build-mex.yml``, best-effort, never gating)
- the Node prebuilds and the WASM package
- the docs consume recipes: ``smoke-cxx`` runs the ``c-tarball`` and
  ``cxx-headers`` recipes from ``tools/ci/docs-recipes.sh`` against the tarball
  and the header bundle this run built, so the install commands the docs print
  are proved on the real artifacts *before* anything is published

A dry run publishes nothing, pushes no tag and creates no release. Fix and
repeat until it is green.

5. Release
----------

Dispatch ``Release`` with ``version=X.Y.Z`` and ``dry_run=false``. The order is
deliberate, so that the ``vX.Y.Z`` tag means "this release is complete":

1. every build and smoke job from step 4 runs again
2. the ``vX.Y.Z`` tag is pushed, then the wheels and sdist go to PyPI. A failed
   PyPI publish deletes the tag again, keeping the tag and the release in step
3. the GitHub Release is created with the CHANGELOG section as its body and
   every artifact attached, each platform archive under both its versioned and
   its unversioned name, because the docs quote the unversioned floating URL
4. the ``stable`` branch is force-moved to the released commit. It is the ref
   the ``FetchContent`` and CPM recipes name, so it moves only after the
   release exists
5. the npm package is published from the same build
6. ``julia-smoke.yml`` is dispatched on the new tag

6. Post-publish checks
----------------------

Two workflows run on their own and both must go green:

- ``release-install.yml``, on ``release: published``. It runs the docs download
  and registry lines verbatim against ``releases/latest``, PyPI and npm: the
  floating ``curl`` URLs, ``pip install treeweave`` and
  ``npm install @flatironinstitute/treeweave``. These are the only documented
  commands no pre-release check can cover, because they need the release to
  exist. It also runs weekly, so a later breakage in PyPI, npm or the release
  assets surfaces between releases.
- ``julia-smoke.yml``, on the tag, which exercises the ``Pkg.add`` path that
  downloads the prebuilt ``libtreeweave_c`` from the release.

A failure here is a docs defect or an asset-name defect, not a build defect.
A published PyPI version is immutable, so the fix ships as the next patch
release.

7. Docs
-------

``docs.yml`` publishes the site from the ``main`` push in step 2. There is
nothing to do.

Where the printed commands are checked
--------------------------------------

Every shell recipe the docs print is a marked region of
``tools/ci/docs-recipes.sh``, and ``scripts/check_docs_code.py`` refuses a
region that no page embeds or that no workflow runs. The three checkpoints:

.. list-table::
   :header-rows: 1
   :widths: 26 74

   * - When
     - What runs
   * - every pull request
     - ``install-smoke.yml`` runs the recipes that build or consume the
       checkout: the clone, the presets, the install prefix, the one-flag
       compile, the dispatch flags, the quick-start projects. Each binding's
       own workflow runs its source-build recipe on the Linux leg.
   * - release dry run
     - ``release.yml``'s ``smoke-cxx`` runs the consume half of the tarball
       recipes against the artifacts the run built.
   * - after publishing
     - ``release-install.yml`` runs the download and registry lines verbatim
       against the published release.
