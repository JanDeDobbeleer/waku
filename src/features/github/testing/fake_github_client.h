#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "features/github/github_api.h"
#include "features/github/qml/github_service.h"

namespace waku::github::testing {

// A stand-in for the GitHub API server: REST pages, the GraphQL call and nothing else.
class ScriptTransport final : public HttpTransport {
 public:
  HttpResponse send(const HttpRequest& request) override {
    requests.push_back(request);
    if (request.url.endsWith(QStringLiteral("/graphql"))) return {500, QStringLiteral("skip")};
    return handler(request);
  }
  std::function<HttpResponse(const HttpRequest&)> handler;
  std::vector<HttpRequest> requests;
};

class FakeClient final : public GithubClient {
 public:
  QString loadToken() override { return token; }
  bool saveToken(const QString& value) override {
    saved = value;
    return saveOk;
  }
  void fetch(const GithubFetchOptions& options,
             std::function<void(const GithubFetchResult&)> done) override {
    fetches.push_back(options);
    if (hold) {
      pending = std::move(done);
      return;
    }
    done(useTransport ? fetchGithubNotifications(transport, options) : canned);
  }
  void send(const HttpRequest& request, std::function<void(const HttpResponse&)> done) override {
    sent.push_back(request);
    done(respond ? respond(request) : HttpResponse{200, QStringLiteral("OK")});
  }

  // A made-up value; the tests never see a real token.
  QString token = QStringLiteral("test-token");
  QString saved;
  bool saveOk = true;
  bool hold = false;
  bool useTransport = false;
  GithubFetchResult canned;
  ScriptTransport transport;
  std::function<void(const GithubFetchResult&)> pending;
  std::function<HttpResponse(const HttpRequest&)> respond;
  std::vector<GithubFetchOptions> fetches;
  std::vector<HttpRequest> sent;
};

}  // namespace waku::github::testing
