### Agent sessions recorded and replayed

- `AE3D_AGENT_RECORD=path` writes the agent channel's whole session to a
  file as it happens: every request and every answer, a line each, flushed
  as written (#416).
- `tools/agent_replay.ae` (on the new `ae3d.replay`) asks a running program
  the same questions and compares every answer with the recorded one:
  numbers within a relative tolerance, the rest exactly, the id and the
  clocks left out. Each difference is printed with its place in the answer,
  so a change that alters what the pipeline sees is found by replaying a
  session that went right.
- `tests/test_agent_record.ae` records a session and replays it twice: 4 of
  4 answers the same on the same scene; with the cube moved, the two that
  say where it started differ, named to the coordinate.
