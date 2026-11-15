#ifndef LETTERPET_UI_THEME_H
#define LETTERPET_UI_THEME_H
#include "pet.h"

enum {UI_THEME_COUNT=5};
typedef struct {
 COLORREF background,surface,input,border,accent,ink,muted,primary,on_primary;
 COLORREF hero,highlight,warning,on_accent;
 int radius;
 bool light;
} UiColors;
extern const UiColors *ui_colors;
const UiColors *ui_theme_colors(int index);
void ui_theme_select(int index);
#define UI_BG (ui_colors->background)
#define UI_SURFACE (ui_colors->surface)
#define UI_INPUT (ui_colors->input)
#define UI_BORDER (ui_colors->border)
#define UI_GOLD (ui_colors->accent)
#define UI_INK (ui_colors->ink)
#define UI_MUTED (ui_colors->muted)
#define UI_WINE (ui_colors->primary)

typedef enum {UI_NORMAL,UI_TITLE,UI_SURFACE_TEXT,UI_FOCUS_HERO,UI_CARE_METRICS,UI_READOUT} UiRole;
/* Fonts returned here are owned by the caller. Private font loading is process-local. */
HFONT ui_theme_font(int height,int weight,bool numeric);
const wchar_t *ui_theme_face(void);
void ui_theme_apply(HWND window);
void ui_theme_preview(HWND window,int index);
void ui_theme_role(HWND control,UiRole role);
void ui_theme_caption(HWND window);
void ui_theme_fill(HDC dc,const RECT *rect,COLORREF color);
COLORREF ui_theme_mix(COLORREF base,COLORREF tint,int percent);
void ui_theme_release(void);
bool ui_theme_selftest(FILE *log);
#endif
