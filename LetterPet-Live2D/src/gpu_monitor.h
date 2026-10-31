#ifndef GPU_MONITOR_H
#define GPU_MONITOR_H
#include "pet.h"
#include <pdh.h>

typedef enum {GPU_UNAVAILABLE,GPU_WARMUP,GPU_READY} GpuStatus;
typedef struct {
 GpuStatus status;
 double usage;
 unsigned engines,adapters;
 wchar_t engine[48];
} GpuReading;
typedef struct {
 PDH_HQUERY query;
 PDH_HCOUNTER counter;
 PDH_STATUS last_error;
 ULONGLONG retry_at;
 bool primed;
} GpuSampler;

void gpu_sample(GpuSampler *sampler,GpuReading *reading);
void gpu_close(GpuSampler *sampler);
bool gpu_selftest(FILE *log);
#endif
