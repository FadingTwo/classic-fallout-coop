# Co-op (two to four players)

Two to four players play one Fallout game together. Player 1 hosts and
runs the whole game; the others (players 2 to 4) each have their own
character (stats, skills, perks, traits, level, inventory, caps, kill
counts) and play along on the same map. Below "player 2" stands for any of
them.

## Three or four players

- The host picks how many can join with PLAYERS when hosting (2, 3 or 4;
  `players` in `fallout.cfg`, default 4). A game that is full is told so
  and drops out of the server browser.
- Players join one after the other: player 3 after player 2 and so on.
  Someone who leaves and comes back takes the first free place (their
  character waits for them).
- Each player has their own view. Players 3 and 4 are outlined in colors
  the viewer doesn't use for players 1 and 2 (white and orange by
  default).
- On one PC, F9 goes round the players and Shift+F9 adds one more.
- HARDER (when hosting, or `harder=1`) makes enemies hold out longer the
  more of you play: as if they had 1.5 times their hit points with two
  players, twice with three and 2.5 times with four. Player 1's setting
  counts.
- The host draws every player's view, so four players need a bit more of
  the host's computer.

Co-op is off unless `fallout.cfg` has a `[coop]` section with
`enabled=1`. Without it the game behaves exactly like Fallout CE (this is
verified byte for byte against upstream, see "Tests").

## Fallout 2

The package has Fallout 2 co-op too (`fallout2-ce`, built on Fallout 2
Community Edition). With Fallout 2 installed, the launcher asks which
game to play (Enter picks the one played last); Fallout 2's settings are
in `fallout2.cfg`. It plays like Fallout 1 co-op, with these differences:

- With TALK set to PLAYER 1, player 2 still trades: walking up to a
  merchant opens their barter screen, with the wares they sell (their
  conversation runs, but player 2 only sees the trade).
- No look to pick for player 2: they look like the Chosen One of their
  own gender does at that point of the story (tribal, later the vault
  suit), or like their armor.
- Your companions (Sulik, Vic...) follow player 1's orders as usual.

Everyone plays the same game (Fallout 1, Fallout 2, Fallout: Sonora,
Fallout: Nevada, Fallout 1.5: Resurrection or Olympus 2207); since 1.13.3
its
release, language or patches may differ (the game runs on player 1's
computer; those players join as a new character). Fallout 2 co-op is
new: please report what breaks (MULTIPLAYER, REPORT BUG).

## Fallout: Sonora

