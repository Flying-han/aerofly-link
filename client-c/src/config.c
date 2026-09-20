/* config.c —— settings.json 读写实现（字段与 Python 版兼容） */
#include "link/config.h"
#include "link/json.h"
#include "link/protocol.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

/* 限长拷贝（dst 为数组或带容量参数） */
static void copy_str(char *dst, size_t cap, const char *src)
{
    strncpy(dst, src ? src : "", cap - 1);
    dst[cap - 1] = '\0';
}

void cfg_defaults(cfg_t *c)
{
    memset(c, 0, sizeof(*c));
    copy_str(c->server, sizeof(c->server), "sweatbox.vatsim.net");
    c->port = 6809;
    c->rating = FSD_RATING_DEFAULT;
    copy_str(c->eco, sizeof(c->eco), "private");
    copy_str(c->type, sizeof(c->type), "legacy");
    copy_str(c->mock_lat, sizeof(c->mock_lat), "51.4775");
    copy_str(c->mock_lon, sizeof(c->mock_lon), "-0.4614");
    copy_str(c->mock_alt, sizeof(c->mock_alt), "3500");
    copy_str(c->servers[0], JSN_STR_CAP, "sweatbox.vatsim.net");
    copy_str(c->servers[1], JSN_STR_CAP, "fsd.vatsim.net");
    copy_str(c->servers[2], JSN_STR_CAP, "127.0.0.1:6809");
    c->nservers = 3;
    copy_str(c->fp_wake, sizeof(c->fp_wake), "Medium");
}

/* 安全读字符串键（缺键保留现值） */
static void get_str(const char *doc, const char *key, char *dst, size_t cap)
{
    char tmp[512];
    if (jsn_string(doc, key, tmp, sizeof(tmp)))
        copy_str(dst, cap, tmp);
}

int cfg_load(cfg_t *c, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;   /* 首次运行，保持默认 */
    char doc[16384];
    size_t n = fread(doc, 1, sizeof(doc) - 1, f);
    fclose(f);
    doc[n] = '\0';

    double num;

    get_str(doc, "callsign", c->callsign, sizeof(c->callsign));
    get_str(doc, "cid", c->cid, sizeof(c->cid));
    get_str(doc, "realname", c->realname, sizeof(c->realname));
    get_str(doc, "server", c->server, sizeof(c->server));
    get_str(doc, "model", c->model, sizeof(c->model));
    get_str(doc, "eco", c->eco, sizeof(c->eco));
    get_str(doc, "type", c->type, sizeof(c->type));

    if (jsn_number(doc, "port", &num) && num > 0 && num < 65536)
        c->port = (int)num;
    if (jsn_number(doc, "rating", &num) && num >= 1 && num <= 4)
        c->rating = (int)num;

    get_str(doc, "mock_lat", c->mock_lat, sizeof(c->mock_lat));
    get_str(doc, "mock_lon", c->mock_lon, sizeof(c->mock_lon));
    get_str(doc, "mock_alt", c->mock_alt, sizeof(c->mock_alt));

    /* 服务器历史：读到多少存多少（兼容旧版 per-ECO dict 形态） */
    jsn_read_servers(doc, "servers", c->servers, CFG_MAX_SERVERS, &c->nservers);

    /* 飞行计划持久字段 */
    get_str(doc, "aircraft", c->fp_aircraft, sizeof(c->fp_aircraft));
    get_str(doc, "wake_category", c->fp_wake, sizeof(c->fp_wake));
    get_str(doc, "tas", c->fp_tas, sizeof(c->fp_tas));
    get_str(doc, "dep_airport", c->fp_dep, sizeof(c->fp_dep));
    get_str(doc, "dest_airport", c->fp_dest, sizeof(c->fp_dest));
    get_str(doc, "alt_airport", c->fp_altn, sizeof(c->fp_altn));
    get_str(doc, "cruise_alt", c->fp_cruise, sizeof(c->fp_cruise));
    get_str(doc, "route", c->fp_route, sizeof(c->fp_route));
    get_str(doc, "remarks", c->fp_remarks, sizeof(c->fp_remarks));
    get_str(doc, "eet", c->fp_eet, sizeof(c->fp_eet));
    get_str(doc, "endurance", c->fp_endur, sizeof(c->fp_endur));

    return 0;
}

int cfg_save(const cfg_t *c, const char *path)
{
    /* 确保目录存在（...\AeroflyLink\） */
    char dir[MAX_PATH];
    copy_str(dir, sizeof(dir), path);
    char *slash = strrchr(dir, '\\');
    if (slash) {
        *slash = '\0';
        CreateDirectoryA(dir, NULL);   /* 已存在时静默失败，无妨 */
    }

    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;

    /* ADR 0003：刻意不写 password */
    fprintf(f,
        "{\n"
        "  \"callsign\": \"%s\",\n"
        "  \"cid\": \"%s\",\n"
        "  \"realname\": \"%s\",\n"
        "  \"server\": \"%s\",\n"
        "  \"port\": %d,\n"
        "  \"rating\": %d,\n"
        "  \"eco\": \"%s\",\n"
        "  \"type\": \"%s\",\n"
        "  \"model\": \"%s\",\n"
        "  \"mock_lat\": \"%s\",\n"
        "  \"mock_lon\": \"%s\",\n"
        "  \"mock_alt\": \"%s\",\n"
        "  \"aircraft\": \"%s\",\n"
        "  \"wake_category\": \"%s\",\n"
        "  \"tas\": \"%s\",\n"
        "  \"dep_airport\": \"%s\",\n"
        "  \"dest_airport\": \"%s\",\n"
        "  \"alt_airport\": \"%s\",\n"
        "  \"cruise_alt\": \"%s\",\n"
        "  \"route\": \"%s\",\n"
        "  \"remarks\": \"%s\",\n"
        "  \"eet\": \"%s\",\n"
        "  \"endurance\": \"%s\",\n"
        "  \"servers\": [",
        c->callsign, c->cid, c->realname, c->server, c->port, c->rating,
        c->eco, c->type, c->model,
        c->mock_lat, c->mock_lon, c->mock_alt,
        c->fp_aircraft, c->fp_wake, c->fp_tas,
        c->fp_dep, c->fp_dest, c->fp_altn, c->fp_cruise,
        c->fp_route, c->fp_remarks, c->fp_eet, c->fp_endur);

    for (size_t i = 0; i < c->nservers; i++)
        fprintf(f, "%s\n    \"%s\"", i ? "," : "", c->servers[i]);

    fprintf(f, "%s]\n}\n", c->nservers ? "\n  " : "");
    fclose(f);
    return 0;
}

void cfg_default_path(char *out, size_t cap)
{
    char appdata[MAX_PATH];
    UINT n = GetEnvironmentVariableA("APPDATA", appdata, sizeof(appdata));
    if (n == 0 || n >= sizeof(appdata))
        copy_str(appdata, sizeof(appdata), "C:\\ProgramData");
    _snprintf(out, cap - 1, "%s\\AeroflyLink\\settings.json", appdata);
    out[cap - 1] = '\0';
}
