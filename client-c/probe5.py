# 一次性：sess_on_readable / app_poll FD 命中探针
data = open("src/gui.c", encoding="utf-8").read()

old = """    if (a->sess.sock != INVALID_SOCKET) {
        if (FD_ISSET(a->sess.sock, &w))
            sess_on_writable(&a->sess);
        if (FD_ISSET(a->sess.sock, &r))
            sess_on_readable(&a->sess);
    }"""
new = """    if (a->sess.sock != INVALID_SOCKET) {
        if (FD_ISSET(a->sess.sock, &w))
            sess_on_writable(&a->sess);
        if (FD_ISSET(a->sess.sock, &r))
            sess_on_readable(&a->sess);
        {
            static int n = 0;
            if (++n % 40 == 0) {
                FILE *f = fopen(
                    "C:/Users/wlh77/AppData/Local/Temp/sess_probe.log", "a");
                if (f) {
                    fprintf(f, "sess: state=%d flags=%u rdset=%d\\n",
                            (int)a->sess.state,
                            (unsigned)a->sess.diag_flags,
                            (int)FD_ISSET(a->sess.sock, &r));
                    fclose(f);
                }
            }
        }
    }"""
assert old in data, "app_poll block not found"
data = data.replace(old, new)

old2 = """void sess_on_readable(sess_t *s)
{
    if (s->sock == INVALID_SOCKET)
        return;

    char chunk[8192];
    int got_lines = 0;"""
new2 = """void sess_on_readable(sess_t *s)
{
    if (s->sock == INVALID_SOCKET)
        return;

    char chunk[8192];
    int got_lines = 0;
    {
        FILE *f = fopen("C:/Users/wlh77/AppData/Local/Temp/sess_probe.log", "a");
        if (f) {
            fprintf(f, "on_readable ENTER\\n");
            fclose(f);
        }
    }"""
assert old2 in data, "on_readable anchor not found"
data = data.replace(old2, new2)

open("src/gui.c", "w", encoding="utf-8", newline="\n").write(data)
print("sess probes installed")
