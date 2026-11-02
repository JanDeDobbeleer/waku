# MSIX package

`build.ps1` packages a deployed waku folder (`dist/waku`, from the `deploy` target) as
`out/waku-<arch>.msix` plus `out/waku-<arch>.appinstaller`. The release workflow
(`.github/workflows/release.yml`) runs it for x64 and arm64 and signs with Azure Trusted Signing.

```powershell
cmake --build --preset arm64-release --target deploy   # see docs/building.md
./build.ps1 -Architecture arm64 -Version 0.1.0         # unsigned; the workflow adds -Sign
```

The package needs the C++ runtime DLLs in the source folder (the workflow copies them in; a
package cannot run `vc_redist`). An unsigned package does not install. To try one locally, sign it
with a test certificate whose subject equals `Publisher` in `appxmanifest.xml`, trust that
certificate, then `Add-AppxPackage -Path out/waku-arm64.msix`.

## What the package does

- Aliases `waku.exe` and `wakuc.exe` go on `PATH` (`Application` entries `Waku` and `Wakuc`).
- `runFullTrust` and `unvirtualizedResources`, with file-system and registry write virtualization
  off: the config home, `%LOCALAPPDATA%\waku` and the registry are shared with unpackaged tools.
- The embedded App Installer file polls
  `https://github.com/<repo>/releases/latest/download/waku-<arch>.appinstaller` for updates.
- `signing.json` names the Azure Trusted Signing account and certificate profile. Its publisher
  must equal `Publisher` in `appxmanifest.xml`.
- `icons/` holds placeholders drawn by `make-icons.ps1`. Replace them with real artwork.
