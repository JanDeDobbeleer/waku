# app: the two executables. They are the only units that may include anything (rule S2). waku
# contributes SafeShell.qml to the QML module waku.widgets, so it is added before the modules are
# assembled (src/CMakeLists.txt).
add_subdirectory(waku)
add_subdirectory(wakuc)
