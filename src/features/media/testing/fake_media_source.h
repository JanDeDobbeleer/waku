#pragma once

#include <QString>
#include <vector>

#include "features/media/media_session.h"

namespace wabar::media::testing {

struct Command {
  QString appId;
  MediaCommand command;
  double value;
};

class FakeSource final : public MediaSessionSource {
 public:
  void start(MediaSessionSink* sink) override { sink_ = sink; }
  void stop() override { sink_ = nullptr; }
  void command(const QString& appId, MediaCommand command, double value) override {
    commands.push_back({appId, command, value});
  }
  MediaSessionSink* sink_ = nullptr;
  std::vector<Command> commands;
};

}  // namespace wabar::media::testing
