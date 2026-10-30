#pragma once

#include <QByteArray>
#include <QString>

namespace wabar::kernel {

// 32-bit BGRA, straight (not premultiplied) alpha, top-down. The callers turn this into a QImage
// (Format_ARGB32).
struct IconBitmap {
  int width = 0;
  int height = 0;
  QByteArray bgra;
  // False when the window did not answer WM_GETICON in time; the caller asks again later.
  bool complete = true;
  bool isNull() const { return width <= 0 || height <= 0 || bgra.isEmpty(); }
};

// The shell icon of a parsing name (a path, or "shell:AppsFolder\<aumid>") at about `size` pixels,
// null when the shell has none. Blocking; joins the calling thread to COM for the call, so any
// thread may use it (the GUI thread should not: it can take a while).
IconBitmap shellItemIcon(const QString& parsingName, int size);

// The colour and mask bitmaps of an HICON as straight alpha BGRA; `hicon` is an HICON. Null when
// the icon has no alpha information.
IconBitmap iconBitmapFromHicon(void* hicon);

}  // namespace wabar::kernel
