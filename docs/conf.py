import os

# Project information
project = "treeweave"
copyright = "2025-2026, Marco Barbone and the treeweave authors"
author = "Marco Barbone"

# Extensions
extensions = [
    "breathe",
    "exhale",
    "myst_parser",
    "sphinx_rtd_theme",
]

# Render `$...$` / `$$...$$` math in the MyST (Markdown) design notes.
myst_enable_extensions = ["dollarmath", "amsmath"]

# Generate slug anchors for headings (h1-h3) so in-page `[text](#slug)` links
# resolve: e.g. the "see [Background](#background-and-further-reading)" link.
myst_heading_anchors = 3

# Breathe configuration. The Doxygen XML dir is passed by CMake via the
# DOXYGEN_XML_OUTPUT env var (see the `sphinx` target in cmake/dev_helpers.cmake);
# fall back to the in-tree build/ path for a manual `sphinx-build`.
doxygen_xml = os.environ.get("DOXYGEN_XML_OUTPUT", "../build/docs/xml")
breathe_projects = {"treeweave": os.path.abspath(doxygen_xml)}
breathe_default_project = "treeweave"

# Exhale configuration: generates the API tree from the Breathe project.
exhale_args = {
    "containmentFolder": "./api",
    "rootFileName": "library_root.rst",
    "doxygenStripFromPath": os.path.abspath(".."),
    "rootFileTitle": "treeweave API Reference",
    "createTreeView": True,
    "exhaleExecutesDoxygen": False,
    # Keep generated pages focused on the public API.
    "listingExclude": [
        r".*::detail::.*",
        r".*\bdetail\b.*",
    ],
    # Avoid brittle overload-resolution pages from generated function entries.
    "unabridgedOrphanKinds": [
        "namespace",
        "class",
        "struct",
        "enum",
        "typedef",
        "variable",
    ],
}

# Theme
html_theme = "sphinx_rtd_theme"
html_theme_options = {
    "navigation_depth": 4,
    "collapse_navigation": False,
    "sticky_navigation": True,
}

# Ship a monospace font with full box-drawing coverage so the ASCII-art
# diagrams stay aligned (see docs/_static/custom.css).
html_static_path = ["_static"]
html_css_files = ["custom.css"]

# RTD theme reads these to render the "Edit on GitHub" link on every page.
html_context = {
    "display_github": True,
    "github_user": "DiamonDinoia",
    "github_repo": "treeweave",
    "github_version": "main",
    "conf_py_path": "/docs/",
}

# Breathe/Exhale emit false-positive warnings for heavily-templated APIs due to
# parser limitations; silence the known-noisy categories so a clean build
# reflects real problems.
suppress_warnings = [
    "docutils",
    "cpp.duplicate_declaration",
    "toc.not_included",
]

# Treat .md as MyST so the design note ports over without conversion.
source_suffix = {
    ".rst": "restructuredtext",
    ".md": "markdown",
}
