#ifndef MAIN_H
#define MAIN_H

namespace fallout {

int falloutMain(int argc, char** argv);

// Co-op tests: the game's own main loop, as after NEW GAME; whether it
// ended in death.
bool main_game_loop_for_test();

} // namespace fallout

#endif /* MAIN_H */
