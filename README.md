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
| [TestGame](samples/TestGame) -- "The Beacon" | 0.8.65+ | A complete first-person game, solo or online (co-op expedition, a raid, a duel; lobby codes, no server): 1024 m of hills in six lands with rivers, villages and outposts under an HDR sky, 84 enemies -- rigged soldiers that aim, flinch, fall limp, are knocked down by grenades and get up again -- creature camps, the Ender Leviathan as a raid boss (a native C++ script), eight guns, a sword that lunges, armour, grenades, abilities, a 50-skill tree, menus, HUD and settings -- all project files (scenes, `.ui`, logic scripts, `.items`, animation graphs and spring chains). |

**Download:** each sample is a zip on the [Releases](https://github.com/orgolis/WorldShaper-Samples/releases) page -- the newest TestGame is always at
https://github.com/orgolis/WorldShaper-Samples/releases/latest/download/TestGame.zip. Unzip it, then in the Hub:
**Projects > Add Existing Project...** and choose the unzipped folder. (Or clone this repository with Git LFS installed.)

**Releasing:** push a tag (`v1.1.0`, ...). The Release workflow opens every sample in the latest engine, then publishes
one zip per sample and template (`tools/package_samples.py`).

## Planned templates

Taken from the engine's genre coverage matrix: open-world RPG, souls-like action-RPG, looter-shooter, MMO-lite,
co-op horror, racing, survival horror, factory builder.

## Status

Bootstrapping. See the issues and the engine's
[`WORKFLOW_PLAN.md`](https://github.com/orgolis/c-Engine-Game/blob/main/docs/EngineMasterPlan/WORKFLOW_PLAN.md)
(Phase 6).
