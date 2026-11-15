#ifndef LETTERPET_ACCOUNT_H
#define LETTERPET_ACCOUNT_H
#include "pet.h"
#include "account_net.h"

void account_init(const wchar_t *guest_settings);
bool account_require_login(const wchar_t *return_notice);
void account_activate_startup(void);
const wchar_t *account_profile_settings(void);
void account_show(void);
void account_poll(void);
void account_close(void);
bool account_dialog_message(MSG *message);
bool account_connection(AccountServer *server,char token[65]);
void pet_switch_profile(const wchar_t *settings_path);
bool pet_flush_profile(void);
void pet_end_session(const wchar_t *notice);

#endif
