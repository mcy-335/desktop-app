#ifndef PET_RESOURCES_H
#define PET_RESOURCES_H
#include "pet.h"
typedef struct {double cpu,memory,gpu,when;} ResourceSample;
bool resources_start(HWND owner);
void resources_stop(HWND owner);
#endif
