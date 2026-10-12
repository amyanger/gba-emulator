# Emerald Companion

Emerald Companion is a side panel that shows live info from your Pokemon Emerald game: the wild Pokemon you're fighting, your party's hidden stats, and the encounter table for where you are. Press **F9** to open it next to the game.

**On this page:** [Battle](#battle) · [Party](#party) · [Route](#route) · [Mirage Island](#mirage-island) · [Nuzlocke](#nuzlocke) · [Berries](#berries) · [Search](#search) · [Details](#details) · [Overworld X-Ray](#overworld-x-ray)

> [!IMPORTANT]
> **Emerald Companion only works with the original, unmodified Pokemon Emerald (US/English) ROM.** This is the version it was built and tested with. It's the retail cartridge release, game code `BPEE`, sold in North America and Europe.
>
> - **ROM hacks are not supported**, even ones based on Emerald. The panel shows "ROM hack not supported" instead of guessing.
> - **Other languages** (Japanese, French, German, Spanish, Italian) and **other Pokemon games** (Ruby, Sapphire, FireRed, LeafGreen) are not supported. The panel shows "Game not supported".
>
> In every case the game itself still runs normally. Only the panel is unavailable.
>
> The check compares your whole ROM file against the original's CRC32 checksum, `1F1C08FB`. If you're not sure which version you have, load your game and press F9. If the panel doesn't say "not supported", your ROM works.

| Key | Panel action |
|-----|--------------|
| F9 | Show or hide the panel |
| F10 | Cycle the page: Auto, Party, Route, Nuzlocke, Berries, Search. Auto shows the battle page during a battle and the route page otherwise. When a battle starts the panel switches to Auto, and when it ends it goes back to the page you had, unless you changed the page during the battle. |
| [ and ] | Select the previous or next Pokemon on the Party page, switch views on the Nuzlocke page, or open the previous or next place on the Berries page |
| N | Start a Nuzlocke run, on the Nuzlocke page |
| J, twice | Move the clock to the next Mirage Island day, on the Route page |
| B | Turn berry alerts on or off, on the Berries page |
| / | Start typing a Pokemon name, on the Search page |

On macOS, hold **fn** with the F-keys (fn+F9, fn+F10), unless your keyboard is set to use F1, F2 and so on as standard function keys. `[`, `]`, `N`, `J`, `B` and `/` only work on their pages, and only when they are not bound to a GBA button in a custom keymap.

## Battle

The opposing Pokemon's species, level, HP, types, nature, IVs, ability and held item, how your active Pokemon's moves match up against it, the opponent's own moves and how they match up against your active Pokemon, and, in wild battles, the catch chance for each ball in your bag. For the opponent's moves the colors are from your side: red means a move is super effective against you, green means you resist it or are immune.

The panel switches to this page by itself when a battle starts, even if you were on Party or Route, and goes back to your page when the battle ends.

![Wild battle against a Poochyena, showing your Swampert's moves and the Poochyena's moves](screenshots/panel-battle.png)

## Party

Each Pokemon's nature, ability, held item, Hidden Power type and power, friendship, experience to the next level, IVs and EVs. Use `[` and `]` to move between Pokemon.

![Party page showing a Torchic's stats, IVs and EVs](screenshots/panel-party.png)

## Route

The name of the route, town or city you're in, read from the game's region map, and its wild encounter table with level ranges and odds. Pokemon you've caught are marked with a check.

![Route page listing Route 101's wild Pokemon](screenshots/panel-route.png)

### Mirage Island

Mirage Island is the rare island on Route 130 where Wynaut lives and the Liechi Berry grows. Emerald rolls a random number once a day, and the island is there only on days when that number matches part of a party Pokemon's personality value. With a full PC that can still be years apart, so most players never see it.

The Mirage Island card at the bottom of the Route page does the math for you:

- **Today.** Whether the island is up today, and which party Pokemon raises it. If the match is a Pokemon in your PC, the card names its box, so you can put it in your party.
- **Forecast.** The next three days one of your Pokemon will raise the island, counting your party and your whole PC.
- **Clock jump.** Press J twice within three seconds to move the cartridge clock to the first of those days.

![Lilycove City with the Route page: Mirage Island is not up today, Latios raises it in 2545 days and Salamence in 13064, and a hint to press J twice](screenshots/panel-mirage-forecast.png)

After the jump, the game catches up the next time you go through a door (not inside a Pokemon Center). Have the named Pokemon in your party when you get to Route 130, and the island is there. The card shows "Up today" in green, and the Route page lists the island's Wynaut.

![Standing on Mirage Island on Route 130, with the Route page saying Mirage Island is up today thanks to Latios and listing Wynaut in the grass](screenshots/panel-mirage-island.png)

Things to know about the clock jump:

- Only the clock moves. Your save data isn't edited, and the new time is kept in the save file, so it survives closing the emulator.
- Berries, Shoal Cave tides, the lottery and the game's other daily events move forward too.
- The clock only moves forward, and there's no way to move it back from here. Copy your `.sav` file first if you want to keep the old date.
- The forecast stops at the end of 2099, the last date the cartridge clock can hold.

## Nuzlocke

Tracks a Nuzlocke run for the save you're playing. Open the Nuzlocke page and press **N** to start. Pokemon you already have join the run, and the routes they were caught on count as used.

![Nuzlocke page on Route 118: every area used so far with what was met there, a missed Poochyena on Route 116, and the current route marked](screenshots/panel-nuzlocke-routes.png)

From then on the tracker watches the game and keeps score:

- **Routes.** The first wild encounter in each area counts: grass, water, fishing and Rock Smash in one area share it. The page lists every area you've used, what you met there and whether you caught it, missed it or it has since died. Your current location stays in view.
- **Dupes clause.** An encounter whose evolution line is already in your run doesn't use up the area. Neither does a shiny.
- **Gifts.** Pokemon you're given (starter, eggs, Castform, Beldum, fossils, trades) join the run without using up an area.
- **Deaths.** A Pokemon that faints is dead for good, in battle or from poison on the field. Healing it is fine. Sending it back into battle is flagged as a rule break.
- **Reloads.** Loading a savestate, rewinding, or loading an older in-game save is counted. If you go back to before a death, the death still counts and the run is marked "reloaded after death". Battling again on an area you'd already used after a reload is flagged as a re-roll.

Use `[` and `]` to switch between **Routes**, **Team** (everyone in the run: green in your party, grey in the PC, red if dead) and **Graveyard** (each death with where it happened and what did it, plus the rule checks).

![Team view next to the in-game party screen: the six party Pokemon in green and Castform in the PC](screenshots/panel-nuzlocke-team.png)

The run is saved as `<rom>.<trainer id>.nuzlocke` next to the ROM, one event per line. It is only ever added to, never rewritten. A new game or a different save file has a different trainer ID, so it gets its own run. To start over, delete that file.

Not tracked yet: level caps, badges, and trainer names on deaths.

## Berries

Keeps track of every berry tree in your game: what's growing, where it is, and when it needs you. Open the Berries page with F10.

- **Needs you.** At the top: trees that are ripe, about to drop their berries (red, under an hour left), or need water for the stage they're in, most urgent first.
- **All trees.** Below, one place at a time. The place you're standing on opens first and is marked "You are here". Each tree shows its growth stage, a drop for each of the four stages you can water it in (filled when watered, an orange ring when it needs water now, dark when that stage was missed), what happens next and how many berries to expect. Before a tree is ripe, the number is the most it can still give if you water every stage that's left. Other places sit underneath with a one-line summary. Use `[` and `]` to open them.
- **Empty soil.** Each place shows how many of its spots are free. With nothing planted, the page lists where the soft soil is.

![Route 102 just after planting a Liechi and a Pecha: the Berries page shows both trees watered once, with countdowns to the next stage and to ripe, and the game's own berry trees elsewhere marked ripe, not visited yet](screenshots/panel-berries-planted.png)

Times are real time, because Emerald's clock is. The countdowns keep moving between the game's own updates, and they match what the game will do when you next go through a door. The trees the game plants on its own show as "ripe, not visited yet" until you walk past them, since they don't grow before that.

![The same spot 13 hours later: the Pecha tree has berries on the map, and the page lists it under Needs you as ripe, with 10 hours before the berries drop](screenshots/panel-berries-ripe.png)

**Berry alerts.** While you play, with or without the panel open, a toast tells you when a tree becomes ripe, when it needs water for a new stage, when ripe berries are about to drop, and when you arrive somewhere with trees that need you. Trees on the map you're standing on stay quiet, events at the same place share one toast, and they wait until a battle is over. When you open a game, one toast sums up what needs you. Press **B** on the Berries page to turn alerts off or back on.

## Search

Type part of a Pokemon's name and see where to get it in Emerald. Open the Search page with F10, then press `/` or click the box to start typing. The list narrows on every keystroke: an exact name comes first, then names that start with what you typed, then names that contain it. Spaces and punctuation are ignored, so "mr mime" and "hooh" work, and "nidoran" finds both Nidorans.

- **Keys.** Up and Down pick a result, or `[` and `]` when you're not typing. PageUp, PageDown or the mouse wheel scroll the details. Enter or Esc stops typing, and Esc clears the text first if there is any.
- **While you type.** Keyboard keys don't reach the game or any hotkey, so typing a name won't press buttons. A controller still plays. Esc never quits the emulator on this page, and a battle starting while you type doesn't switch the page away.
- **Details.** The national dex number and a check if you've caught it, then every wild spot: Grass, Surf, Dive, Rock Smash and the Old, Good and Super Rod, each with levels and encounter chance. When floors of the same place differ, the chance is shown as a range.
- **Special cases.** Feebas appears on Route 119 with any rod, on one of 6 hidden tiles. Wynaut on Mirage Island shows up only on days the island appears. Places that open after the Hall of Fame are listed under "After the Hall of Fame", and Shoal Cave's low tide spots are marked "Low tide".
- **Not wild.** The page works back to the nearest Pokemon you can catch and lists the steps. For Gardevoir that is: evolve Kirlia at Lv 30, evolve Ralts at Lv 20, and Ralts is on Route 102. Pokemon that only hatch from eggs say so and tell you which evolved form to breed, and Azurill needs a parent holding Sea Incense. Anything else says "Not found in the wild in Emerald".

![Route 102 with "ra" typed in the Search page: Rattata, Raticate, Raichu and more narrowed down by name, with Ralts picked and its Route 102 grass spot on the right](screenshots/panel-search-typing.png)

![Searching "gardevoir" on Route 102: Gardevoir isn't wild, so the page says to evolve Kirlia at Lv 30 and Ralts at Lv 20, and that Ralts is caught in Route 102's grass](screenshots/panel-search.png)

Gifts, legendaries, trades and roaming Pokemon aren't covered yet. Like the rest of the panel, every name and location is read from your own ROM.

## Details

The panel sizes itself to the screen's pixel density, so it stays crisp on standard monitors, Windows display scaling, Retina and 4K. `--scale` only sets the size of the game. The panel is 600 by 480 points, the window is at least 480 points tall while the panel is open, and the game is centered vertically. In fullscreen (F11) the game grows to fill the screen with the panel beside it. On Linux X11 the panel uses the display DPI only when it indicates a high-DPI screen; otherwise it uses 1.0.

Every name and number is read from your own ROM at runtime, and nothing from the game ships with the emulator. The panel only reads memory. It never writes to the game, makes no bus accesses, does not change timing, and leaves savestates untouched. Addresses come from the pret/pokeemerald decomp.

Not shown yet: Safari Ball odds, mass outbreaks, exact Feebas tile positions, roamers, Altering Cave variants, lead ability effects (Static, Magnet Pull and similar), and Battle Pyramid and Battle Pike tables. Weather Ball matchups use its base type.

To check decoded state without a window, run headless with `--game-dump <file>` (see [Headless mode](usage.md#headless-mode)).

## Overworld X-Ray

Press F4 to draw what the game knows about the map on top of it. Each trainer gets a cone showing the tiles where they will spot you. A red cone means they still want to battle, and the label above them shows their class and sight range ("Youngster · 4 tiles"). A grey cone and a "Beaten" label mean you already won. Cones stop where the game's own line of sight stops, at walls, one-way tiles, height changes and other people. Hidden items you have not picked up yet get a small sparkle on their tile, including those just across a route border.

Items get names too. A small gold label shows what is inside each Poké Ball on the map ("Rare Candy") and what each hidden item is ("Heart Scale"). People who will give you something get a green label, such as "Gives TM36", or "Gives Mach Bike / Acro Bike" when you get to pick one. A gift label disappears once you have received it, the same moment the game stops offering it. Items handed out in cutscenes rather than by someone you talk to (the Itemfinder, HMs like Fly and Surf) are not labelled. Labels move beside their object when the space above it is taken, and they stay off trainer cones so they never hide the tiles you need to see.

The overlay hides itself in menus, dialog, battles, cutscenes and screen fades, and comes back when you can walk again. An X-RAY badge shows while it is on, and the setting is remembered the next time you open the emulator. Like the panel, it only reads memory and never changes the game.

For a picture without a window, run headless with `--overworld-out <file>` (see [Headless mode](usage.md#headless-mode)).
