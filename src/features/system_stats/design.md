# system_stats: CPU, memory, battery, GPU and disk samples

Current design of the capability. The widgets are `Cpu`, `Memory` and `Battery` (`qml/*.qml`), built
on the services of the same names (`wabar.services`, `qml/stat_sources.h`) and the shared
`StatGraph` and `StatPopup` components.

## Layers

| Path | Content |
|---|---|
| `system_stats.h` | `CpuSample`, `MemorySample`, `BatterySample` and the data source interfaces `CpuDataSource`, `MemoryDataSource`, `BatteryDataSource`, plus the three factories. Qt Core only. |
| `win32/pdh.*` | `PdhCpuDataSource`: PDH counters for total and per-core load, base and current frequency, core counts. |
| `win32/memory.*` | `WindowsMemoryDataSource`: `GlobalMemoryStatusEx`, `GetPerformanceInfo`, the swap counter, the cached size. |
| `win32/battery.*` | `WindowsBatteryDataSource`: `GetSystemPowerStatus` plus the battery IOCTLs for rate, voltage, capacity and health. |
| `gpu_engines.*` | pure: `gpuEngineKey`, `gpuUsage` (sums the processes on one engine type of one adapter, the busiest engine type wins) |
| `win32/gpu.cpp` | `PdhGpuDataSource`: PDH `GPU Engine` utilization and `GPU Adapter Memory` dedicated usage; DXGI for the adapter name and dedicated memory size |
| `win32/disks.cpp` | `WindowsDiskDataSource`: `GetLogicalDrives`, `GetDriveTypeW`, `GetDiskFreeSpaceExW`, `GetVolumeInformationW` |
| `qml/stat_source.h` | `StatSource`: the QML face of `kernel::PolledSource` (polling starts at `componentComplete()`). Also the base of the `Network` service. |
| `qml/stat_sources.*` | `Memory`, `Cpu`, `Battery`: one QML element each, exposing the last sample as properties. |
| `testing/fake_stat_sources.h` | Fake data sources with call counters, used by `tests/`. |

## Gpu and Disks

`Gpu` and `Disks` are polled like the others (`StatSource`).

- `Gpu`: `available`, `usage` (also `percent`, `percentage`), `memory` (also `memoryUsed`; dedicated
  video memory in use, bytes), `memoryTotal` (dedicated memory of the hardware adapters, bytes; 0 for
  an integrated GPU without dedicated memory), `memoryPercent`, `name` (first hardware adapter).
  Everything stays 0 and `available` false when the `GPU Engine` counter is missing.
- The engine query is rebuilt every 20 samples, one sample ahead of the swap, because a PDH wildcard
  query never sees processes that start after it was opened. The first sample after the first open
  reads 0 (a rate counter needs two collections).
- `Disks`: `disks` is a list of maps with `mount`, `name`, `fileSystem`, `total`, `free`, `used`,
  `perc`/`percent`, `hasRoot`/`system` (holds Windows), `removable`; `primaryDisk` is the system
  drive's map. Fixed, removable and optical drives are listed; network drives are not, so an
  unreachable share cannot stall the sample. `SEM_FAILCRITICALERRORS` keeps an empty card reader
  from raising a dialog. Volumes with 0 total bytes (empty optical drives) are skipped.
- Names follow caelestia's `Gpu` (`percentage`, `name`) and `DiskInfo` (`mount`, `used`, `total`,
  `free`, `perc`, `hasRoot`). Sizes are bytes.

## Behaviour

- A service samples on a `QThreadPool` thread (`PolledSource::sampleAsync`), never two samples at
  once, and applies the result on the GUI thread. A throwing sample is dropped and polling goes on.
- `interval` has a floor of 100 ms. `active = false` stops polling until it is set again.
- Each sample also feeds a history ring (`historySize`, default 60) that `StatGraph` draws.
- `Battery.available` is false when the machine has no battery; the widget then hides itself.
- On the offscreen platform and in tests the real data sources are not created by the tests: they
  pass fakes through the second constructor of each service.

## Windows APIs

All documented, except the `ntdll!NtQuerySystemInformation` class 80 call in `win32/memory.cpp`
(cached size); it is on the allowlist in `docs/architecture.md` and falls back to `GetPerformanceInfo`.
`win32/pdh.cpp` picks `% Processor Utility` or `% Processor Time` from `kernel::windowsBuild()`.

## Threads

No thread of its own. Samples run on the global `QThreadPool`; a service destroyed during a sample
drops the result (guarded by a weak handle in `PolledSource`).
