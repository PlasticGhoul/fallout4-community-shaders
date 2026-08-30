# AI Development Instructions

This repository is a fork of [Skyrim Community Shaders](https://github.com/community-shaders/skyrim-community-shaders)
being ported to Fallout 4. It is not a Skyrim project any more; the inherited Skyrim plugin sources
were removed and live on under the `skyrim-base` tag.

**Read these three, in this order, before proposing any work:**

1. `docs/fallout4-port/ROADMAP.md` — how the port is cut into subprojects, what is decided, what is
   still open. Decisions recorded there are not to be relitigated.
2. `docs/superpowers/specs/` — the design for the subproject currently in flight.
3. `.claude/CLAUDE.md` — build commands, architecture and conventions as they stand today,
   including an explicit list of what is temporarily moot.

Consulting the Skyrim implementation while porting is expected:
`git show skyrim-base:src/Features/Skylighting.cpp`.
