# Acceptance configuration

`acceptance_matrix.json` is the tracked source of truth for the GPU migration
test matrix.  The N=2 and N=128 production runtime files remain owned by the
pinned Aircraft-Simu submodule.  Intermediate N values use the same per-instance
scenario, flight-control and fault inputs and are selected by the GPU probe's
`--instances` option until the production `--config` loader is available.

Paths are repository-relative.  All acceptance scripts must resolve them from
the repository root rather than from the caller's current working directory.
