# app: the two executables. They are the only units that may include anything (rule S2). wabar
# contributes SafeConfig.qml to the QML module wabar.widgets, so it is added before the modules are
# assembled (src/CMakeLists.txt).
add_subdirectory(wabar)
add_subdirectory(wabarc)
