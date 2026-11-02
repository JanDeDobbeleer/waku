# system_stats: CPU, memory, battery, GPU and disk samples

Current design of the capability. The widgets are `Cpu`, `Memory` and `Battery` (`qml/*.qml`), built
on the services of the same names (`waku.services`, `qml/stat_sources.h`) and the shared
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
| `temperature.*` | pure: Kelvin and deci-Celsius to Celsius, plausibility range, `selectCpuTemperature` |
| `win32/thermal.*` | `PdhThermalReader`: PDH `Thermal Zone Information` per-zone readings |
| `win32/disks.cpp` | `WindowsDiskDataSource`: `GetLogicalDrives`, `GetDriveTypeW`, `GetDiskFreeSpaceExW`, `GetVolumeInformationW` |
| `qml/stat_source.h` | `StatSource`: the QML face of `kernel::PolledSource` (polling starts at `componentComplete()`). Also the base of the `Network` service. |
| `qml/stat_sources.*` | `Memory`, `Cpu`, `Battery`: one QML element each, exposing the last sample as properties. |
| `testing/fake_stat_sources.h` | Fake data sources with call counters, used by `tests/`. |

## One source per kind

The factories (`create*DataSource()`) hand out one process-wide source per kind through a
`kernel::LingeringHub`: every widget of every engine shares it, and it outlives its last holder by
30 s, so a reload neither closes nor opens a PDH query or a GPU adapter. A source holds no QML
object. The rate sources (CPU, GPU) return the previous sample to a caller that comes less than
1 s after it, so two widgets polling the shared query never rate a few milliseconds. The GPU source
opens its adapters for the temperature once and keeps them until it goes.

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

## Temperatures

`Cpu.temperature` and `Gpu.temperature` are Celsius. `temperatureAvailable` says whether a real value
exists; while it is false `temperature` is NaN, never 0 or a guess. Both are read only while the
service polls (`active`), and only when `kernel::liveDesktop("system_stats")`, so tests and the
offscreen platform touch no counter and no adapter. Pure conversion and selection live in
`temperature.*`; the Windows reads live in `win32/thermal.*` (CPU) and `win32/gpu.cpp` (GPU).

- CPU: the PDH set `\Thermal Zone Information(*)` (ACPI thermal zones, admin-free). Per zone,
  `High Precision Temperature` (tenths of a Kelvin) wins; `Temperature` (whole Kelvin) is the
  fallback. Readings outside 0 C (exclusive) to 150 C are dropped: sensorless zones report 0 or
  placeholders. `Cpu.temperature` is the hottest remaining zone.
- Why the hottest zone: ACPI zone names are paths (`\_SB.TZ0`), not labels, so no zone can be tied
  to the CPU by name. The hottest zone is the SoC/package proxy, not a per-core die sensor. It is
  not WMI `MSAcpi_ThermalZoneTemperature` (needs admin) and uses no driver.
- GPU: `D3DKMTQueryAdapterInfo(KMTQAITYPE_ADAPTERPERFDATA)`, field `Temperature` in deci-Celsius,
  per hardware DXGI adapter (via `D3DKMTOpenAdapterFromLuid`); the hottest wins. 0 means the driver
  reports none and gives no value. All structs and thunks are in the Windows SDK `d3dkmthk.h`
  (gdi32), so there is no undocumented-API row.

Live check on the Surface Pro 11th ed. (Snapdragon X Elite, Windows 11 build 26300), read-only:

- The counter set exists with `Temperature`, `High Precision Temperature`, `Throttle Reasons` and
  `% Passive Limit`; 37 zones: `\TPOL`, `\_SB.TZ0`, `TZ01`-`TZ05`, `TZ09`-`TZ0C`, `TZ1`-`TZ6`,
  `TZ11`, `TZ15`-`TZ24`, `TZ31`-`TZ37`, `TZ51`, `TZ99`.
- 16 zones read a plausible value (31 to 51 C across runs); 13 read 0 (`TPOL`, `TZ01`, `TZ0C`,
  `TZ15`-`TZ24`); `TZ31`/`TZ32` read -17.8 C, `TZ33`-`TZ37` -40.1 C and `TZ51` -0.1 C
  (placeholders, dropped by the range check).
- Response to load: all cores busy for 35 s raised the live zones by 0.4 to 3.3 C; `TZ0` and `TZ1`
  (47.2 to 50.5 C) rose most, `TZ99` (50.5 to 52.1 C) stayed the hottest. So the hottest-zone
  value tracks load, weakly: these are ACPI zones, not a die sensor.
- GPU: the Adreno X1-85 driver opens and answers `ADAPTERPERFDATA` with success, but
  `Temperature`, `Power` and `FanRPM` are 0 (only the memory frequency is filled). `Gpu.temperature`
  is therefore unavailable on this machine. The Microsoft Basic Render Driver fails the query
  (`STATUS_INVALID_PARAMETER`) and is a software adapter anyway.

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
