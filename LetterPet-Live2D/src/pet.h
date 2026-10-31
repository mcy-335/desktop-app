#ifndef PET_H
#define PET_H
#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <windowsx.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <wchar.h>
#include <string.h>
#define KEY_COUNT 86
#define SCENE_SIZE 1254.0
#define PI 3.14159265358979323846
typedef struct {double x,y;} Vec;
typedef struct {Vec shoulder,elbow,wrist,tip;} Pose;
typedef struct {const char *name;UINT vk;double x,y,w,h,amount,until;bool held;uint64_t order;} Key;
typedef struct {bool held;double amount,from,start,release;} MouseState;
typedef struct {Key keys[KEY_COUNT];int count,active;uint64_t order;Vec tip,from;double move_start,last;MouseState mouse[3];} Model;
typedef struct {int w,h;uint32_t *pixels;} Image;
typedef struct {int size,x,y;bool topmost,clickthrough,game,autostart,live2d;} Settings;
extern HWND pet_window;
extern HINSTANCE pet_instance;
extern Settings settings;
extern Model model;
extern wchar_t app_dir[MAX_PATH];
extern bool test_mode;
double clampd(double v,double lo,double hi);
double smooth(double v);
double now_ms(void);
void model_init(Model *m);
void model_key(Model *m,UINT vk,bool down,double now);
void model_mouse(Model *m,int b,bool down,double now);
bool model_tick(Model *m,double now);
Vec key_center(const Key *k,double amount);
Pose keyboard_pose(Vec tip);
Pose mouse_pose(const Model *m);
Vec skin_point(Vec q,const Pose *p,bool mouse);
bool model_selftest(FILE *log);
bool renderer_init(void);
void renderer_free(void);
bool renderer_resize(int size);
void renderer_draw(int frame,double now,const double pulses[3]);
void renderer_present(HWND w);
bool renderer_hit(int x,int y);
bool renderer_bmp(const wchar_t *path);
void live2d_init(int size);
void live2d_free(void);
bool live2d_available(void);
bool live2d_active(void);
bool live2d_draw(uint32_t *pixels,int size,double now);
void live2d_reset_clock(void);
void live2d_click(double now);
const wchar_t *live2d_error(void);
bool live2d_selftest(FILE *log);
void live2d_key(unsigned vk,bool down);
void live2d_mouse(int button,bool down);
void live2d_clear_input(void);
bool live2d_input_selftest(FILE *log);
int collectible_hit(int x,int y);
void monitor_show(void);
void monitor_close(void);
bool monitor_selftest(FILE *log);
void pet_menu(POINT p);
void pet_command(UINT id);
#define CMD_SHOW 100
#define CMD_TOP 101
#define CMD_PASS 102
#define CMD_GAME 103
#define CMD_STARTUP 104
#define CMD_RESET 105
#define CMD_MONITOR 106
#define CMD_QUIT 107
#define CMD_LIVE2D 108
#define CMD_SCALE 200
#endif
