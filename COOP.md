# Co-op (two players)

Two players play one Fallout game together. Player 1 hosts and runs the
whole game; player 2 has their own character (stats, skills, perks, traits,
level, inventory, caps, kill counts) and plays along on the same map.

Co-op is off unless `fallout.cfg` has a `[coop]` section with
`enabled=1`. Without it the game behaves exactly like Fallout CE (this is
verified byte for byte against upstream, see "Tests").

## Fallout 2 (beta)

The package has Fallout 2 co-op too (`fallout2-ce`, built on Fallout 2
Community Edition). With Fallout 2 installed, the setup screen asks which
game to play (Enter picks the one played last); Fallout 2's settings are
in `fallout2.cfg`. It plays like Fallout 1 co-op, with these differences
for now:

- Player 2 trades through conversations, like player 1 (with TALK set to
  PLAYER 1 player 2 cannot trade).
- No look to pick for player 2: they look like the Chosen One does at that
  point of the story.
- Your companions (Sulik, Vic...) follow player 1's orders as usual.

Both players need Fallout 2 in the same version and language, like
Fallout 1. It is new: please report what breaks (MULTIPLAYER, REPORT
BUG).

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
- If either player dies, the game is lost. With KNOCKOUT on (MORE menu,
  or `knockout=1`; player 1's setting counts) a player who would die is
  knocked out instead while the other is still up: they can't be hurt
  while out and wake up by themselves or when healed (Stimpak, First
  Aid, Doctor). The last player standing dies as usual.
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
     or PRIVATE CODE (only with the game code). Online games go through
     the co-op server's relay, so nobody opens ports; they get a code like
     KQX472. Player 2 can join any time; they create their character when
     they join (or continue the one in the save).
   - **JOIN GAME** (player 2): the server browser lists open games on
     your network (found with a UDP search on the game port) and listed
     online; pick one, or ENTER ADDRESS and type an address or a game
     code. Your screen then shows the game from player 1's computer; F10
     leaves.
   - **MORE**: ONE PC (both players on one computer; press F9 in the
     game), OUTLINES (the players' outline colors on your screen),
     RESOLUTION (the screen size, used from the next start) and KNOCKOUT
     (knocked out instead of game over, see above).
   - **UPDATE**: looks for a newer version on the co-op server and, if
     you say INSTALL, downloads it, checks it, replaces the game's files
     and starts the game again.
   - **REPORT BUG**: sends a short description and the recent log to the
     co-op server (or saves it next to the game if there is none).

Both computers need the same Fallout version, language and patches; the
host refuses a client whose game data differs. On a LAN the host's
firewall must allow port 27015 (TCP to play, UDP to be found). Player 1
sends player 2 the picture: about 0.1-0.3 MB/s, scaled to player 2's
screen size and packed, and never more than player 2's line takes.

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

One PC: just `enabled=1`, then F9 in a game.

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
| `talk` | `0`: only player 1 talks to people (MORE, TALK). Default `1`: both do. |
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
