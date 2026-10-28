#pragma once

#include <QDialog>
#include <QPointer>
#include <QString>

#include "platform/github_api.h"

class QFrame;
class QLabel;
class QPushButton;
class QShowEvent;
class QTimer;

namespace yasb::widgets {

// OAuth device-flow window (yasb:src/core/widgets/services/github/auth_dialog.py). Requests run
// on QThreadPool threads through platform::sendHttpAsync and results come back on the GUI thread,
// so the dialog never blocks. The token is saved to the shared token file and handed to
// `tokenReady`; it is never logged or shown.
class GithubAuthDialog final : public QDialog {
  Q_OBJECT
 public:
  explicit GithubAuthDialog(QString tokenFilePath, QWidget* parent = nullptr);

 signals:
  void authCompleted(const QString& token);

 protected:
  void closeEvent(QCloseEvent* event) override;
  void showEvent(QShowEvent* event) override;

 private:
  void buildView();
  void applyFrameStyles();
  void startDeviceFlow();
  void onDeviceCode(const platform::HttpResponse& response);
  void schedulePoll();
  void poll();
  void showResult(const QString& message);
  void finishSuccess(const QString& token);

  QString tokenFilePath_;
  QString deviceCode_;
  QString userCode_;
  int pollIntervalSeconds_ = 5;
  bool stop_ = false;
  QLabel* title_ = nullptr;
  QLabel* instructions_ = nullptr;
  QLabel* codeLabel_ = nullptr;
  QLabel* status_ = nullptr;
  QLabel* result_ = nullptr;
  QFrame* codeFrame_ = nullptr;
  QFrame* separator_ = nullptr;
  QPushButton* copyButton_ = nullptr;
  QPushButton* openButton_ = nullptr;
  QPushButton* cancelButton_ = nullptr;
  QTimer* pollTimer_ = nullptr;
};

}  // namespace yasb::widgets
