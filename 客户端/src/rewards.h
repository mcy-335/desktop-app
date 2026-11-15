#ifndef PET_REWARDS_H
#define PET_REWARDS_H

#include "pet.h"

typedef enum {
 REWARD_NONE, REWARD_STAR, REWARD_HEART, REWARD_MOON, REWARD_LAMP, REWARD_BONSAI,
 REWARD_HOURGLASS, REWARD_TEA, REWARD_BOOKMARK, REWARD_STARLAMP, REWARD_IVORYTEA,
 REWARD_FERN, REWARD_PRISM, REWARD_ITEM_COUNT
} RewardAccessory;
typedef enum {
 REWARD_SLOT_LAMP, REWARD_SLOT_BONSAI, REWARD_SLOT_HOURGLASS, REWARD_SLOT_PLAQUE,
 REWARD_SLOT_TEA, REWARD_SLOT_BOOKMARK, REWARD_SLOT_COUNT
} RewardSlot;
typedef struct {RewardAccessory item[REWARD_SLOT_COUNT];} RewardEquipment;

void rewards_focus_time(double elapsed,bool focusing,bool in_round,bool allowed);
void rewards_progress(wchar_t *text,size_t capacity);
void rewards_tick(void);
void rewards_shutdown(void);
RewardEquipment rewards_equipment(void);
bool rewards_animate(void);
const wchar_t *rewards_sprite_name(RewardAccessory item);
bool rewards_selftest(FILE *log);
bool renderer_rewards_snapshot(RewardEquipment equipment,const wchar_t *path);
void renderer_draw_accessories(uint32_t *target,int size,RewardEquipment equipment,bool diy,int character);
long long rewards_balance(void);

#endif
