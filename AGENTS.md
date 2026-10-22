# Agent Guide

This file is for AI assistants; it is kept terse on purpose. Human docs: README.md, docs/.

## Source
- Design intent lives in `main/` and `components/` (then `docs/`, `README.md`). `build*/` is generated: read it only for build/link issues, never edit it.
- Third-party code: `third_party/` (LovyanGFX is a git submodule; don't edit, bump the submodule instead).

## ESP-IDF
- Pinned to ESP-IDF v5.5.6 (EIM-managed). Parent and derived projects (e.g. cyd_time_puncher) use the same IDF; upgrade here first, then merge into the derived ones. Never go below v5.5.5: derived TLS 1.2 session resumption depends on its mbedtls dynamic-buffer fix.
- `idf.py` is not on PATH. Prefer the wrappers, which load the pinned IDF and default `DEV=1`: `./scripts/idf-build.sh`, `./scripts/idf-menuconfig.sh`, `./scripts/idf-flash-monitor.sh PORT`, or `./scripts/idf-env.sh <idf.py args>`.
- Direct use: `source ~/.espressif/tools/activate_idf_v5.5.6.sh || true` (the script can exit non-zero after setting up; never chain it with `&&` under `set -e`), then `python "$IDF_PATH/tools/idf.py" ...`.
- Use `DEV=1` for normal builds. `DEV` is read at CMake configure time; to change it, `idf.py reconfigure` with the new value (a `DEV=` on `ninja` alone has no effect).
- Wi-Fi off build: `APP_WIFI_STA=0` at configure time (see docs/build_feature_switches.md).
- If an existing `build/` was configured by another IDF/Python and idf.py refuses it, build into a separate dir (`-B <dir> -D SDKCONFIG=<dir>/sdkconfig`). If `fullclean` refuses to delete `build/`, ask the user to delete it; never delete it yourself.
- `PermissionError: [Errno 1]` from psutil in idf_component_manager means the sandbox blocked process listing, not a source error: rerun with permissions.

## Verify
- Host tests: `bash test/host/run.sh` (component sources against stubs in `test/host/stub/`).
- UI: `tools/cyd_sim` (SDL). `cyd_sim --export DIR --scale N` writes every scene as PNG; compare exports byte-for-byte to prove rendering is unchanged. See docs/cyd_sim.md.
- Hardware checks are done by the user; put a device checklist in each PR.

## Docs style
- Write docs in Japanese; headings in ASCII English; identifiers as in code.
- Add short English supplements only where they remove ambiguity (intent, constraints, invariants, API contracts, concurrency).
- This file is the exception: English only, terse.