[Fallout: Sonora](https://cambragol.github.io/Fallout-Sonora/) is a free
game on the Fallout 2 engine by the Nevada Band (English version by
cambragol). The launcher offers it as a third game: FALLOUT: SONORA. The
first time, it downloads Sonora's own release from its makers' GitHub
(version 1.16.4E, 1.7 GB; checked against its SHA-256) and unpacks the
game's files into a `sonora` folder next to this package; it is never part
of this package. Then it plays like Fallout 2 co-op, in English. Sonora's
terms: free, not for sale, for players over 18.

Credits: Fallout: Sonora is made by the Nevada Band; its English version
and release by cambragol
([GitHub](https://github.com/cambragol/Fallout-Sonora-English)). All credit
for the game goes to them; this mod only plays it.

Sonora is a game of its own for co-op: Sonora players only find and join
Sonora games, and the other way round. Everyone needs Sonora installed
the same way (the launcher does it). Its saves stay in its own folder.

## Fallout: Nevada

[Fallout: Nevada](https://fnevada.bgforge.net/) is a free game on the
Fallout 2 engine by the Nevada Band (Black Designer, FoN, Nobody's Nail
Machine, Master); its Extended Edition is by Pyran, on Foxx's Fixed
Edition. It needs Fallout 2. Install Fallout: Nevada Extended Edition
2.0.3.4 with its makers' own installer (inside or next to your Fallout 2
folder), then pick FALLOUT: NEVADA in the launcher. It looks for Nevada in
and under the Fallout 2 folders it finds, or asks where it is once (the
folder with PATCH000.DAT), and remembers it. The game runs in Nevada's
folder; if Nevada's settings can't find Fallout 2's MASTER.DAT and
CRITTER.DAT, they are pointed at your Fallout 2. Nothing of Nevada is
copied or included here. All credit for the game goes to its makers; this
mod only plays it.

Nevada is a game of its own for co-op: Nevada players only find and join
Nevada games. Everyone needs the same Nevada installed.

## Fallout 1.5: Resurrection

[Fallout 1.5: Resurrection](https://resurrection.cz/en/) is a free game on
the Fallout 2 engine by a team of fans from the Czech Republic, in English.
It needs Fallout 2. Pick FALLOUT 1.5: RESURRECTION in the launcher. The
first time, it downloads its makers' release (version 1.5, 340 MB; checked
against its SHA-256) and unpacks the game's files into a `resurrection`
folder next to this package, its settings pointed at your Fallout 2's
MASTER.DAT and CRITTER.DAT; nothing of Fallout 2's is changed. If you
installed it with its makers' installer (into a Fallout 2 folder), that one
is found and played instead. All credit for the game goes to its makers;
this mod only plays it.

Resurrection is a game of its own for co-op: Resurrection players only
find and join Resurrection games.

## Olympus 2207

[Olympus 2207](https://www.moddb.com/mods/olympus-2207) is a free game on
the Fallout 2 engine with a world, story and rules of its own, made by a
Russian team and translated into English. It brings its own MASTER.DAT and
CRITTER.DAT: no Fallout 2 files are needed. Install Olympus 2207 1.2
(English) with its makers' own installer into a folder of its own, then
pick OLYMPUS 2207 in the launcher. It looks for it next to the Fallout 2
folders it finds and in the usual folders for games, or asks where it is
once (the folder with OLYMPUS.CFG), and remembers it. Nothing of Olympus is
copied or included here. All credit for the game goes to its makers; this
mod only plays it.

Olympus is a game of its own for co-op: Olympus players only find and join
Olympus games.

## Rules

- Player 2 creates their own character on the regular character creation
  screen and picks a look (vault jumpsuit, leather jacket or robe). The
  other player is drawn with an outline: player 1 green, player 2 yellow
  (each player can pick other colors under MULTIPLAYER, MORE, OUTLINES).
- Both players get full XP for everything (kills, quests, skills),
  whoever did it. Each levels up and picks perks on their own.
- Karma and reputation are shared. Drug addictions are per player.
- Perks/traits that affect everyone (Jinxed, Friendly Foe) apply if
  either player has them.
- Both players talk to people, each as their own character: player 2's
  conversations use player 2's Speech, Intelligence and items, and play
  on player 2's screen while player 1 waits. People who start a
  conversation themselves talk to player 1. With TALK set to PLAYER 1
  (MORE menu, or `talk=0`; player 1's setting counts) only player 1
  talks, and player 2 walking up to a merchant opens the barter screen
  directly.
- Quests are shared (one game): whoever takes a quest or turns it in, it
  counts for both. Rewards from player 2's conversations (items, caps) go
  to player 2. A conversation that sends you somewhere, starts a fight or
  adds a companion does so for the whole group.
- Only player 1 travels (world map, exit grids), rests and saves. Player
  2 follows map and elevation changes automatically.
- In combat each player plays their own turn. `turn_time` limits a turn.
- If a player dies, the game is lost. With KNOCKOUT on (MORE menu,
  or `knockout=1`; player 1's setting counts) a player who would die is
  knocked out instead while another is still up: they can't be hurt
  while out and wake up by themselves or when healed (Stimpak, First
  Aid, Doctor). The last player standing dies as usual.
- Each player's camera follows their own character, however far apart
  you are, and each hears sound effects as loud as they are where they
  stand (music and speech are the same for everyone).
- Press T to chat (over the network). Enter sends, Esc cancels.
- Press G to mark the spot under your mouse for the other player; it
  blinks on both screens for five seconds.
- Click the other player (or talk to them) to give each other items in
  the loot screen; what they wear or hold stays out of it.
- Player 2 keeps a copy of their character (`coop-character.dat` next
  to their game), updated when player 1 saves. Joining a game that has no
  player 2 yet, they can pick it instead of making a new character, to
  take it into a friend's game or carry on if the host is gone. Item
  scripts don't come along (they belong to the game they came from).
- With two players, messages name the player ("Max Stone was hit")
  instead of saying "you", since both read them.
- If player 2 leaves, their character waits out of sight and comes back
  next to player 1 when they join again.
- When a player is in a menu (inventory, character screen, Pip-Boy,
  dialog, barter...) or it is their combat turn, the game waits for them
  and the other player sees their screen with a status line.

## Controller

An Xbox or PlayStation controller works, and so do the Steam Deck's own
controls (as a gamepad, not a mouse). Plug it in any time; the game uses
the first one it finds. The left stick moves the mouse cursor (slowly when
tilted a little, to hit a hex), the rest stand for keys:

| Button | Does | Button | Does |
|---|---|---|---|
| A | left click | B | right click (change cursor mode) |
| X | inventory (I) | Y | Pip-Boy (P) |
| View / Back | character screen (C) | Menu / Start | Escape |
| LB | skilldex (S) | RB | switch hand (B) |
| LT | attack mode (A) | RT | end turn (Space) |
| D-pad, right stick | scroll the map | L3 / R3 | Enter / co-op marker (G) |

The game still sees a mouse and keyboard, so everything works as before,
and a keyboard and mouse can be used at the same time. On one PC (ONE PC)
both players share it, like they share the mouse.

## Getting started

1. Start the game. The first time, if it cannot find your Fallout files,
   a setup terminal lists the installations it found (Steam, GOG, Heroic,
   ...). Pick yours, or type/paste the folder that has MASTER.DAT and
   CRITTER.DAT. Your saves stay in that installation's DATA folder.
2. In the main menu choose **MULTIPLAYER**:
   - **HOST GAME** (player 1): then NEW GAME or LOAD GAME, then one of
     LAN ONLY (same network), LIST ONLINE (in everyone's server browser)
     or WITH A FRIEND (only with the game code). Online games go through
     the co-op server's relay, so nobody opens ports; they get a code like
     KQX472. PLAYERS sets how many can join (2-4) and HARDER makes enemies
     tougher the more of you play. The others can join any time; they
     create their character when they join (or continue the one in the
     save).
   - **JOIN GAME** (player 2): the server browser lists open games on
     your network (found with a UDP search on the game port) and listed
     online; pick one, or ENTER ADDRESS and type an address or a game
     code. Your screen then shows the game from player 1's computer; F10
     leaves.
   - **MORE**: ONE PC (all players on one computer; press F9 in the
     game to switch, Shift+F9 to add a player), OUTLINES (the players' outline colors on your screen),
     RESOLUTION (the screen size, used from the next start) and KNOCKOUT
     (knocked out instead of game over, see above).
   - **UPDATE**: looks for a newer version on the co-op server and, if
     you say INSTALL, downloads it, checks it, replaces the game's files
     and starts the game again.
   - **REPORT BUG**: sends a short description and the recent log to the
     co-op server (or saves it next to the game if there is none).

Everyone needs the same game (Fallout 1 or Fallout 2) and the same mod
version; the game's release, language or patches may differ (a player
with other game files joins as a new character). On a LAN the host's
firewall must allow port 27015 (TCP to play, UDP to be found). Player 1
sends each other player their picture: only what changed since the last
one (moved with the camera when they walk), packed, about 0.05-0.15 MB/s,
and never more than their line takes.

## Without the menu

Everything can also be set in `fallout.cfg`. Host:

```ini
[coop]
enabled=1
mode=host
```

Client (starts straight into the host's game):

```ini
[coop]
enabled=1
mode=client
host=192.168.1.10
```

One PC: just `enabled=1`, then F9 in a game (Shift+F9 for a third or
fourth player).

## Co-op server (updates, bug reports, server browser, relay)

`tools/coop_server.py` is a small standard-library Python server for
UPDATE, REPORT BUG, crash reports, the online server browser and the relay
that online games go through (see `docs/coop/SERVER.md`). Run it on a
machine everyone can reach:

```sh
tools/coop_server.py --port 8080 --root /srv/fallout-coop
```

Put `latest.txt` and the packages in `/srv/fallout-coop/public`.
`tools/build-release.sh <out dir> http://your.server:8080` builds both
packages and writes a matching `latest.txt`:

```
version=1.1
notes=Fixes the trading crash
url_linux=http://your.server:8080/fallout-coop/fallout-coop-1.1-linux.tar.gz
url_windows=http://your.server:8080/fallout-coop/fallout-coop-1.1-windows.zip
sha256_linux=...
sha256_windows=...
```

The game installs a package only if its SHA-256 matches. Packages hold
one folder with the game's files; they are unpacked with the system's
`tar` (built into Windows 10 and later) over the game's folder.

Reports arrive in `/srv/fallout-coop/reports`. Build releases with the
server built in, so players need no setup:

```sh
cmake -B build -DCOOP_SERVER=http://your.server:8080
```

(`server=` under `[coop]` in `fallout.cfg` overrides it.) Without a
server the game still works on the LAN and with ENTER ADDRESS; the online
options and UPDATE need one.

## Settings

| Key | Meaning |
|---|---|
| `enabled` | `1` turns co-op on. |
| `mode` | `host`, `client`, or absent for both players on one computer. |
| `host` | Client: the host's address. |
| `port` | TCP port (default 27015). |
| `turn_time` | Seconds per combat turn for human players, `0` = no limit. |
| `outline_player1`, `outline_player2` | Outline colors on this screen (0 green, 1 yellow, 2 white, 3 orange, 4 pink, 5 blue). |
| `colorblind` | Older setting: `1` means white and orange when the two above are absent. |
| `server` | Co-op server for UPDATE, REPORT BUG and the server browser, `http://host:port`. |
| `relay` | Relay for online games: `http://host/fallout-coop/relay` or `host:port`. Default: the server's host, port 80. |
| `knockout` | `1`: a player who would die is knocked out instead while the other is up (MORE, KNOCKOUT). |
| `talk` | `0`: only player 1 talks to people (MORE, TALK). Default `1`: every player does. |
| `players` | Host: how many can play, `2` to `4` (PLAYERS when hosting). Default `4`. |
| `harder` | Host: `1` makes enemies hold out longer the more players there are (HARDER when hosting). |
| `selftest` | Developer: `1` enables the F8 self-test of player switching. |

## Saves

Saves made with a player 2 contain an extra co-op block at the end; they
are not meant to be loaded by builds without co-op. Saves without a player
2, and saves from the original game, are unchanged and load as before.

## Tests

The game has a headless test harness; `docs/coop/TESTING.md` describes it.
Tests need `FALLOUT_DATA` pointing at a Fallout installation, which is
never written to. Game data must never be committed to this repository.

How the co-op code works: `docs/coop/ARCHITECTURE.md`.
