#ifndef LIVE2D_BRIDGE_H
#define LIVE2D_BRIDGE_H
#include <stdint.h>
#include <wchar.h>
/* Versioned C ABI: no C++ objects, CRT allocations or exceptions cross it.
   All calls belong to the creating UI thread; one model instance per process. */
#define PET_LIVE2D_ABI 2
typedef struct { int visible_keys,held_visible,last_vk,mouse_mask,arrived;float joints[5],eye,nearest_error,key_depth; } PetL2DState;
#ifdef __cplusplus
extern "C" {
#endif
typedef int (*PetL2DVersion)(void);
typedef void* (*PetL2DCreate)(const wchar_t*,const wchar_t*,int,char*,int);
typedef int (*PetL2DRender)(void*,int,float,int,uint32_t*,int,char*,int);
typedef void (*PetL2DDestroy)(void*);
typedef void (*PetL2DRestart)(void*);
typedef float (*PetL2DEye)(void*);
typedef void (*PetL2DKey)(void*,unsigned,int);
typedef void (*PetL2DMouse)(void*,int,int);
typedef void (*PetL2DClear)(void*);
typedef int (*PetL2DInspect)(void*,PetL2DState*,int);
#ifdef __cplusplus
}
#endif
#endif
