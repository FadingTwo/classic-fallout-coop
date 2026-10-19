#ifndef DISPLAY_MONITOR_H
#define DISPLAY_MONITOR_H

namespace fallout {

int displayMonitorInit();
int displayMonitorReset();
void displayMonitorExit();
void displayMonitorAddMessage(char* string);
// CE: co-op: all messages to one player's monitor (index) alone until
// called with -1; the monitor of the player whose screen is drawn now.
void displayMonitorSetPrivate(int player);
void displayMonitorRefresh();
// Testing: the newest line on a player's monitor.
const char* displayMonitorLastLine(int player);
void displayMonitorDisable();
void displayMonitorEnable();

} // namespace fallout

#endif /* DISPLAY_MONITOR_H */
