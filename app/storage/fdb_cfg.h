#ifndef _FDB_CFG_H_
#define _FDB_CFG_H_

#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* 使用 KVDB */
#define FDB_USING_KVDB

/* 使用 FAL 存储模式 */
#define FDB_USING_FAL_MODE
/* NOR Flash 写粒度 1 bit */
#define FDB_WRITE_GRAN 1

#endif /* _FDB_CFG_H_ */
