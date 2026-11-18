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
};
static UtItem root = UT_ROOT("Embedded Test", kids);
static UtApp app = { "embed test", "Embedded Test", "test-embed.config", "EMB",
                     NULL, "test-embed", NULL, 0 };

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
