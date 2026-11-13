# Maintain the wiki

The source for these pages is [`docs/wiki`](https://github.com/JanDeDobbeleer/waku/tree/main/docs/wiki)
in the waku repository. Change it in the same pull request as any user-visible command, option,
default, QML API or installation change. Review the affected examples against the implementation.
Do not edit managed pages directly in the wiki: the next publish replaces them.

The `publish wiki` workflow copies Markdown pages from `main` after a source change, a published
release or a manual run. It requires the repository Actions secret `WIKI_TOKEN`: a token with
write access to this repository's wiki. GitHub's normal workflow token cannot push to the separate
wiki repository. Use a dedicated account or token, limit its access and rotate it when needed.
The workflow fails if the secret is missing or publishing fails; check the Actions run and retry.
It publishes the current `main`, not a versioned snapshot of the release.

The workflow updates named source pages but does not delete other wiki pages. If a managed page is
removed or renamed, remove the old page explicitly in the wiki and fix incoming links. The wiki
therefore remains readable to people while its source is available to agents and code review.

Before a release, review the [release checklist](https://github.com/JanDeDobbeleer/waku/blob/main/docs/release-checklist.md)
and confirm the publish workflow succeeded. There is no automatic check that prose matches code;
the change and release reviews must check that.
