/* config.c —— settings.json 读写；旧 type/eco 键只读兼容 */
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

/* Write one UTF-8 JSON string without allowing user text to alter the document. */
static int write_json_string(FILE *f, const char *value)
{
    if (fputc('"', f) == EOF)
        return -1;

    const unsigned char *p = (const unsigned char *)(value ? value : "");
    for (; *p; ++p) {
        const char *escape = NULL;
        switch (*p) {
        case '"':  escape = "\\\""; break;
        case '\\': escape = "\\\\"; break;
        case '\b': escape = "\\b"; break;
        case '\f': escape = "\\f"; break;
        case '\n': escape = "\\n"; break;
        case '\r': escape = "\\r"; break;
        case '\t': escape = "\\t"; break;
        default: break;
        }

        if (escape) {
            if (fputs(escape, f) == EOF)
                return -1;
        } else if (*p < 0x20) {
            if (fprintf(f, "\\u%04x", (unsigned int)*p) < 0)
                return -1;
        } else if (fputc(*p, f) == EOF) {
            return -1;
        }
    }

    return fputc('"', f) == EOF ? -1 : 0;
}

static int write_json_field(FILE *f, const char *key, const char *value)
{
    return fprintf(f, "  \"%s\": ", key) < 0
        || write_json_string(f, value) != 0
        || fputs(",\n", f) == EOF ? -1 : 0;
}

void cfg_defaults(cfg_t *c)
{
    memset(c, 0, sizeof(*c));
    copy_str(c->server, sizeof(c->server), "flight.skeet.top");
    c->port = 6809;
    c->rating = FSD_RATING_DEFAULT;
    copy_str(c->eco, sizeof(c->eco), "private");
    copy_str(c->type, sizeof(c->type), "vatsim");
    copy_str(c->language, sizeof(c->language), "zh-CN");
    copy_str(c->theme, sizeof(c->theme), "dark");
    copy_str(c->jwt_url, sizeof(c->jwt_url), "https://api.skeet.top/api/fsd-jwt");
    copy_str(c->jwt_proxy_bypass, sizeof(c->jwt_proxy_bypass),
             "localhost;127.0.0.1;::1");
    copy_str(c->mock_lat, sizeof(c->mock_lat), "51.4775");
    copy_str(c->mock_lon, sizeof(c->mock_lon), "-0.4614");
    copy_str(c->mock_alt, sizeof(c->mock_alt), "3500");
    copy_str(c->servers[0], JSN_STR_CAP, "flight.skeet.top:6809");
    c->nservers = 1;
    copy_str(c->fp_wake, sizeof(c->fp_wake), "Medium");
    copy_str(c->fp_type, sizeof(c->fp_type), "IFR");
}

/* 安全读字符串键（缺键保留现值） */
static void get_str(const char *doc, const char *key, char *dst, size_t cap)
{
    char tmp[512];
    if (jsn_string(doc, key, tmp, sizeof(tmp)))
        copy_str(dst, cap, tmp);
}

static bool ascii_equal_ci(const char *a, const char *b)
{
    if (!a || !b)
        return false;
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + ('a' - 'A'));
        if (ca != cb)
            return false;
    }
    return *a == '\0' && *b == '\0';
}

