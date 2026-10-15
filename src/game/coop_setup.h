#ifndef FALLOUT_GAME_COOP_SETUP_H_
#define FALLOUT_GAME_COOP_SETUP_H_

namespace fallout {

// First run: when the game data (MASTER.DAT, CRITTER.DAT) cannot be found
// with the current fallout.cfg, shows a setup screen - a Pip-Boy style
// terminal, as the game's own art is not available yet - that finds or
// asks for the Fallout installation and writes its paths to fallout.cfg.
//
// Call before the game initializes. Returns false if the player quit.
//
// Testing aids (environment): FALLOUT_SETUP_SEARCH adds folders to look in
// (separated by ';'), FALLOUT_SETUP_SCREENSHOT saves the first frame as a
// BMP, FALLOUT_SETUP_CHOOSE picks the n-th found installation.
bool coop_setup_ensure_game_data(int argc, char** argv);

// With Fallout 2 co-op (fallout2-ce) next to this game and Fallout 2
// installed, asks which game to play (the last one is preselected; saved
// as [coop]game in fallout.cfg) and starts Fallout 2 if picked. Returns 1
// to go on with Fallout 1, 0 to quit (Fallout 2 was started, or the player
// quit). FALLOUT_SETUP_GAME=1/2 picks without asking (tests).
int coop_setup_choose_game(int argc, char** argv);

} // namespace fallout

#endif /* FALLOUT_GAME_COOP_SETUP_H_ */
