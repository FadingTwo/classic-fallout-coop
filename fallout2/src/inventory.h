#ifndef INVENTORY_H
#define INVENTORY_H

#include <vector>

#include "obj_types.h"

namespace fallout {

typedef enum Hand {
    // Item1 (Punch)
    HAND_LEFT,
    // Item2 (Kick)
    HAND_RIGHT,
    HAND_COUNT,
} Hand;

typedef void InventoryPrintItemDescriptionHandler(char* string);

void _inven_reset_dude();
void inventoryOpen();
void _adjust_ac(Object* critter, Object* oldArmor, Object* newArmor);
void inventoryOpenUseItemOn(Object* targetObj);
Object* critterGetItem2(Object* critter);
Object* critterGetItem1(Object* critter);
Object* critterGetArmor(Object* critter);
Object* objectGetCarriedObjectByPid(Object* obj, int pid);
int objectGetCarriedQuantityByPid(Object* obj, int pid);
Object* _inven_find_type(Object* obj, int itemType, int* indexPtr);
Object* _inven_find_id(Object* obj, int id);
Object* _inven_index_ptr(Object* obj, int index);
// Makes critter equip a given item in a given hand slot with an animation.
// 0 - left hand, 1 - right hand. If item is armor, hand value is ignored.
int _inven_wield(Object* critter, Object* item, int hand);
// Same as inven_wield but allows to wield item without animation.
int _invenWieldFunc(Object* critter, Object* item, int hand, bool animate);
// Makes critter unequip an item in a given hand slot with an animation.
int _inven_unwield(Object* critter, int hand);
// Same as inven_unwield but allows to unwield item without animation.
int _invenUnwieldFunc(Object* critter, int hand, bool animate);
int inventoryOpenLooting(Object* looter, Object* target);
int inventoryOpenStealing(Object* thief, Object* target);
int inventoryOpenGiving(Object* giver, Object* receiver);
void inventoryOpenTrade(int win, Object* barterer, Object* playerTable, Object* bartererTable, int barterMod);
int _inven_set_timer(Object* item);
Object* inven_get_current_target_obj();

// Co-op: what a player's inventory run on their own computer changed
// (true: something to do in player 1's game, which then opens it again),
// and player 1's game doing it, in that player's scope.
bool inven_coop_take_changes(std::vector<unsigned char>* out);
void inven_coop_apply(const std::vector<unsigned char>& changes);
void inven_coop_test_drop_right_hand();
// Testing: changes for `command` (1 drop, 2 use, 3 unload, 4 reload: `item`
// the ammo, `other` the weapon, 5 put `item` into `other`).
void inven_coop_test_changes(int command, Object* item, int quantity, Object* other, std::vector<unsigned char>* out);

} // namespace fallout

#endif /* INVENTORY_H */
