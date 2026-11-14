# GameWorldshaper — Samples & Templates

Starter templates and complete sample games for the [GameWorldshaper engine](https://github.com/orgolis/c-Engine-Game),
installed and launched through the [Hub](https://github.com/orgolis/WorldShaper-Hub).

This is a **separate repository on purpose**: sample content is large and binary, and it must not bloat the
engine repo. Git LFS is configured here from the first commit — the one mistake that ruins game repositories.

## Layout

```
templates/   one starter project per genre preset (from the engine's feature catalog)
samples/     small COMPLETE games, with source, one per genre
tools/       the regression harness that replays samples against a new engine version
```

## Why this repo also matters to the engine

Every sample is a **regression test**. If a sample breaks on a new engine version, a user's project will too —
so the Hub's *upgrade preflight* runs against this repo's content.

## Samples

| Sample | Needs engine | What it is |
|---|---|---|
| [TestGame](samples/TestGame) -- "The Beacon" | 0.8.43+ | A complete first-person game: hills, creature camps, eight guns in three weapon slots, armour, grenades, a skill tree, menus, HUD, settings, checkpoints and a goal -- all project files (scenes, `.ui`, logic scripts, `.items`). |

Open one from the Hub: **Projects > Add Existing Project...** and choose its folder (clone with Git LFS installed).

## Planned templates

Taken from the engine's genre coverage matrix: open-world RPG, souls-like action-RPG, looter-shooter, MMO-lite,
co-op horror, racing, survival horror, factory builder.

## Status

Bootstrapping. See the issues and the engine's
[`WORKFLOW_PLAN.md`](https://github.com/orgolis/c-Engine-Game/blob/main/docs/EngineMasterPlan/WORKFLOW_PLAN.md)
(Phase 6).
