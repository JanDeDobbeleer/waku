#include "widgets/github/github_auth_dialog.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace yasb::widgets {

GithubAuthDialog::GithubAuthDialog(QString tokenFilePath, QWidget* parent)
    : QDialog(parent), tokenFilePath_(std::move(tokenFilePath)) {
  setWindowTitle(QStringLiteral("GitHub Authorization - YASB"));
  setWindowFlag(Qt::WindowContextHelpButtonHint, false);
  setWindowFlag(Qt::WindowStaysOnTopHint, true);
  setAttribute(Qt::WA_DeleteOnClose);
  setFixedSize(420, 260);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 20, 24, 16);
  layout->setSpacing(6);

  title_ = new QLabel(QStringLiteral("Sign in to GitHub"), this);
  layout->addWidget(title_);
  instructions_ = new QLabel(QStringLiteral("Requesting authorization code..."), this);
  instructions_->setWordWrap(true);
  layout->addWidget(instructions_);

  auto* codeFrame = new QFrame(this);
  codeFrame->setFixedHeight(56);
  auto* codeLayout = new QHBoxLayout(codeFrame);
  codeLayout->setContentsMargins(16, 0, 8, 0);
  codeLayout->setSpacing(8);
  codeLabel_ = new QLabel(QString(10, QChar(0x2013)), codeFrame);
  QFont codeFont = codeLabel_->font();
  codeFont.setStyleHint(QFont::Monospace);
  codeFont.setPointSize(16);
  codeFont.setBold(true);
  codeFont.setLetterSpacing(QFont::AbsoluteSpacing, 3);
  codeLabel_->setFont(codeFont);
  codeLayout->addWidget(codeLabel_, 1);
  copyButton_ = new QPushButton(QStringLiteral("Copy"), codeFrame);
  copyButton_->setFixedSize(80, 30);
  copyButton_->setEnabled(false);
  connect(copyButton_, &QPushButton::clicked, this, [this] {
    if (userCode_.isEmpty()) return;
    QApplication::clipboard()->setText(userCode_);
    copyButton_->setText(QStringLiteral("Copied"));
    QTimer::singleShot(1000, this, [this] { copyButton_->setText(QStringLiteral("Copy")); });
  });
  codeLayout->addWidget(copyButton_);
  layout->addSpacing(4);
  layout->addWidget(codeFrame);

  status_ = new QLabel(QStringLiteral("Connecting to GitHub..."), this);
  layout->addWidget(status_);
  layout->addStretch();

  auto* buttons = new QHBoxLayout;
  buttons->setSpacing(8);
  buttons->addStretch();
  openButton_ = new QPushButton(QStringLiteral("Open Browser"), this);
  connect(openButton_, &QPushButton::clicked, this, [] {
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/login/device")));
  });
  buttons->addWidget(openButton_);
  cancelButton_ = new QPushButton(QStringLiteral("Cancel"), this);
  connect(cancelButton_, &QPushButton::clicked, this, &QDialog::reject);
  buttons->addWidget(cancelButton_);
  layout->addLayout(buttons);

  pollTimer_ = new QTimer(this);
  pollTimer_->setSingleShot(true);
  connect(pollTimer_, &QTimer::timeout, this, &GithubAuthDialog::poll);

  QTimer::singleShot(100, this, &GithubAuthDialog::startDeviceFlow);
}

void GithubAuthDialog::startDeviceFlow() {
  platform::sendHttpAsync(
      platform::githubDeviceCodeRequest(), this,
      [this](const platform::HttpResponse& response) { onDeviceCode(response); });
}

void GithubAuthDialog::onDeviceCode(const platform::HttpResponse& response) {
  if (stop_) return;
  const platform::GithubDeviceCode code = platform::parseGithubDeviceCode(response);
  if (!code.error.isEmpty()) {
    showResult(code.error);
    return;
  }
  userCode_ = code.userCode;
  deviceCode_ = code.deviceCode;
  pollIntervalSeconds_ = code.interval;
  codeLabel_->setText(userCode_);
  copyButton_->setEnabled(true);
  status_->setText(QStringLiteral("Waiting for authorization..."));
  instructions_->setText(QStringLiteral(
      "Your browser has been opened. Enter the code below at github.com/login/device:"));
  QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/login/device")));
  schedulePoll();
}

void GithubAuthDialog::schedulePoll() {
  // Python: poll_interval = max(interval, 5), then sleeps before every request.
  pollTimer_->start(std::max(pollIntervalSeconds_, 5) * 1000);
}

void GithubAuthDialog::poll() {
  if (stop_) return;
  platform::sendHttpAsync(platform::githubAccessTokenRequest(deviceCode_), this,
                          [this](const platform::HttpResponse& response) {
                            if (stop_) return;
                            const platform::GithubTokenPoll result =
                                platform::parseGithubAccessToken(response);
                            using Kind = platform::GithubTokenPoll::Kind;
                            switch (result.kind) {
                              case Kind::Pending:
                                schedulePoll();
                                break;
                              case Kind::SlowDown:
                                pollIntervalSeconds_ = std::max(pollIntervalSeconds_, 5) + 5;
                                schedulePoll();
                                break;
                              case Kind::Token:
                                finishSuccess(result.token);
                                break;
                              case Kind::Error:
                                showResult(result.message);
                                break;
                            }
                          });
}

void GithubAuthDialog::showResult(const QString& message) {
  title_->hide();
  instructions_->hide();
  codeLabel_->parentWidget()->hide();
  status_->hide();
  openButton_->hide();
  if (!result_) {
    result_ = new QLabel(this);
    result_->setWordWrap(true);
    result_->setAlignment(Qt::AlignCenter);
    static_cast<QVBoxLayout*>(layout())->insertWidget(0, result_, 1);
  }
  result_->setText(message);
  cancelButton_->setText(QStringLiteral("Close"));
}

void GithubAuthDialog::finishSuccess(const QString& token) {
  platform::saveGithubTokenFile(tokenFilePath_, token);
  showResult(QStringLiteral("Authorization successful!"));
  emit authCompleted(token);
  QTimer::singleShot(1800, this, &QDialog::accept);
}

void GithubAuthDialog::closeEvent(QCloseEvent* event) {
  stop_ = true;
  pollTimer_->stop();
  QDialog::closeEvent(event);
}

}  // namespace yasb::widgets
