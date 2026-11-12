pragma Singleton
import QtQml
import waku
import waku.services as S

// Quickshell's `Quickshell` singleton. `screens` is the host's screen list; everything else is
// forwarded to waku.services.QuickshellSingleton (env, execDetached, the paths, clipboardText,
// reload, ...). A QML file because the host unit cannot include a feature's headers, and QML may
// use any unit.
QtObject {
    id: root

    // The screens shells are built for (the same list as ScreenList.screens).
    final readonly property list<ShellScreen> screens: ScreenList.screens

    final readonly property real processId: S.QuickshellSingleton.processId
    final readonly property string shellDir: S.QuickshellSingleton.shellDir
    final readonly property string shellRoot: S.QuickshellSingleton.shellDir
    final readonly property string configDir: S.QuickshellSingleton.shellDir
    final readonly property string dataDir: S.QuickshellSingleton.dataDir
    final readonly property string stateDir: S.QuickshellSingleton.stateDir
    final readonly property string cacheDir: S.QuickshellSingleton.cacheDir

    // Read and write. An alias, not a binding: the system clipboard is read when a shell reads the
    // value, not each time this singleton is created (every start and reload). A read can block
    // for seconds while another process holds the clipboard.
    final property alias clipboardText: clipboard.text
    final property QtObject clipboardSource: S.ClipboardText {
        id: clipboard
    }

    // Read and write: the value is copied both ways, so it follows the singleton and a write
    // reaches it.
    final property bool watchFiles: S.QuickshellSingleton.watchFiles
    final property string workingDirectory: S.QuickshellSingleton.workingDirectory

    onWatchFilesChanged: S.QuickshellSingleton.watchFiles = watchFiles
    onWorkingDirectoryChanged: S.QuickshellSingleton.workingDirectory = workingDirectory

    signal reloadCompleted
    signal reloadFailed(string errorString)
    signal lastWindowClosed

    function env(name) {
        return S.QuickshellSingleton.env(name);
    }

    function execDetached(context) {
        S.QuickshellSingleton.execDetached(context);
    }

    function shellPath(path) {
        return S.QuickshellSingleton.shellPath(path);
    }

    function configPath(path) {
        return S.QuickshellSingleton.configPath(path);
    }

    function dataPath(path) {
        return S.QuickshellSingleton.dataPath(path);
    }

    function statePath(path) {
        return S.QuickshellSingleton.statePath(path);
    }

    function cachePath(path) {
        return S.QuickshellSingleton.cachePath(path);
    }

    function iconPath(icon, checkOrFallback) {
        return checkOrFallback === undefined
            ? S.QuickshellSingleton.iconPath(icon)
            : S.QuickshellSingleton.iconPath(icon, checkOrFallback);
    }

    function hasThemeIcon(icon) {
        return S.QuickshellSingleton.hasThemeIcon(icon);
    }

    function hasVersion(major, minor, features) {
        return features === undefined
            ? S.QuickshellSingleton.hasVersion(major, minor)
            : S.QuickshellSingleton.hasVersion(major, minor, features);
    }

    function hasQtVersion(major, minor) {
        return S.QuickshellSingleton.hasQtVersion(major, minor);
    }

    function reload(hard) {
        S.QuickshellSingleton.reload(hard === true);
    }

    function inhibitReloadPopup() {
        S.QuickshellSingleton.inhibitReloadPopup();
    }

    final property QtObject forwarder: Connections {
        target: S.QuickshellSingleton

        function onWatchFilesChanged() {
            root.watchFiles = S.QuickshellSingleton.watchFiles;
        }
        function onWorkingDirectoryChanged() {
            root.workingDirectory = S.QuickshellSingleton.workingDirectory;
        }
        function onReloadCompleted() {
            root.reloadCompleted();
        }
        function onReloadFailed(errorString) {
            root.reloadFailed(errorString);
        }
        function onLastWindowClosed() {
            root.lastWindowClosed();
        }
    }
}
