# TestGame -- "The Beacon"

A complete first-person game made with GameWorldshaper's own tools: clear the soldiers'
outposts and the creature camps in the hills -- alone or with friends online -- take on
the Ender Leviathan in a raid, or face a friend in a duel.

Everything in it is ordinary project content -- scenes, UI documents, logic scripts, item
and effect files, one native script -- so every part of it can be opened and changed in the
editor. Nothing about this game is built into the engine.

**Needs engine 0.8.66 or newer.**

## Open it

1. Install engine 0.8.66 (or newer) from the Hub.
2. Download [TestGame.zip](https://github.com/orgolis/WorldShaper-Samples/releases/latest/download/TestGame.zip)
   and unzip it (or clone this repository with Git LFS installed -- the terrain, sky and models are stored with it).
3. In the Hub: **Projects > Add Existing Project...** and choose this `TestGame` folder.
4. Open it, press **Play** -- or, on Windows, double-click `Play TestGame.cmd` (the game in
   its own window, with the newest engine the Hub installed). `Play Leviathan.cmd` opens the
   Leviathan encounter on its own (`leviathan.schizo`).

**The Leviathan is a native script** (`assets/scripts/ender_leviathan.cpp`): the engine
compiles it with g++ (MinGW-w64) when the scene starts. Have g++ on PATH, or point
`GWS_SCRIPT_CXX` at it (`C:\Strawberry\c\bin\g++.exe` is found on its own). Without a
compiler the rest of the game plays; the Leviathan does not act.

## Modes

The title screen (`scenes/start.scene`) offers:

- **Co-op Expedition** (`scenes/coop_world.scene`) -- Solo, or online with up to four players.
- **Leviathan Raid** (`scenes/leviathan_raid.scene`) -- Solo, or online with up to four players.
- **PvP Duel** (`scenes/duel_arena.scene`) -- two players, first to three rounds.

**Online** gives you a six-character code to send your friends; they type it under
**Join a friend**. The games find each other on the local network and over the internet
and connect directly -- no server to run (for the networks they cannot get through, see
`config/multiplayer.ini`). **Host IP** / **Join by IP** connect by address on a LAN.

**Your progress goes with you** from one mode to the next -- level, XP, skill points, the
skill tree, the abilities in their slots, the bag, the gear worn and the guns: it is kept on
your own computer (`saves/profile.sav`; an exported game keeps it in the player's own
folder) and saved every 15 seconds and whenever you leave a mode. Online, each guest brings
their own. Every mode starts at full health; the duel keeps its own rules. Delete
`saves/profile.sav` to start over; `keep_progress=0` in `config/multiplayer.ini` turns it off.

## What is in it

- **The world**: 1024 m of rolling hills in six lands (painted biomes) -- a meadow where you
  start, a pine forest, autumn woods, dry plains, snowy highlands round the beacon and
  wetland along the rivers -- with four rivers and their lakes, villages, a farm, ruins, log
  cabins and soldiers' outposts, under an HDR sky. Grass moves in the wind; trees and rocks
  stop the player, rounds, grenades, sight and creatures. (`scenes/main.scene` is the
  original single-player world.)
- **84 enemies**: 37 soldiers at ten outposts and 47 creatures in thirteen camps.
- **Soldiers** (`assets/gameplay/soldiers.items`, `assets/models/military/`): riflemen and
  heavy gunners who patrol, spot you, turn and aim at you (up and down hills), shoot, flinch
  when hit and go limp when killed. Thrown down by a grenade that leaves them alive, they
  hold their fire while they lie and get up again -- off their back or face down -- from
  where they landed.
- **Their animation** (33 clips each, made procedurally in Blender): every soldier and the
  player's own body run an animation graph -- walking and running blended every way at the
  speeds their clips were measured to move at, aiming and shots on the upper body -- with
  feet planted on the slopes they stand on and held where they step, a body that leans into
  its moves, the support hand on the gun, ragdolls, and the heavy gunner's radio antenna
  swaying as he moves.
- **The Ender Leviathan** (`scenes/leviathan_raid.scene`, `assets/scripts/ender_leviathan.cpp`):
  a raid boss with 60,000 health and three phases -- a dorsal salvo, a beam, a bite from
  above, a tail strike, a current, a roar, meteors and teleports -- whose blows land on its
  animation's own cues.
- **The player**: first person -- a rigged soldier (seen in its shadow), the gun in hand seen
  and animated -- with sprint, crouch, slide, dash, mantle and climb. Three weapon slots
  hold a rifle, a pistol and a sword at the start (swung at someone a step out of reach, it
  lunges onto them), plus frag grenades (aim up to throw far; they knock down whoever they
  do not kill within 7 m), a Firebolt spell and health potions.
- **Eight guns** (`assets/gameplay/arsenal.items`): rifle, pistol, shotgun, marksman rifle,
  SMG, burst carbine, light machine gun and revolver, most of them in supply caches, along
  with armour (helmet, hood, vests) and more grenades.
- **Levels and a skill tree** (`assets/gameplay/hero.items`): XP for each kill and skill
  points each level for a tree of 50 skills in Sorcery, Survival and Arms & Mobility --
  abilities to learn (Rail Lance, Bulwark, Renewal, Overdrive, movement ones) and passives.
  No mana: abilities cost only their cooldowns; three ability slots hold the ones in use.
- **Creature camps**: creatures see you only with a clear line, hear gunfire and call their
  camp in; melee creatures rear up before they claw, spitters arc globs that can be dodged
  (`assets/gameplay/creatures.items`).
- **Effects** (`assets/gameplay/effects.items`): muzzle flashes that light their
  surroundings, tracers, sparks, smoke, fiery trails, blasts that glow and bloom.
- **Screens** (`ui/*.ui`): title and mode menu, HUD, inventory, skill tree, abilities,
  pause menu, settings (every control rebindable) and a win screen.
- **The rules** (`testgame/*.lgraph`, logic scripts): `GameRules` (menus, keys, XP and
  loot, counting the enemies, the screens), `Checkpoint` (saves when you walk through one),
  `Goal` (the beacon).

## Keys

| | |
|---|---|
| Move | W A S D, Shift sprint, Space jump / mantle, Ctrl crouch / slide, V dash |
| Guns | left mouse fire, right mouse aim, R reload, 1 / 2 / 3 weapon slots, mouse wheel or Q switch |
| Other | G grenade, Z / X / C abilities, E open / pick up, I inventory, K skill tree, P abilities |
| Game | Esc menu, F6 save, F9 load, Shift+F1 free the mouse for the editor |

All except Esc can be rebound in the game's Settings. The defaults are in `input.cfg`.
Esc belongs to the game (`escape = game` in `project.schizo`): its own Session Menu, not the
engine's.

## Known limits

- Creatures are capsules (the soldiers are rigged models); they steer round trees but can
  get stuck in a dense thicket.
- Starting Play bakes the navmesh from the full terrain, which pauses for a few seconds.
- Online guests do not see knockdowns yet (the host does), and the player is never knocked
  down.

## Credits

The sky is [Kloofendal 48d Partly Cloudy (Pure Sky)](https://polyhaven.com/a/kloofendal_48d_partly_cloudy_puresky)
from Poly Haven, CC0.

More detail: the engine's
[`docs/systems/TEST_GAME.md`](https://github.com/orgolis/c-Engine-Game/blob/main/docs/systems/TEST_GAME.md)
and [`docs/systems/CHARACTERS.md`](https://github.com/orgolis/c-Engine-Game/blob/main/docs/systems/CHARACTERS.md).
This sample is also a regression test. CI loads and runs it against every new engine
release.
