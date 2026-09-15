#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "lfs.h"
#include "spi_flash.h"
#include "flash_partition.h"
#include "lfs_port.h"
#include "fal.h"
#include "flashdb.h"
#include "storage.h"


//FlashDB KVDB 瀹炰緥 (鐗? FDB 鍒嗗尯)
static struct fdb_kvdb kvdb;
static lfs_t *lfs;

bool storage_init(void)
{
	storage_lfs_lock_init();
	lfs = storage_lfs_get();

	if (!spi_flash_init())
	{
		printf("[STORAGE] spi flash init failed\n");
		return false;
	}

	int err = lfs_mount(lfs, storage_lfs_config());
	if (err < 0)
	{
		printf("[STORAGE] littlefs not found, formatting...\n");
		err = lfs_format(lfs, storage_lfs_config());
		if (err < 0)
		{
			printf("[STORAGE] littlefs format failed (%d)\n", err);
			return false;
		}
		err = lfs_mount(lfs, storage_lfs_config());
		if (err < 0)
		{
			printf("[STORAGE] littlefs mount failed (%d)\n", err);
			return false;
		}
	}
	storage_fdb_init();
	printf("[STORAGE] ready\n");
	return true;
}

bool storage_fdb_init(void)
{
	fal_init();

	if (fdb_kvdb_init(&kvdb, "env", "fdb", NULL, NULL) != FDB_NO_ERR)
	{
		printf("[FDB] kvdb init failed\n");
		return false;
	}
	printf("[FDB] kvdb ready\n");
	return true;
}

bool storage_wifi_set(const char *ssid, const char *pass)
{
	char *v;
	if (ssid == NULL || pass == NULL)
		return false;
	v = fdb_kv_get(&kvdb, "wifi_ssid");
	if (v == NULL || strcmp(v, ssid) != 0)
	{
		if (fdb_kv_set(&kvdb, "wifi_ssid", ssid) != FDB_NO_ERR)
			return false;
	}
	v = fdb_kv_get(&kvdb, "wifi_pass");
	if (v == NULL || strcmp(v, pass) != 0)
	{
		if (fdb_kv_set(&kvdb, "wifi_pass", pass) != FDB_NO_ERR)
			return false;
	}
	return true;
}

bool storage_wifi_get(char *ssid, size_t ssid_cap, char *pass, size_t pass_cap)
{
	char *v;
	if (ssid == NULL || pass == NULL)
		return false;
	v = fdb_kv_get(&kvdb, "wifi_ssid");
	if (v == NULL)
		return false;
	strncpy(ssid, v, ssid_cap - 1);
	ssid[ssid_cap - 1] = '\0';
	v = fdb_kv_get(&kvdb, "wifi_pass");
	if (v == NULL)
		return false;
	strncpy(pass, v, pass_cap - 1);
	pass[pass_cap - 1] = '\0';
	return true;
}
