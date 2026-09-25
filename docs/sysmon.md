# key-cli machine protocol

This is the stable machine-facing contract owned by key-cli. `key sysmon` preserves the
schema-versioned JSON interface migrated from keytop; Clavis consumes the JSONL stream
directly. Protocol changes require updating this document and the integration
contract tests in `native/tests/sysmon_integration_test.cpp`.

## Commands

```bash
key sysmon snapshot --format json
key sysmon stream --format jsonl --interval 1000 --modules system,cpu,memory,network
key sysmon processes --format json --limit 20 --sort cpu
key sysmon modules --format json
```

The supported metric modules are `system`, `cpu`, `memory`, `gpu`, `disk`, `network`,
`battery` and `processes`. The `ram` module name is accepted as an input alias for
`memory`. Processes are collected only when the `processes` module is requested.

## JSON envelope

Successful snapshots contain:

- `schemaVersion`: integer, currently `1`;
- `timestampMs`: Unix epoch timestamp in milliseconds;
- `sequence`: monotonically increasing sample sequence for a stream;
- `intervalMs`: configured stream interval when present;
- one object for each requested module;
- `errors`: array of partial-sensor diagnostics, possibly empty.

`key sysmon modules --format json` returns the same `schemaVersion` and a `modules` array.
Usage failures requested with `--format json` return `schemaVersion`, `ok: false`, and an
`error` object with a stable `code` and human-readable `message`. Successful metric
snapshots do not add an `ok` field; their process exit status is the success signal.

Unavailable numeric values are JSON `null`, not fake zeroes. Each module also exposes its
`available` boolean. Empty strings, empty arrays and zero counters retain their meaning and
must not be treated as missing values by consumers.

## Units and field families

- byte counters and sizes use bytes (`totalBytes`, `usedBytes`, `memoryBytes`);
- transfer and I/O rates use bytes per second (`*BytesPerSecond`);
- temperatures use Celsius (`*TemperatureCelsius`), unless a field name says otherwise;
- frequencies use MHz (`*MHz`), power uses watts (`*Watts`), and percentages are `0..100`;
- process times use milliseconds for `startTimeMs` and seconds for `runtimeSeconds`;
- optional values, including unavailable sensor readings, use JSON `null`.

The exact field names are the JSON names emitted by `native/src/core/sysmon/serialization.cpp`.
Module objects include their documented availability flag and may contain additional
device-specific fields, but field type and unit changes are protocol changes.

### GPU devices

Each `gpus` entry keeps the schema-version-1 metric fields and adds stable identity and
metric provenance. PCI devices use a normalized `pciId` such as `0000:01:00.0` and an
`id` of `pci:0000:01:00.0`; non-PCI devices use a stable `drm:` fallback. The array is
sorted by `id`, so DRM card numbering and provider enumeration order are not identities.

`utilizationPercent` means the provider's best normalized device-level utilization in
the range `0..100`. Native aggregate values use sources such as `nvml-device` or
`sysfs-device`. Intel DRM fdinfo fallback reports `drm-fdinfo-engine-max`: clients are
deduplicated, activity is aggregated per physical engine, and the busiest engine is used
rather than summing unrelated engines. The first sample and invalid/reset deltas are
`null`.

The additive `provider`, `utilizationSource`, and `capabilities` fields describe the
current sample. `capabilities` contains `utilization`, `temperature`, `memory`, `power`,
and `frequency` booleans; a false capability corresponds to null metric fields. The
legacy `supported` field remains and is true when at least one capability is available.
NVML is loaded dynamically when present. Missing NVIDIA or Intel vendor runtimes do not
prevent sysfs/fdinfo providers or other GPUs from being sampled.

### Disk devices

Each `disks` entry represents one physical block device rather than one mounted
filesystem. `device` is the normalized parent block path, for example
`/dev/nvme0n1`; separate `/`, `/home`, and EFI mounts on that drive do not create
duplicate entries. Partition devices, mount paths, and filesystem types remain available
as the additive string arrays `partitions`, `mountPoints`, and `filesystems`. The legacy
singular `mountPoint` and `filesystem` fields are empty for physical-disk entries.

`readBytesPerSecond`, `writeBytesPerSecond`, `readIops`, and `writeIops` are sampled from
the parent block device counters, so they describe whole-device activity.
`deviceTotalBytes` comes from the parent device sector count. `totalBytes`, `usedBytes`,
and `freeBytes` aggregate mounted filesystem capacity once per distinct partition, so an
unmounted filesystem is not incorrectly reported as free space; if no mounted capacity is
available, `totalBytes` falls back to `deviceTotalBytes`. `usagePercent` is
`usedBytes / totalBytes * 100`. The first rate sample remains `null`.

## JSONL stream and exit status

`stream` emits one complete compact JSON object per line, flushes after each line, and
stops on `SIGINT`, `SIGTERM` or `SIGHUP`. A consumer should parse each line independently
and use `sequence` to detect ordering. `--interval` accepts `100..60000` milliseconds.
Cadence uses monotonic absolute deadlines. Collection time therefore does not accumulate
into interval drift; if a sample overruns one or more ticks, key-cli skips missed deadlines
and resumes at the next future deadline instead of emitting catch-up samples back-to-back.

Exit codes are:

- `0`: successful output, including snapshots with partial sensor errors;
- `2`: command-line usage or option error;
- `3`: output stream failure.

Text output is for humans and is not a machine protocol. Consumers must request `--format
json` or `--format jsonl` and must reject an unknown `schemaVersion` rather than guessing.
