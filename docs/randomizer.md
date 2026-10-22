# Randomizer

An opt-in mode for Pokémon Emerald that shuffles which Pokemon you find and fight. It is off by default and is set per save.

It works on the original US Emerald only. Other games and ROM hacks run unchanged.

## Turning it on

Open Settings (Ctrl+, or Cmd+,), go to the **Randomizer** page (the last one in the sidebar) and tick **Randomize Pokemon**. The page shows the seed as `XXXX-XXXX`. **New seed** rolls a different one. The page is always there, but the toggle is only enabled for the original US Pokémon Emerald with a save file. Otherwise it is greyed out with "Pokemon Emerald (US) only." The change takes effect the next time you open the game. A toast on load says `Randomizer on` with the seed.

## What changes

- **Starters.** The three starters are replaced. Each one is guaranteed a damaging move by level 5, so the first rival fight on Route 103 can always be won.
- **Wild Pokemon.** Every grass, water, fishing and Rock Smash slot, including the Safari Zone, Altering Cave and Mirage Island.
- **Trainers.** Every trainer team: gym leaders, the Elite Four, your rival, Team Aqua and Team Magma. Trainers with a set moveset get the new Pokemon's own level-up moves instead. Shedinja is never put on a trainer team.

Any non-legendary species can show up anywhere. There is no matching by strength, so expect odd fights.

## What stays the same

- **Legendaries** stay where they are and are never rolled for a random slot.
- **Feebas** keeps its Route 119 slot, so the Feebas finder in [Overworld X-Ray](pokemon-companion.md#feebas-tiles) still works.
- **Scripted one-offs:** Birch's rescue Zigzagoon, the Wally tutorial, Steven's partner team, Kecleon, Voltorb and Electrode, Sudowoodo, the Beldum gift, fossils, eggs and Castform. Roaming Pokemon are untouched too.
- **In-game trades** and the **Battle Frontier**, including the Pike and the Pyramid, the Battle Tents (Slateport, Verdanturf, Fallarbor) and the Trainer Hill, which use their own tables.

## Your files

Your `.gba` and `.sav` are never edited. The randomizer rewrites the game's tables in memory each time it starts. The seed is kept in `<save>.rando` next to the save, as one line such as `seed 1A2B3C4D v1`.

Deleting that file, or unticking the box and reopening the game, turns the randomizer off. Pokemon you caught while it was on stay in your save.

## Save states and the Companion

Save states load whether the randomizer is on or off and with any seed, so your existing states keep working. The Pokemon in them are stored in the save data, not the tables.

The [Pokémon Companion](pokemon-companion.md) pages (Route, Search, Hunt, Run) show the randomized Pokemon, since they read the same tables.

## Testing

Headless runs never read a `.rando` file. Use `--rando-seed <hex>` to randomize one run; see [Headless mode](usage.md#headless-mode).
