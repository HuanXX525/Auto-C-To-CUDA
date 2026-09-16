# Acceptance configuration

`acceptance_matrix.json` is the tracked source of truth for the GPU migration
test matrix. `runtime_1.json` supplies the tracked N=1 entry. The N=2 and N=128
production runtime files remain owned by the pinned Aircraft-Simu submodule.
Intermediate N values use the same per-instance scenario, flight-control and
fault inputs and are selected by the GPU probe's `--instances` option until
production execution is connected to the validated `--config` data model.

Paths are repository-relative.  All acceptance scripts must resolve them from
the repository root rather than from the caller's current working directory.

Validate the production runtime mapping (including N=1/2/128, deduplicated
assets, missing paths and invalid ranges) before launching a kernel with:

```bash
./tools/build_campaign_config_probe.sh
```
