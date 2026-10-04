# Upstream baseline

- Source: https://github.com/s60sc/ESP32-CAM_MJPEG2SD
- Captured commit: `3c9ae28d04ef77b56b378d4d8dbdfe5674f32ae4`
- Application version: `10.9.8`
- Local tag: `upstream-baseline` (untouched source)
- Working branch: `feature/xiao-wg-ui`
- Remote name: `upstream`; no publishing remote is configured.
- License: AGPL-3.0. Original LICENSE and attribution are preserved.

The existing folder contained only the supplied plan, so Git init + fetch +
checkout captured the upstream directly into this folder, retaining the plan.
This is equivalent to a shallow clone of the recorded source revision.

Build adaptation retains the original sketch and `src/` layout. Only board
selection and build configuration may change before the baseline build.

