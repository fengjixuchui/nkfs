#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <memory.h>
#include <errno.h>
#include <malloc.h>

#include <include/ds_cmd.h>
#include <crtlib/include/crtlib.h>
#include <utils/ucrt/include/ucrt.h>


int ds_dev_add(const char *dev_name, int format);
int ds_dev_rem(const char *dev_name);

int ds_dev_query(const char *dev_name,
			struct ds_obj_id *psb_id);
int ds_obj_insert(struct ds_obj_id *sb_id, struct ds_obj_id *obj_id,
			u64 value, int replace);

int ds_obj_find(struct ds_obj_id *sb_id, struct ds_obj_id *obj_id,
			u64 *pvalue);

int ds_obj_delete(struct ds_obj_id *sb_id, struct ds_obj_id *obj_id);

int ds_server_stop(int port);
int ds_server_start(int port);