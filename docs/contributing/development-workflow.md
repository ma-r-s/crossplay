# Development Workflow

This page defines the expected local workflow before opening a pull request.

## 1) Fork and create a focused branch

- Fork the repository to your own GitHub account
- Clone your fork locally and add the upstream repository if needed
- Enable repo hooks once per clone: `git config core.hooksPath .githooks && chmod +x .githooks/pre-commit`

- Branch from `xteink`, this fork's default branch. Upstream's is `develop`;
  they are not interchangeable.
- Keep each PR focused on one fix or feature area

## 2) Implement with scope in mind

- Confirm your idea is in this fork's scope: [LOCAL_SCOPE.md](../../LOCAL_SCOPE.md),
  which overrides upstream's [SCOPE.md](../../SCOPE.md) where the two disagree.
  Most of what belongs here lives in new files under `src/apps_local/`.
- Prefer incremental changes over broad refactors

## 3) Run local checks

```sh
./bin/clang-format-fix
pio check --fail-on-defect high
./scripts_local/check.sh --committed
```

CI enforces formatting, static analysis, the unit tests and the device builds.
Use clang-format 21 locally to match it; if `clang-format` is missing or the
wrong version, see [Getting Started](./getting-started.md).

Do not reach for a bare `pio run` instead. It builds `[env:default]`, which is
upstream's ESP32-C3 target rather than this fork's, and it bypasses the
workspace build lock that keeps concurrent builds from corrupting each other.
Name an environment (`-e x4pro`, `-e sticky`, `-e simulator_x4_pro`) when you
build one directly.

## 4) Open the PR

- Target `xteink` (this repository's default branch)
- Use a semantic title (example: `fix: avoid crash when opening malformed epub`)
- Describe the problem, approach, and any tradeoffs
- Say what you did **not** verify. Hardware always counts as not verified.
- Include reproduction and verification steps for bug fixes
- [Landing and integration](./landing-and-integration.md) says which gate your
  branch actually needs, and what to check first when the gate goes red on
  something that is not your diff

## 5) Review etiquette

- Be explicit and concise in responses
- Keep discussions technical and respectful
- Assume good intent and focus on code-level feedback

For community expectations, see [GOVERNANCE.md](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/GOVERNANCE.md).
