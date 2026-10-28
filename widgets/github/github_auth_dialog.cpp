#include "widgets/github/github_auth_dialog.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "core/widgets/backdrop.h"
#include "widgets/fluent/fluent_button.h"
#include "widgets/fluent/fluent_theme.h"

namespace yasb::widgets {

GithubAuthDialog::GithubAuthDialog(QString tokenFilePath, QWidget* parent)
    : QDialog(parent), tokenFilePath_(std::move(tokenFilePath)) {
  setWindowTitle(QStringLiteral("GitHub Authorization - YASB"));
  setWindowFlag(Qt::WindowContextHelpButtonHint, false);
  setWindowFlag(Qt::WindowStaysOnTopHint, true);
  setAttribute(Qt::WA_DeleteOnClose);
  setFixedSize(420, 260);
  buildView();

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 20, 24, 16);
  layout->setSpacing(6);

  title_ =
      fluent::makeTextBlock(QStringLiteral("Sign in to GitHub"), QStringLiteral("subtitle"), this);
  layout->addWidget(title_);
  instructions_ = fluent::makeTextBlock(QStringLiteral("Requesting authorization code..."),
                                        QStringLiteral("caption"), this);
  instructions_->setWordWrap(true);
  layout->addWidget(instructions_);

  codeFrame_ = new QFrame(this);
  codeFrame_->setFixedHeight(56);
  auto* codeLayout = new QHBoxLayout(codeFrame_);
  codeLayout->setContentsMargins(16, 0, 8, 0);
  codeLayout->setSpacing(8);
  codeLabel_ = new QLabel(QString(10, QChar(0x2013)), codeFrame_);
  QFont codeFont = codeLabel_->font();
  codeFont.setStyleHint(QFont::Monospace);
  codeFont.setPointSize(16);
  codeFont.setBold(true);
  codeFont.setLetterSpacing(QFont::AbsoluteSpacing, 3);
  codeLabel_->setFont(codeFont);
  codeLayout->addWidget(codeLabel_, 1);
  copyButton_ = new fluent::FluentButton(QStringLiteral("Copy"), QStringLiteral("default"), 12,
                                         QStringLiteral("demibold"), this);
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
  layout->addWidget(codeFrame_);

  status_ = fluent::makeTextBlock(QStringLiteral("Connecting to GitHub..."),
                                  QStringLiteral("caption"), this);
  layout->addWidget(status_);
  layout->addStretch();

  separator_ = new QFrame(this);
  separator_->setFixedHeight(1);
  layout->addWidget(separator_);
  layout->addSpacing(4);

  auto* buttons = new QHBoxLayout;
  buttons->setSpacing(8);
  buttons->addStretch();
  openButton_ = new fluent::FluentButton(QStringLiteral("Open Browser"), QStringLiteral("accent"),
                                         12, QStringLiteral("demibold"), this);
  connect(openButton_, &QPushButton::clicked, this, [] {
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/login/device")));
  });
  buttons->addWidget(openButton_);
  cancelButton_ = new fluent::FluentButton(QStringLiteral("Cancel"), QStringLiteral("default"), 12,
                                           QStringLiteral("demibold"), this);
  connect(cancelButton_, &QPushButton::clicked, this, &QDialog::reject);
  buttons->addWidget(cancelButton_);
  layout->addLayout(buttons);

  pollTimer_ = new QTimer(this);
  pollTimer_->setSingleShot(true);
  connect(pollTimer_, &QTimer::timeout, this, &GithubAuthDialog::poll);

  QTimer::singleShot(100, this, &GithubAuthDialog::startDeviceFlow);
}

// ViewBase.build_view: Mica backdrop with a transparent base on Windows 11, a solid base fill
// elsewhere.
void GithubAuthDialog::buildView() {
  const bool dark = fluent::isDark();
  const bool mica = isMicaSupported();
  const QString background =
      mica ? QStringLiteral("transparent") : fluent::token(QStringLiteral("solid_bg_base"), dark);
  setStyleSheet(QStringLiteral("QDialog { background-color: %1; } QLabel { color: %2; }")
                    .arg(background, fluent::token(QStringLiteral("text_primary"), dark)));
  if (!mica) return;
  setAttribute(Qt::WA_TranslucentBackground, true);
  enableMica(winId());
}

// GitHubAuthDialog._apply_frame_styles: frame tints derived from the window text colour.
void GithubAuthDialog::applyFrameStyles() {
  QColor border = palette().color(QPalette::WindowText);
  border.setAlphaF(0.12f);
  QColor fill = palette().color(QPalette::WindowText);
  fill.setAlphaF(0.04f);
  QColor separator = palette().color(QPalette::WindowText);
  separator.setAlphaF(0.08f);
  codeFrame_->setStyleSheet(
      QStringLiteral("QFrame { border-radius: 6px; border: 1px solid %1; background-color: %2; }")
          .arg(border.name(QColor::HexArgb), fill.name(QColor::HexArgb)));
  codeLabel_->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
  separator_->setStyleSheet(
      QStringLiteral("background-color: %1;").arg(separator.name(QColor::HexArgb)));
}

void GithubAuthDialog::showEvent(QShowEvent* event) {
  applyFrameStyles();
  QDialog::showEvent(event);
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
  applyFrameStyles();
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
  codeFrame_->hide();
  status_->hide();
  separator_->hide();
  openButton_->hide();
  if (!result_) {
    result_ = fluent::makeTextBlock(QString(), QStringLiteral("subtitle"), this);
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
