#ifndef __LV_FS_LFS_H__
#define __LV_FS_LFS_H__

//把 LittleFS 注册为 LVGL 文件系统驱动 (盘符 'A')
//需在 lv_init() 之后、使用 "A:/xxx" 资源之前调用
void lv_fs_lfs_init(void);

#endif
