#ifndef __LFS_PORT_H__
#define __LFS_PORT_H__

#include "lfs.h"

//LittleFS 运行实例与底层配置 (片2, 整片16MB)
lfs_t *storage_lfs_get(void);
const struct lfs_config *storage_lfs_config(void);

//LittleFS access mutex (shared by LVGL and USB MTP)
void storage_lfs_lock_init(void);
void storage_lfs_lock(void);
void storage_lfs_unlock(void);

#endif