static void load_auth_mode(const char *doc, cfg_t *c)
{
    char value[32];
    if (!jsn_string(doc, "auth_mode", value, sizeof(value))) {
        /* Read the old internal key for backward compatibility. */
        if (!jsn_string(doc, "type", value, sizeof(value)))
            return;
        if (ascii_equal_ci(value, "legacy"))
            copy_str(c->type, sizeof(c->type), "legacy");
        else if (ascii_equal_ci(value, "vatsim"))
            copy_str(c->type, sizeof(c->type), "vatsim");
        return;
    }
    if (ascii_equal_ci(value, "legacy"))
        copy_str(c->type, sizeof(c->type), "legacy");
    else if (ascii_equal_ci(value, "fsd-jwt")
             || ascii_equal_ci(value, "vatsim"))
        copy_str(c->type, sizeof(c->type), "vatsim");
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
    load_auth_mode(doc, c);
    get_str(doc, "language", c->language, sizeof(c->language));
    get_str(doc, "theme", c->theme, sizeof(c->theme));
    get_str(doc, "jwt_url", c->jwt_url, sizeof(c->jwt_url));
    get_str(doc, "jwt_proxy", c->jwt_proxy, sizeof(c->jwt_proxy));
    get_str(doc, "jwt_proxy_bypass", c->jwt_proxy_bypass,
            sizeof(c->jwt_proxy_bypass));

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
    get_str(doc, "flight_rules", c->fp_type, sizeof(c->fp_type));
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
    if (!c || !path || !path[0] || c->nservers > CFG_MAX_SERVERS)
        return -1;

    /* 确保目录存在（...\AeroflyLink\） */
    char dir[MAX_PATH];
    copy_str(dir, sizeof(dir), path);
    char *slash = strrchr(dir, '\\');
    if (slash) {
        *slash = '\0';
        CreateDirectoryA(dir, NULL);   /* 已存在时静默失败，无妨 */
    }

    char tmp_path[MAX_PATH];
    int tmp_len = _snprintf(tmp_path, sizeof(tmp_path) - 1,
                            "%s.%lu.tmp", path,
                            (unsigned long)GetCurrentProcessId());
    if (tmp_len < 0 || (size_t)tmp_len >= sizeof(tmp_path) - 1)
        return -1;
    tmp_path[tmp_len] = '\0';

    /* Keep the last good settings file intact if serialization or disk I/O fails. */
    FILE *f = fopen(tmp_path, "wb");
    if (!f)
        return -1;

    int failed = fputs("{\n", f) == EOF;
#define WRITE_FIELD(key, value) \
    do { if (!failed && write_json_field(f, key, value) != 0) failed = 1; } while (0)
    /* ADR 0003: password is intentionally never serialized. */
    WRITE_FIELD("callsign", c->callsign);
    WRITE_FIELD("cid", c->cid);
    WRITE_FIELD("realname", c->realname);
    WRITE_FIELD("server", c->server);
    if (!failed && fprintf(f, "  \"port\": %d,\n  \"rating\": %d,\n",
                           c->port, c->rating) < 0)
        failed = 1;
    WRITE_FIELD("auth_mode", ascii_equal_ci(c->type, "legacy")
                              ? "legacy" : "fsd-jwt");
    WRITE_FIELD("language", c->language);
    WRITE_FIELD("theme", c->theme);
    WRITE_FIELD("jwt_url", c->jwt_url);
    WRITE_FIELD("jwt_proxy", c->jwt_proxy);
    WRITE_FIELD("jwt_proxy_bypass", c->jwt_proxy_bypass);
    WRITE_FIELD("model", c->model);
    WRITE_FIELD("mock_lat", c->mock_lat);
    WRITE_FIELD("mock_lon", c->mock_lon);
    WRITE_FIELD("mock_alt", c->mock_alt);
    WRITE_FIELD("aircraft", c->fp_aircraft);
    WRITE_FIELD("flight_rules", c->fp_type);
    WRITE_FIELD("wake_category", c->fp_wake);
    WRITE_FIELD("tas", c->fp_tas);
    WRITE_FIELD("dep_airport", c->fp_dep);
    WRITE_FIELD("dest_airport", c->fp_dest);
    WRITE_FIELD("alt_airport", c->fp_altn);
    WRITE_FIELD("cruise_alt", c->fp_cruise);
    WRITE_FIELD("route", c->fp_route);
    WRITE_FIELD("remarks", c->fp_remarks);
    WRITE_FIELD("eet", c->fp_eet);
    WRITE_FIELD("endurance", c->fp_endur);
#undef WRITE_FIELD

    if (!failed && fputs("  \"servers\": [", f) == EOF)
        failed = 1;
    for (size_t i = 0; !failed && i < c->nservers; ++i) {
        if (fprintf(f, "%s\n    ", i ? "," : "") < 0
            || write_json_string(f, c->servers[i]) != 0)
            failed = 1;
    }
    if (!failed && fprintf(f, "%s]\n}\n", c->nservers ? "\n  " : "") < 0)
        failed = 1;
    if (ferror(f))
        failed = 1;
    if (fclose(f) != 0)
        failed = 1;

    if (failed || !MoveFileExA(tmp_path, path,
                               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(tmp_path);
        return -1;
    }
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
