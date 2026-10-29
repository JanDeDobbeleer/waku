# Third-party notices

yasb-native is distributed under the MIT licence in `LICENSE`. It contains code derived from the
projects below, whose notices must be kept with any copy or substantial portion of this software.

## YASB (amnweb/yasb)

- Project: https://github.com/amnweb/yasb
- Reference checkout: commit `0a756c07c33e462417ee95ac341b7fbe1dbc5fc3` (version 2.0.8, recorded in
  `tests/vectors/manifest.json`)
- Use: the whole port is a C++ re-implementation of YASB's behaviour. Config handling, the CSS
  preprocessor, the adaptive bar, widget label formatting, `schema/schema.json` (exported from YASB's
  pydantic models) and the parity vectors in `tests/vectors/` all derive from it.

The licence below is copied verbatim from YASB's `LICENSE` file, including the notice for the
original project it derives from.

```text
MIT License

Copyright (c) 2024 amnweb

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.


Original project Yasb - Yet Another Status Bar (https://github.com/da-rth/yasb), which is licensed as follows:

MIT License

Copyright (c) 2021 denBot

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Build and runtime dependencies

Not vendored in this repository; fetched by vcpkg (`vcpkg.json`) or installed separately. Their
licences apply to binaries that include them.

| Dependency | Licence | How it is used |
|---|---|---|
| Qt 6.10 (Core, Gui, Qml, Quick, Network) | LGPLv3 | Dynamically linked; DLLs deployed next to the exe by `windeployqt`, unmodified |
| nlohmann-json | MIT | Header-only (JSON from the window managers, GitHub and the tray hook) |
| GoogleTest | BSD-3-Clause | Test executable only; not shipped |
| C++/WinRT headers (Windows SDK) | MIT | Header-only (apartment init) |
| Microsoft Visual C++ runtime | Microsoft redistributable licence | Not shipped by this repository |
