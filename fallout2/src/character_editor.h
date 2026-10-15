#ifndef CHARACTER_EDITOR_H
#define CHARACTER_EDITOR_H

#include "db.h"

namespace fallout {

extern int gCharacterEditorRemainingCharacterPoints;

int characterEditorShow(bool isCreationMode);
int _get_input_str(int win, int cancelKeyCode, char* text, int maxLength, int x, int y, int textColor, int backgroundColor, int flags);
void characterEditorInit();
bool _isdoschar(int ch);
char* _strmfe(char* dest, const char* name, const char* ext);
int characterEditorSave(File* stream);
int characterEditorLoad(File* stream);
void characterEditorReset();

} // namespace fallout

#endif /* CHARACTER_EDITOR_H */
