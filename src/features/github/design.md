# wabar GitHub: design note

Status: decisions taken, built in this pass. Sits on `spike/qt-quick-bar` next to the Notifications
and Bluetooth widgets. Reference: Python YASB `yasb/github*` (branch `local/build`) and the Widgets
build's `widgets/github/` and `features/github/github_api.*`.

## What the widget does

A pill with the GitHub glyph and a notification dot (corner, colour and margin as Python's
`notification_dot`) while there are unread notifications. The label is `label`, or `labelAlt` with
`{data}` replaced by the unread count; a right click toggles between them.

A click opens a popup. Signed in, it lists the notifications grouped by category (Issue, Pull
Requests, ...) in `categoriesOrder` (then unknown categories in first-seen order), or as one flat
list when `showCategories` is off. A row shows the state icon, the title and
`repo - Updated <relative time>` (both cut at `maxFieldSize`), a comment count when
`showComments` is on, and is bold while unread. Clicking a row marks it read and opens its page in
the browser. The footer shows the unread count and "Mark all as read". With nothing to show the
popup says "No unread notifications". The data is polled every `interval`.

Not signed in, the popup shows a "Sign in" button instead (see Authentication).

Options (Python names, camel case): `label`, `labelAlt`, `interval` (ms; Python's `update_interval`
is seconds), `maxNotification`, `onlyUnread`, `showComments` (Python `show_comment_count`),
`maxFieldSize`, `categoriesOrder`, `showCategories`, `dotEnabled`, `dotCorner`, `dotColor`,
`dotMarginX`, `dotMarginY`, `reasonFilters`, `hideEmpty`, `commentGlyph`.

## What is reused and what is new

| Piece | Where | Use |
|---|---|---|
| REST fetch, paging, link header, reason filter, GraphQL enrichment (state, comment count) | `features/github/github_api.*` `fetchGithubNotifications` | as is, run on a `QThreadPool` thread with the blocking network transport |
| mark thread / all as read requests | `githubMarkThreadReadRequest`, `githubMarkAllReadRequest` | as is, sent with `sendHttpAsync` |
| token file, device flow requests and parsers | `githubTokenFilePath`, `readGithubTokenFile`, `saveGithubTokenFile`, `githubDeviceCodeRequest`, `parseGithubDeviceCode`, `githubAccessTokenRequest`, `parseGithubAccessToken` | as is |
| relative time, category titles, field truncation, category order, state classes | `features/github/github_format.*` (no UI) | as is |
| `GithubWidget`, `GithubDataStore`, `GithubAuthDialog` | `widgets/github/` | not used: QWidgets. Their logic (unread count, mark read, ordering, poll skip while a fetch runs, device-flow loop) is ported to the service |
| `wabar.services.GitHub` | `features/github/qml/github_service.*` | new: the singleton below |
| `GitHub.qml` | `features/github/qml/` | new |

## QML-facing API

`S.GitHub` (singleton, one per `wabar` run, shared by every GitHub widget):

- State: `available` (constant: false on the offscreen platform), `signedIn`, `unreadCount`, `items`
  (maps: `id`, `title`, `repo`, `type`, `category`, `categoryTitle`, `url`, `unread`, `comments`
  (-1 when unknown), `time` (relative text), `state` (e.g. "closed", "merged draft"), `icon`),
  `groups` (maps: `type`, `title`, `items`, ordered by `categoriesOrder`), `error` (last failure,
  empty when fine), `loading`.
- Sign-in state: `signingIn`, `userCode`, `verificationUri`, `signInError`.
- Options the widget sets: `interval`, `onlyUnread`, `maxNotification`, `showComments`,
  `reasonFilters`, `categoriesOrder`. A change schedules one refresh on the next event-loop turn
  (several setters at start-up cost one fetch).
- Invokables: `refresh()`, `open(id)` (marks the item read, returns its URL; the QML opens it with
  `S.Shell.open`), `markRead(id)`, `markAllRead()`, `signIn()`, `cancelSignIn()`,
  `truncate(text, n)`.
- Signal: `changed`.

## Authentication

The token is never in the config, in QML or in logs; `signedIn` is all QML sees.

1. At start-up the service reads `YASB_GITHUB_TOKEN`, else the token file the Python build and the
   Widgets build already use (`%LOCALAPPDATA%\YASB\github_token`). A user who signed in with either
   build is signed in here.
2. Without a token no request is sent (as Python's skipped poll), `signedIn` is false and the
   popup offers "Sign in".
3. `signIn()` runs the same OAuth device flow as Python's auth dialog: request a device code,
   expose `userCode` and `verificationUri` for the popup (which shows the code and an "Open GitHub"
   button that calls `S.Shell.open`), then poll the token endpoint every `max(interval, 5)` s,
   adding 5 s on `slow_down`. On success the token is saved to the token file, kept in memory and
   a refresh runs. `cancelSignIn()` or a failure stops the loop; a late response is ignored.

## Decisions

- One service, not a store per widget: Python shares one data manager among widgets with the same
  settings; the wabar bar has one set of options per run, so the last widget to set an option wins.
- Failure behaviour matches Python: a failed fetch logs, sets `error` and clears the list. A poll
  while another fetch runs is skipped; an option change during a fetch re-runs once afterwards.
- `open(id)` returns the URL instead of opening it, so the service has no desktop side effect
  and QML uses the one `Shell` path for every link.
- State icons come from the schema defaults (`GithubIconsConfig`), filled in the service; they
  are not an option in wabar yet.
- The real client (`GithubClient`) is only created off the offscreen platform; tests inject a fake
  and never touch the network or a real token.

## Left out

- Per-widget stores and per-widget polling settings.
- Keybindings, tooltip toggle, popup blur / corner / alignment options (wabar popups share one
  style).
- A token in the config (`token: env`); the environment variable and the token file replace it.
- Signing out (Python has none either; delete the token file).
- Opening the browser automatically at sign-in; the popup button does it.
