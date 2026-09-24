# Changes waiting for the changelog

A pull request says what it changed here, in a file of its own, instead of at
the top of `CHANGELOG.md`: there, every open pull request wrote to the same
line, and each merge left the next one in conflict.

Name the file for its date and what it is, `2026-09-24-net-events.md`, and
write it as the changelog's entries read: a `###` heading, then bullets that
say what changed, why, and what it was measured at.

`./build/fold_changes` (tools/fold_changes.ae) folds them into
`## [current]`, newest first, and deletes them; it runs in a pull request of
its own.
