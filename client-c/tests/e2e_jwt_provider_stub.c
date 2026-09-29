/*
 * Link-time JWT provider for local FSD E2E only.
 *
 * The test runner obtains a short-lived token from its isolated FSD instance
 * over loopback HTTP, then places it in AEROFLYLINK_E2E_JWT. This replaces only
 * jwt_acquire() at link time so the real C session still sends the VATSIM
 * revision-100 login packet. Production builds never compile this file.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int jwt_acquire(const char *url, const char *proxy, const char *proxy_bypass,
                const char *cid, const char *password,
                char *token, size_t token_cap, char *err, size_t err_cap)
{
    (void)url;
    (void)proxy;
    (void)proxy_bypass;
    (void)cid;
    (void)password;
    const char *fixture = getenv("AEROFLYLINK_E2E_JWT");
    if (!fixture || !fixture[0] || strlen(fixture) >= token_cap) {
        if (err && err_cap)
            snprintf(err, err_cap, "local E2E JWT fixture is missing or too long");
        return -1;
    }
    memcpy(token, fixture, strlen(fixture) + 1);
    fprintf(stderr, "E2E JWT provider injected %zu-byte token\n", strlen(fixture));
    if (err && err_cap)
        err[0] = '\0';
    return 0;
}
