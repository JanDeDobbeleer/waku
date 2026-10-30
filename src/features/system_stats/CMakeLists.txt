# system_stats: CPU, memory and battery samples and the Cpu, Memory, Battery widgets.
# pdh: Performance Data Helper counters (CPU, swap); psapi: GetPerformanceInfo (cache size);
# setupapi: battery device enumeration; dxgi: GPU adapter names and dedicated memory;
# gdi32: D3DKMT adapter performance data (GPU temperature).
wabar_feature(NAME system_stats
  SOURCES
    gpu_engines.cpp
    gpu_engines.h
    system_stats.h
    temperature.cpp
    temperature.h
  WIN32_SOURCES
    win32/battery.h
    win32/battery.cpp
    win32/disks.cpp
    win32/gpu.cpp
    win32/memory.h
    win32/memory.cpp
    win32/pdh.h
    win32/pdh.cpp
    win32/thermal.h
    win32/thermal.cpp
  QML_SOURCES
    qml/stat_sources.h
    qml/stat_sources.cpp
  QML_FILES
    qml/Battery.qml
    qml/Cpu.qml
    qml/Memory.qml
    qml/StatGraph.qml
    qml/StatPopup.qml
  TESTS
    tests/test_gpu_disks.cpp
    tests/test_stat_sources.cpp
    tests/test_stat_widgets.cpp
    tests/test_temperature.cpp
  SYSTEM_LIBS pdh psapi setupapi dxgi gdi32)
