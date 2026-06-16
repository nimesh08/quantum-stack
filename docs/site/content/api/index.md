# API reference

Three surfaces, three doc generators, one navigable site.

<div class="grid cards" markdown>

-   :material-api:{ .lg .middle } **[REST API](rest/index.md)**

    ---

    Auto-generated from FastAPI's OpenAPI schema by
    [Redocly CLI 2.32.2](https://redocly.com/docs/cli/). Every endpoint
    with method, path, parameters, request/response schemas, status
    codes, and a "try it" panel for live calls.

-   :material-language-python:{ .lg .middle } **[Python (jobsvc)](python/jobsvc/index.md)**

    ---

    Auto-generated from docstrings by
    [mkdocstrings 1.0.4](https://mkdocstrings.github.io/) using the
    griffe AST walker. Every public class, function, dataclass, and
    method with parameters / returns / raises / examples.

-   :material-language-python:{ .lg .middle } **[Python (calibration)](python/calibration/index.md)**

    ---

    Same generator. Provider base class, the four shipped providers,
    the atomic writer, the drift detector, and the scheduler entry
    point.

-   :material-language-typescript:{ .lg .middle } **[TypeScript (playground)](typescript/index.md)**

    ---

    Auto-generated from JSDoc + TS types by
    [typedoc-plugin-markdown 4.12.0](https://typedoc-plugin-markdown.org/).
    The `api` client, the hooks, the components.

</div>

## Navigation

Every reference page links upstream:

- **Source link** — every symbol carries a "view on GitHub" pointer.
- **Cross-references** — type annotations on parameters / returns are
  linked to their definitions inside the same site.
- **"On this page"** sidebar — drill down within a long module.

## Search

The site has full-text search covering every doc page **and** every
auto-generated symbol page. Hit ++slash++ or click the search box.

## Versioning

This site is built from `main` on every push. For older versions,
check out the corresponding tag on
[GitHub](https://github.com/nimesh08/quantum-stack) and run
`mkdocs serve` from `docs/site/` locally.
