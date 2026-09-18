/**
 * config.h —— settings.json 读写
 * ================================================================
 * 与 Python 版配置文件同路径同字段（%APPDATA%/AeroflyLink/settings.json），
 * 两个实现可互换读写。策略见 ADR 0003：**密码永不写入磁盘**——
 * cfg_save 输出的文档不含 password；cfg_load 遇到旧文件里的 password 忽略。
 */
#ifndef LINK_CONFIG_H
#define LINK_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include "link/json.h"

#define CFG_MAX_SERVERS 16

typedef struct {
    /* 连接 */
    char callsign[16];
    char cid[32];
    char password[64];        /* 仅内存，cfg_save 不落盘 */
    char realname[64];
    char server[128];
    int  port;
    int  rating;              /* 默认 FSD_RATING_DEFAULT (S1) */
    char eco[16];             /* vatsim | private | legacy */
    char type[16];            /* vatsim | legacy */
    char model[8];

    /* 模拟 DLL 初始位置（字符串形式，与 Python 配置一致，空 = 用默认） */
    char mock_lat[24];
    char mock_lon[24];
    char mock_alt[24];

    /* 服务器历史列表 */
    char servers[CFG_MAX_SERVERS][JSN_STR_CAP];
    size_t nservers;

    /* 飞行计划持久字段 */
    char fp_aircraft[8];
    char fp_wake[16];
    char fp_tas[8];
    char fp_dep[8];
    char fp_dest[8];
    char fp_altn[8];
    char fp_cruise[12];
    char fp_route[256];
    char fp_remarks[256];
    char fp_eet[8];
    char fp_endur[8];
} cfg_t;

/* 默认值（与 Python DEFAULT_* / 连接页默认一致） */
void cfg_defaults(cfg_t *c);

/* 读取；文件不存在/损坏时保持默认值并返回 0（非致命） */
int cfg_load(cfg_t *c, const char *path);

/* 保存（不含 password）；路径目录不存在时自动创建。0 成功 */
int cfg_save(const cfg_t *c, const char *path);

/* %APPDATA%/AeroflyLink/settings.json 的完整路径（写入住 provided 缓冲） */
void cfg_default_path(char *out, size_t cap);

#endif /* LINK_CONFIG_H */
