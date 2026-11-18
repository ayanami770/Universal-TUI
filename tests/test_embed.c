/*
 * test_embed.c — ut_run_fd: the engine on a host's pseudo-terminal, with no
 * signal handlers of its own (POSIX).  A thread runs the editor on the pty's
 * slave; the test plays the host on the master: it reads the screen, reports a
 * resize, answers the exit prompt, asks for a quit, and hangs the terminal up.
 *
 *   cc -std=c99 -pthread -I. -o test-embed tests/test_embed.c utui.c && ./test-embed
 */
#define _XOPEN_SOURCE 600
#include "utui.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static int fails = 0;
#define CK(name, cond) do { if (cond) printf("ok   %s\n", name); \
                            else { printf("FAIL %s\n", name); fails++; } } while (0)

static UtItem kids[] = {
    UT_BOOL(2, "Feature one", "ONE", "help one"),
    UT_STR("A string", "NAME", "embedded", NULL),
    UT_INT("A number", "NUM", "7", NULL),
};
static UtItem root = UT_ROOT("Embedded Test", kids);
static UtApp app = { "embed test", "Embedded Test", "test-embed.config", "EMB",
                     NULL, "test-embed", NULL, 0, 0, 0 };

typedef struct { int fd; int result; volatile int done; } Run;

static void *runner(void *arg)
{
    Run *r = arg;
    r->result = ut_run_fd(&root, &app, r->fd, r->fd);
    r->done = 1;
    return NULL;
}

/* Read what the master has for `ms` milliseconds into a growing buffer. */
static size_t slurp(int m, char *buf, size_t cap, size_t len, int ms)
{
    struct timespec t0, t;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        struct pollfd p; long left;
        clock_gettime(CLOCK_MONOTONIC, &t);
        left = ms - ((t.tv_sec - t0.tv_sec) * 1000L + (t.tv_nsec - t0.tv_nsec) / 1000000L);
        if (left <= 0) break;
        p.fd = m; p.events = POLLIN; p.revents = 0;
        if (poll(&p, 1, (int)left) <= 0) continue;
        {
            long n = (long)read(m, buf + len, cap - 1 - len);
            if (n <= 0) break;
            len += (size_t)n;
            if (len >= cap - 1) break;
        }
    }
    buf[len] = 0;
    return len;
}

static int contains(const char *buf, size_t len, const char *s)
{
    size_t n = strlen(s), i;
    for (i = 0; i + n <= len; i++) if (memcmp(buf + i, s, n) == 0) return 1;
    return 0;
}

static int open_pty(int *slave, int cols, int rows)
{
    struct winsize ws;
    int m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0 || grantpt(m) != 0 || unlockpt(m) != 0) return -1;
    *slave = open(ptsname(m), O_RDWR | O_NOCTTY);
    if (*slave < 0) return -1;
    memset(&ws, 0, sizeof ws);
    ws.ws_col = (unsigned short)cols; ws.ws_row = (unsigned short)rows;
    ioctl(m, TIOCSWINSZ, &ws);
    return m;
}

static int wait_done(Run *r, int ms)
{
    int i;
    for (i = 0; i < ms / 10 && !r->done; i++) usleep(10000);
    return r->done;
}

static char buf[1 << 20];

