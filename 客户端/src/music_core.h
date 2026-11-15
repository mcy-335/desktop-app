#ifndef PET_MUSIC_CORE_H
#define PET_MUSIC_CORE_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>
#define MUSIC_MAX 128
#define MUSIC_PATH 1024
typedef enum { MUSIC_ORDER, MUSIC_REPEAT_ONE, MUSIC_REPEAT_ALL } MusicMode;
typedef struct { uint32_t id; int format; wchar_t source[MUSIC_PATH]; } MusicTrack;
typedef struct { MusicTrack tracks[MUSIC_MAX]; int count,volume,mode; uint32_t next_id; } MusicLibrary;
void music_defaults(MusicLibrary *library);
int music_format(const wchar_t *path);
bool music_local_path(const wchar_t *path);
int music_find_source(const MusicLibrary *library,const wchar_t *source);
int music_find_id(const MusicLibrary *library,uint32_t id);
bool music_append(MusicLibrary *library,uint32_t id,int format,const wchar_t *source);
bool music_remove(MusicLibrary *library,int index);
int music_next_index(int count,int current,int mode,int direction,bool ended);
void music_title(const MusicTrack *track,wchar_t *text,int capacity);
bool music_selftest(FILE *log);
#endif
