# startup: `wakuc startup enable|disable|status`, a per-user logon Scheduled Task that starts waku.
# Used by wakuc (src/app/wakuc) through the StartupSystem interface in startup_system.h.
waku_feature(NAME startup
  SOURCES
    startup_service.cpp
    startup_service.h
    startup_system.h
  WIN32_SOURCES
    win32/startup_system_win.cpp
    win32/startup_system_win.h
  TESTS
    tests/test_startup_service.cpp
  SYSTEM_LIBS taskschd ole32 oleaut32 uuid shell32 secur32 advapi32)