int main(void)
{
    Run r; pthread_t th; int m, s; size_t n;
    struct sigaction old;

    remove("test-embed.config");

    /* --- 1. renders on the pty, installs no SIGWINCH handler, answers a resize, saves --- */
    m = open_pty(&s, 80, 24);
    CK("pty opened", m >= 0);
    r.fd = s; r.result = -9; r.done = 0;
    pthread_create(&th, NULL, runner, &r);
    n = slurp(m, buf, sizeof buf, 0, 600);
    CK("draws the title on the host's terminal", contains(buf, n, "Embedded Test"));
    CK("enters the alternate screen", contains(buf, n, "\x1b[?1049h"));
    sigaction(SIGWINCH, NULL, &old);
    CK("installs no SIGWINCH handler", old.sa_handler == SIG_DFL);
    {
        struct winsize ws; memset(&ws, 0, sizeof ws); ws.ws_col = 100; ws.ws_row = 30;
        ioctl(m, TIOCSWINSZ, &ws);
        ut_notify_resize();
        n = slurp(m, buf, sizeof buf, 0, 600);
        /* the redraw reaches the new last row (30) */
        CK("redraws at the reported size", contains(buf, n, "\x1b[30;"));
    }
    if (write(m, "\x1b", 1) != 1) fails++;
    n = slurp(m, buf, sizeof buf, 0, 500);
    CK("ESC asks to save", contains(buf, n, "save your new configuration"));
    if (write(m, "y", 1) != 1) fails++;
    CK("returns after the exit prompt", wait_done(&r, 3000));
    pthread_join(th, NULL);
    CK("reports a save", r.result == 1);
    {
        FILE *f = fopen("test-embed.config", "r");
        char line[256]; int one = 0, name = 0;
        CK("writes the config file", f != NULL);
        while (f && fgets(line, sizeof line, f)) {
            if (strstr(line, "CONFIG_EMB_ONE=y")) one = 1;
            if (strstr(line, "CONFIG_EMB_NAME=\"embedded\"")) name = 1;
        }
        if (f) fclose(f);
        CK("saves the values", one && name);
    }
    n = slurp(m, buf, sizeof buf, 0, 200);
    close(s); close(m);
    remove("test-embed.config");

    /* --- 2. a host's quit request ends a run without saving --- */
    m = open_pty(&s, 80, 24);
    r.fd = s; r.result = -9; r.done = 0;
    pthread_create(&th, NULL, runner, &r);
    slurp(m, buf, sizeof buf, 0, 400);
    ut_request_quit();
    CK("ut_request_quit ends the run", wait_done(&r, 3000));
    pthread_join(th, NULL);
    CK("a quit saves nothing", r.result == 0);
    close(s); close(m);

    /* --- 3. hanging the terminal up (closing the master) ends a run, no busy loop --- */
    m = open_pty(&s, 80, 24);
    r.fd = s; r.result = -9; r.done = 0;
    pthread_create(&th, NULL, runner, &r);
    slurp(m, buf, sizeof buf, 0, 400);
    close(m);
    CK("a hang-up ends the run", wait_done(&r, 3000));
    pthread_join(th, NULL);
    CK("a hang-up saves nothing", r.result == 0);
    close(s);

    /* --- 4. a run can follow a run (the engine's state is reset) --- */
    m = open_pty(&s, 80, 24);
    r.fd = s; r.result = -9; r.done = 0;
    pthread_create(&th, NULL, runner, &r);
    n = slurp(m, buf, sizeof buf, 0, 600);
    CK("a later run draws again", contains(buf, n, "Embedded Test"));
    ut_request_quit();
    wait_done(&r, 3000);
    pthread_join(th, NULL);
    close(s); close(m);

    /* --- 6. a narrow terminal (UtApp.min_cols) and the blurb (instructions_min_rows) --- */
    {
        static const struct { int mc, imr, cols, rows; const char *name; int fits, instr; const char *need; } T[] = {
            {  0,   0, 60, 30, "the default still needs 80 columns",          0, 0, "by 80 columns" },
            { 52,   0, 52, 30, "min_cols 52 lays the menu out in 52 columns", 1, 1, NULL },
            { 52,   0, 51, 30, "one column narrower is too small",            0, 0, "by 52 columns" },
            { 30,   0, 51, 30, "a min_cols under the floor is the floor",     0, 0, "by 52 columns" },
            { 52,  30, 52, 24, "the blurb is left out below its rows",        1, 0, NULL },
            { 52,  30, 52, 30, "the blurb is drawn from its rows",            1, 1, NULL },
            { 52,  -1, 52, 40, "a negative instructions_min_rows: never",     1, 0, NULL },
        };
        size_t i;
        for (i = 0; i < sizeof T / sizeof T[0]; i++) {
            int ok;
            app.min_cols = T[i].mc; app.instructions_min_rows = T[i].imr;
            m = open_pty(&s, T[i].cols, T[i].rows);
            r.fd = s; r.result = -9; r.done = 0;
            pthread_create(&th, NULL, runner, &r);
            n = slurp(m, buf, sizeof buf, 0, 500);
            if (T[i].fits)
                ok = !contains(buf, n, "too small") && contains(buf, n, "elect>") && contains(buf, n, "oad >") &&
                     contains(buf, n, "eature one") && (contains(buf, n, "Arrow keys") == T[i].instr);
            else
                ok = contains(buf, n, "too small") && contains(buf, n, T[i].need);
            CK(T[i].name, ok);
            ut_request_quit();
            wait_done(&r, 3000);
            pthread_join(th, NULL);
            close(s); close(m);
        }
        /* the number's input box: its prompt (the engine's longest) wraps to 4 lines at 52 columns, and the
         * field goes below them instead of over the last one */
        app.min_cols = 52;
        m = open_pty(&s, 52, 30);
        r.fd = s; r.result = -9; r.done = 0;
        pthread_create(&th, NULL, runner, &r);
        slurp(m, buf, sizeof buf, 0, 400);
        if (write(m, "\x1b[B\x1b[B\r", 7) != 7) fails++;
        n = slurp(m, buf, sizeof buf, 0, 500);
        CK("a narrow input box shows its whole prompt above the field",
           contains(buf, n, "Fractions") && contains(buf, n, "below it."));
        ut_request_quit();
        wait_done(&r, 3000);
        pthread_join(th, NULL);
        close(s); close(m);
        app.min_cols = 0; app.instructions_min_rows = 0;
    }

    /* --- 5. not a terminal: refused --- */
    {
        int p[2];
        if (pipe(p) == 0) {
            CK("a pipe is refused", ut_run_fd(&root, &app, p[0], p[1]) == -1);
            close(p[0]); close(p[1]);
        }
    }

    printf("%s (%d failed)\n", fails ? "FAILED" : "PASSED", fails);
    remove("test-embed.config");
    return fails ? 1 : 0;
}
