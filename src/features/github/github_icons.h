#pragma once

#include <string>

namespace waku::github {

// The icon strings the notification list needs, one per notification kind. The defaults are the
// Nerd Font glyphs of Python YASB's GithubWidget.
struct GithubIcons {
  std::string issue = "\xEF\x90\x9B";                // U+F41B
  std::string issue_closed = "\xEF\x90\x9D";         // U+F41D
  std::string pull_request = "\xEE\xA9\xA4";         // U+EA64
  std::string pull_request_closed = "\xEE\xAF\x9A";  // U+EBDA
  std::string pull_request_merged = "\xEF\x85\xBF";  // U+F17F
  std::string pull_request_draft = "\xEE\xAF\x9B";   // U+EBDB
  std::string release = "\xEE\xAA\x84";              // U+EA84
  std::string discussion = "\xEF\x91\x82";           // U+F442
  std::string discussion_answered = "\xEF\x93\x80";  // U+F4C0
  std::string checksuite = "\xEF\x90\x98";           // U+F418
  std::string default_ = "\xEE\xAA\x84";             // U+EA84
};

}  // namespace waku::github
