#include "lvgl.h"
#include "lfs.h"
#include "lfs_port.h"
#include "lv_fs_lfs.h"

static lv_fs_res_t lfs_res_map(int err)
{
	return (err >= 0) ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}

static bool lfs_ready_cb(lv_fs_drv_t *drv)
{
	(void)drv;
	return true;
}

static void *lfs_open_cb(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode)
{
	(void)drv;
	lfs_t *lfs = storage_lfs_get();
	lfs_file_t *f = lv_mem_alloc(sizeof(lfs_file_t));
	int flags = LFS_O_RDONLY;

	if (f == NULL)
		return NULL;

	storage_lfs_lock();
	if (mode & LV_FS_MODE_WR)
		flags = (mode & LV_FS_MODE_RD) ? LFS_O_RDWR : LFS_O_WRONLY;

	if (lfs_file_open(lfs, f, path, flags) < 0)
	{
		storage_lfs_unlock();
		lv_mem_free(f);
		return NULL;
	}
	storage_lfs_unlock();
	return f;
}

static lv_fs_res_t lfs_close_cb(lv_fs_drv_t *drv, void *file_p)
{
	(void)drv;
	lfs_file_t *f = file_p;
	storage_lfs_lock();
	int err = lfs_file_close(storage_lfs_get(), f);
	storage_lfs_unlock();
	lv_mem_free(f);
	return lfs_res_map(err);
}

static lv_fs_res_t lfs_read_cb(lv_fs_drv_t *drv, void *file_p, void *buf, uint32_t btr, uint32_t *br)
{
	(void)drv;
	storage_lfs_lock();
	int n = lfs_file_read(storage_lfs_get(), file_p, buf, btr);
	storage_lfs_unlock();
	*br = (n < 0) ? 0 : (uint32_t)n;
	return lfs_res_map(n);
}

static lv_fs_res_t lfs_write_cb(lv_fs_drv_t *drv, void *file_p, const void *buf, uint32_t btw, uint32_t *bw)
{
	(void)drv;
	storage_lfs_lock();
	int n = lfs_file_write(storage_lfs_get(), file_p, buf, btw);
	storage_lfs_unlock();
	*bw = (n < 0) ? 0 : (uint32_t)n;
	return lfs_res_map(n);
}

static lv_fs_res_t lfs_seek_cb(lv_fs_drv_t *drv, void *file_p, uint32_t pos, lv_fs_whence_t whence)
{
	(void)drv;
	int w = LFS_SEEK_SET;
	if (whence == LV_FS_SEEK_CUR) w = LFS_SEEK_CUR;
	else if (whence == LV_FS_SEEK_END) w = LFS_SEEK_END;
	storage_lfs_lock();
	{
		int err = lfs_file_seek(storage_lfs_get(), file_p, (lfs_soff_t)pos, w);
		storage_lfs_unlock();
		return lfs_res_map(err);
	}
}

static lv_fs_res_t lfs_tell_cb(lv_fs_drv_t *drv, void *file_p, uint32_t *pos_p)
{
	(void)drv;
	storage_lfs_lock();
	lfs_soff_t pos = lfs_file_tell(storage_lfs_get(), file_p);
	storage_lfs_unlock();
	if (pos < 0)
		return LV_FS_RES_UNKNOWN;
	*pos_p = (uint32_t)pos;
	return LV_FS_RES_OK;
}

void lv_fs_lfs_init(void)
{
	static lv_fs_drv_t fs_drv;

	lv_fs_drv_init(&fs_drv);
	fs_drv.letter = 'A';
	fs_drv.ready_cb = lfs_ready_cb;
	fs_drv.open_cb = lfs_open_cb;
	fs_drv.close_cb = lfs_close_cb;
	fs_drv.read_cb = lfs_read_cb;
	fs_drv.write_cb = lfs_write_cb;
	fs_drv.seek_cb = lfs_seek_cb;
	fs_drv.tell_cb = lfs_tell_cb;
	lv_fs_drv_register(&fs_drv);
}
