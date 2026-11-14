# TestGame -- "The Beacon"

A small, complete first-person game made with GameWorldshaper's own tools. Clear the
creature camps in the hills, then reach the beacon on the far side.

Everything in it is ordinary project content -- scenes, UI documents, logic scripts,
item files -- so every part of it can be opened and changed in the editor. Nothing about
this game is built into the engine.

**Needs engine 0.8.43 or newer.**

## Open it

1. Install engine 0.8.43 (or newer) from the Hub.
2. Get this folder: clone this repository (with Git LFS installed -- the terrain is
   stored with it) or download it.
3. In the Hub: **Projects > Add Existing Project...** and choose this `TestGame` folder.
4. Open it, press **Play**.

## What is in it

- **The world** (`scenes/main.scene`): rolling hills with rivers, forests, grass and rocks.
- **The player**: first person, with sprint, crouch, slide, dash, mantle and climb. It has
  three weapon slots, holding a rifle and a pistol at the start, plus frag grenades,
  a Firebolt spell and potions.
- **Eight guns** (`assets/gameplay/arsenal.items`, `starter.items`): rifle, pistol,
  shotgun, marksman rifle, SMG, burst carbine, light machine gun and revolver. Six of
  them are in **supply caches**, along with armour (helmet, hood, vest) and more grenades.
  A gun found while all three slots are full goes into the bag. Use it from the inventory
  to swap it with the gun in hand.
- **Levels and a skill tree** (`assets/gameplay/hero.items`): XP for each creature, a
  skill point a level, three branches of skills and abilities.
- **Creature camps**: creatures patrol, notice the player, chase it and attack. Every
  third camp spits from range.
- **Screens** (`ui/*.ui`): title menu, HUD, inventory, skill tree, pause menu, settings
  (every control rebindable, mouse and graphics options) and a win screen.
- **The rules** (`testgame/*.lgraph`, logic scripts): `GameRules` (menus, keys, XP and
  loot, counting the creatures, the inventory and skill screens), `Checkpoint` (saves
  when you walk through one), `Goal` (the beacon).

## Keys

| | |
|---|---|
| Move | W A S D, Shift sprint, Space jump / mantle, Ctrl crouch / slide, V dash |
| Guns | left mouse fire, right mouse aim, R reload, 1 / 2 / 3 weapon slots, mouse wheel or Q switch |
| Other | G grenade, Z / X / C / F abilities, E open / pick up, I inventory, K skill tree |
| Game | Esc menu, F6 save, F9 load, Shift+F1 free the mouse for the editor |

All except Esc can be rebound in the game's Settings. The defaults are in `input.cfg`.

## Known limits

- Creatures are capsules (no rigged models yet), and they pass through trees.
- No gun models in the hand. A grenade goes off where the crosshair lands, and trees
  don't stop rounds or grenades.
- Starting Play bakes the navmesh from the full terrain, which pauses for a few seconds.

More detail: the engine's
[`docs/systems/TEST_GAME.md`](https://github.com/orgolis/c-Engine-Game/blob/main/docs/systems/TEST_GAME.md).
This sample is also a regression test. CI loads and runs it against every new engine
release.
