#ifndef PET_BACKUP_IO_H
#define PET_BACKUP_IO_H
#include "pet.h"
bool atomic_write(const wchar_t *path,const BYTE *data,DWORD length);
bool data_backup_export(const wchar_t *source,const wchar_t *destination);
bool data_backup_validate(const wchar_t *source,const wchar_t *staging);
bool data_backup_restore(const wchar_t *source,const wchar_t *destination,const wchar_t *recovery);
bool backup_selftest(FILE *log);
#endif
