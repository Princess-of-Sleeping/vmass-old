/*
 *
 * PlayStation(R)Vita Virtual Mass Header
 *
 * Copyright (C) 2020 Princess of Slepping
 *
 */

#ifndef _VMASS_H_
#define _VMASS_H_

#include <stdint.h>
#include <psp2/types.h>

typedef struct SceUsbMassDevInfo {
	SceSize number_of_all_sector;
	int data_04;
	SceSize sector_size;
	int data_0C;
} SceUsbMassDevInfo;

int vmassInit(void);
int vmassRegisterPath(const char *path);
int vmassGetDevInfo(SceUsbMassDevInfo *info);

int vmassReadSector(SceSize sector_pos, void *data, SceSize sector_num);
int vmassWriteSector(SceSize sector_pos, const void *data, SceSize sector_num);

#endif	/* _VMASS_H_ */

/* Local variables: */
/* tab-width: 4 */
/* End: */
/* vi:set tabstop=4: */
