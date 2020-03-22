/*
 *
 * PlayStation(R)Vita Virtual Mass Header
 *
 * Copyright (C) 2020 Princess of Slepping
 *
 */

#include <stdio.h>
#include <string.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/io/fcntl.h>
#include <psp2kern/io/stat.h>
#include <taihen.h>
#include "vmass.h"

#define VMASS_REQ_READ  (1 << 0)
#define VMASS_REQ_WRITE (1 << 1)
#define VMASS_REQ_DONE  (1 << 2)

const char umass_start_byepass_patch[] = {
	0x00, 0xBF, 0x00, 0xBF,
	0x00, 0xBF,
	0x00, 0xBF, 0x00, 0xBF,
	0x00, 0xBF,
	0x00, 0x20, 0x00, 0x00
};

SceUID vmass_evf_uid;
SceUID vmass_mtx_uid;

int g_res;
SceSize g_sector_pos;
void *g_data_for_read;
const void *g_data_for_write;
SceSize g_sector_num;

SceUsbMassDevInfo dev_info;
char img_path[0x400];

__attribute__((noinline))
int vmassRegisterPath(const char *path){

	int res;
	SceIoStat stat;
	SceSize path_len;

	res = ksceKernelLockMutex(vmass_mtx_uid, 1, NULL);
	if(res < 0)
		return res;

	path_len = strnlen(path, sizeof(img_path));
	if(path_len == sizeof(img_path)){
		res = -1;
		goto ret;
	}

	res = ksceIoGetstat(path, &stat);
	if(res < 0)
		goto ret;

	memcpy(img_path, path, path_len);

	dev_info.number_of_all_sector = (SceSize)(stat.st_size >> 9);
	dev_info.data_04              = 0;
	dev_info.sector_size          = 0x200;
	dev_info.data_0C              = 0;

	res = 0;

ret:
	ksceKernelUnlockMutex(vmass_mtx_uid, 1);

	return res;
}

__attribute__((noinline))
int vmassGetDevInfo(SceUsbMassDevInfo *info){

	ksceKernelLockMutex(vmass_mtx_uid, 1, NULL);

	if(img_path[0] == 0 || info == NULL)
		return -1;

	memcpy(info, &dev_info, sizeof(*info));

	ksceKernelUnlockMutex(vmass_mtx_uid, 1);

	return 0;
}

int vmass_io_thread(SceSize args, void *argp){

	SceOff offset;
	unsigned int outBits;
	SceUID fd;

	while(1){

wait_req:
		outBits = 0;

		ksceKernelWaitEventFlag(vmass_evf_uid, VMASS_REQ_READ | VMASS_REQ_WRITE, SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR_PAT, &outBits, NULL);

		if(outBits == VMASS_REQ_READ){

			fd = ksceIoOpen(img_path, SCE_O_RDONLY, 0777);
			if(fd < 0){
				g_res = fd;
				ksceKernelSetEventFlag(vmass_evf_uid, VMASS_REQ_DONE);
				goto wait_req;
			}

			offset = 0LL;
			offset = g_sector_pos;
			offset *= 0x200;

			ksceIoLseek(fd, offset, SCE_SEEK_SET);
			ksceIoRead(fd, g_data_for_read, g_sector_num * 0x200);
			ksceIoClose(fd);

			fd = 0;

			g_res = 0;
			ksceKernelSetEventFlag(vmass_evf_uid, VMASS_REQ_DONE);
			goto wait_req;

		}else if(outBits == VMASS_REQ_WRITE){

			fd = ksceIoOpen(img_path, SCE_O_WRONLY, 0777);
			if(fd < 0){
				g_res = fd;
				ksceKernelSetEventFlag(vmass_evf_uid, VMASS_REQ_DONE);
				goto wait_req;
			}

			offset = 0LL;
			offset = g_sector_pos;
			offset *= 0x200;

			ksceIoLseek(fd, offset, SCE_SEEK_SET);
			ksceIoWrite(fd, g_data_for_write, g_sector_num * 0x200);
			ksceIoClose(fd);

			fd = 0;

			g_res = 0;
			ksceKernelSetEventFlag(vmass_evf_uid, VMASS_REQ_DONE);
			goto wait_req;
		}

	}

	return ksceKernelExitDeleteThread(0);
}

__attribute__((noinline))
int vmassReadSector(SceSize sector_pos, void *data, SceSize sector_num){

	int res;

	ksceKernelLockMutex(vmass_mtx_uid, 1, NULL);

	g_sector_pos    = sector_pos;
	g_data_for_read = data;
	g_sector_num    = sector_num;

	ksceKernelSetEventFlag(vmass_evf_uid, VMASS_REQ_READ);
	ksceKernelWaitEventFlag(vmass_evf_uid, VMASS_REQ_DONE, SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR_PAT, NULL, NULL);

	res = g_res;

	ksceKernelUnlockMutex(vmass_mtx_uid, 1);

	return res;
}

__attribute__((noinline))
int vmassWriteSector(SceSize sector_pos, const void *data, SceSize sector_num){

	int res;

	ksceKernelLockMutex(vmass_mtx_uid, 1, NULL);

	g_sector_pos     = sector_pos;
	g_data_for_write = data;
	g_sector_num     = sector_num;

	ksceKernelSetEventFlag(vmass_evf_uid, VMASS_REQ_WRITE);
	ksceKernelWaitEventFlag(vmass_evf_uid, VMASS_REQ_DONE, SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR_PAT, NULL, NULL);

	res = g_res;

	ksceKernelUnlockMutex(vmass_mtx_uid, 1);

	return res;
}

// Load umass.skprx to avoid taiHEN's unlinked problem
int load_umass(void){

	SceUID umass_modid, patch_uid;

	// Since bootimage cannot be mounted after the second time, load vitashell's umass.skprx
	umass_modid = ksceKernelLoadModule("ux0:VitaShell/module/umass.skprx", 0, NULL);

	patch_uid = taiInjectDataForKernel(0x10005, umass_modid, 0, 0x1546, umass_start_byepass_patch, sizeof(umass_start_byepass_patch) - 2);

	int start_res = -1;
	ksceKernelStartModule(umass_modid, 0, NULL, 0, NULL, &start_res);

	taiInjectReleaseForKernel(patch_uid);

	return 0;
}

__attribute__((noinline))
int vmassInit(void){

	vmass_evf_uid = ksceKernelCreateEventFlag("VmassEvf", SCE_EVENT_WAITMULTIPLE, 0, NULL);

	vmass_mtx_uid = ksceKernelCreateMutex("VmassMtx", 0, 0, NULL);

	SceUID thid = ksceKernelCreateThread("VmassIoThread", vmass_io_thread, 0x60, 0x4000, 0, 0, NULL);
	ksceKernelStartThread(thid, 0, NULL);

	load_umass();

	return 0;
}

/* Local variables: */
/* tab-width: 4 */
/* End: */
/* vi:set tabstop=4: */
