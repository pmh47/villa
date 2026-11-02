# Spiral startup validation

The native revision view resolves selected objects once and reuses matching
relationship pins, retaining historical applicability checks and releasing the
GIL during native reads. Spiral batches verified track arrays in groups of 4096
and skips the duplicate startup snapshot when plain-store link refresh is off.
Ordering, precision, filtering, and relationship semantics are unchanged.
CLI and service use the same loader; no new configuration is required.
Rebuild the object-store Python binding as well as using the updated Spiral code.

## Reproduce

Use an existing Python environment with Spiral's dependencies. From the repository
root, set `PYTHON` to its interpreter and `BUILD` to the object-store build directory.
Build with `RelWithDebInfo` or `Release`, Python bindings and tests enabled, using
the dependency setup documented by the object store. No installation is necessary
to test an isolated build:

```sh
cmake --build "$BUILD" --target vc_object_bindings test_object_store_relationships test_object_store_hub_positions test_object_store_geometry_edits -j 6
ctest --test-dir "$BUILD" --output-on-failure -R 'object_store_(relationships|hub_positions|geometry_edits)$'
export PYTHONPATH="$PWD/spiral-fitting${PYTHONPATH:+:$PYTHONPATH}"
H=vc_object_store/benchmarks/spiral_load
"$PYTHON" "$H/run_with_store.py" "$BUILD/python" -m pytest spiral-fitting/tests/test_object_store_tracks.py spiral-fitting/tests/test_object_store_startup.py spiral-fitting/tests/test_object_store_positions.py vc_object_store/tests/python/test_workflows.py -q
"$PYTHON" "$H/run_with_store.py" "$BUILD/python" "$H/profile_spiral_store.py" "$ROI_STORE" --spiral "$PWD/spiral-fitting" --out /tmp/spiral-load
"$PYTHON" "$H/run_with_store.py" "$BUILD/python" "$H/bench_crossings.py" "$CROSSING_STORE" --spiral "$PWD/spiral-fitting" --out /tmp/spiral-crossings.json
```

The launcher bypasses the installed object-store editable-module redirect and
prints the actual native module path. Explicit `PYTHONPATH` is also necessary
when the environment otherwise imports Spiral from another worktree.
Both benchmarks open stores read-only. The ROI benchmark disables refresh and
tracks and selects z=[10000,11000). Add `--profile` for cProfile hotspots.
The crossing benchmark materializes tracks in z=[0,20000).
Compare JSON hashes, counts, and shapes, not just timings.

## Measurements and limits

Earlier experiments on baseline `4c1b46a37`, Ubuntu x86-64, i7-11700F, GCC 13.3,
RelWithDebInfo, Python 3.14 used the schema-17 Paris 4 ROI store on local NVMe:
9604 constraint surfaces, one shell, 111 fibers, 662 point collections, and
1,665,121 tracks. Three alternating runs per variant, without dropping OS caches:

| Full non-track load, refresh off | Mean | Min | Median | Max |
|---|---:|---:|---:|---:|
| Baseline | 70.27s | 68.04s | 69.46s | 73.32s |
| Optimized | 33.98s | 32.94s | 33.75s | 35.26s |

Materialized output hashes were identical. Baseline profiling attributed 46.85s
to two snapshot reads and 37.59s to materialization; these profiled timings are
separate from the table. These historical measurements motivate the changes;
they are not a new controlled A/B measurement against `dd1e080b7`.

The port onto `dd1e080b7` preserves its GIL release. A fresh single-run check on
43,902 real tracks and 99,421 crossings took 31.27s for the snapshot and 2.91s
for materialization; all array hashes, shapes and counts match the baseline.
Peak process RSS was 2.80 GiB. This validation build reused cached Delta3D
`eedd0e6`, rather than main's pinned `4533974`; dependency integration at the
new pin and macOS/arm64 were not validated.

The fresh full non-track ROI check took 50.74s and matched the baseline's input
digest, counts, and all materialized output hashes. It overlapped other validation
work, so it is a correctness check rather than a controlled timing comparison.
Regression validation: 94 Python tests passed, two skipped; all three native test
executables passed. Python workflows still emit nanobind shutdown leak warnings,
also seen in the earlier experiment; this change does not address those warnings.

Automatic link refresh can still take minutes; these changes do not cache
detector results. They also do not implement packed crossing records or prove
acceptable memory usage for loading all 1.67 million tracks. Full-store track
loading and a GPU fit remain untested.
