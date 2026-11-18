/*
 * utui.h — Universal TUI: a dependency-free, single-engine "menuconfig"-style
 *          terminal UI toolkit in C99.  Portable across POSIX (Linux/macOS/BSD,
 *          termios+poll) and pure Windows (Win32 Console API).  No ncurses.
 *
 * You describe a tree of typed configuration items (bool / tristate / choice /
 * string / int / submenus / comments) as static data, hand it to ut_run(), and
 * get the full lxdialog experience: 3D dialogs, hotkeys, y/n/m, help, search
 * with jump, .config save/load, mouse, resize, ASCII fallback.
 *
 *   Engine  : utui.c  (compile once, reuse everywhere)
 *   Consumer: define a UtItem tree + a UtApp, call ut_main() from main().
 *
 * License: Apache-2.0.
 */
#ifndef UTUI_H
#define UTUI_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UTUI_VERSION "1.2"

/* Item kinds (stored in UtItem.type). */
enum {
    UT_TYPE_MENU,     /* submenu container (children[])                        */
    UT_TYPE_BOOL,     /* [ ]/[*]  (value 0/2); may also carry children (menu)  */
    UT_TYPE_TRI,      /* < >/<M>/<*> tristate (value 0/1/2)                    */
    UT_TYPE_CHOICE,   /* "name (current)" with radio children; value = index   */
    UT_TYPE_RADIO,    /* a choice option (selected when parent->value == idx)  */
    UT_TYPE_STR,      /* (text) string value in strval                         */
    UT_TYPE_INT,      /* (123)  decimal value in strval                        */
    UT_TYPE_COMMENT   /* "*** label ***" non-selectable divider                */
};

typedef struct UtItem {
    int   type;                 /* one of UT_TYPE_*                            */
    int   value;                /* bool/tri: 0/1/2; choice: selected index     */
    int   forced;               /* 1 => shown as -*- and not user-changeable   */
    const char *name;           /* prompt text (UTF-8)                         */
    const char *sym;            /* CONFIG symbol (without prefix); NULL=unsaved */
    const char *help;           /* long help text (NULL => generic message)    */
    char  strval[512];          /* value for UT_TYPE_STR / UT_TYPE_INT (UTF-8)  */
    struct UtItem *children;    /* submenu / choice options                    */
    int   nchild;
    struct UtItem *parent;      /* filled in by the engine                     */
    int   idx;                  /* index within parent->children (engine)      */
} UtItem;

#define UT_NITEMS(a) ((int)(sizeof(a)/sizeof((a)[0])))

/* ---- item constructors (positional initializers => strict-C99 clean) ---- */
#define UT_BOOL(v,nm,sm,hl)        { UT_TYPE_BOOL,(v),0,(nm),(sm),(hl),{0},0,0,0,0 }
#define UT_TRI(v,nm,sm,hl)         { UT_TYPE_TRI, (v),0,(nm),(sm),(hl),{0},0,0,0,0 }
#define UT_FORCED(nm,sm,hl)        { UT_TYPE_BOOL,2,1,(nm),(sm),(hl),{0},0,0,0,0 }
#define UT_MENU(nm,kids)           { UT_TYPE_MENU,0,0,(nm),0,0,{0},(kids),UT_NITEMS(kids),0,0 }
#define UT_SUBMENU(v,nm,sm,hl,kids) { UT_TYPE_BOOL,(v),0,(nm),(sm),(hl),{0},(kids),UT_NITEMS(kids),0,0 }
#define UT_CHOICE(ix,nm,kids)      { UT_TYPE_CHOICE,(ix),0,(nm),0,0,{0},(kids),UT_NITEMS(kids),0,0 }
#define UT_CHOICE_H(ix,nm,hl,kids) { UT_TYPE_CHOICE,(ix),0,(nm),0,(hl),{0},(kids),UT_NITEMS(kids),0,0 }
#define UT_RADIO(nm,sm)            { UT_TYPE_RADIO,0,0,(nm),(sm),0,{0},0,0,0,0 }
#define UT_STR(nm,sm,v,hl)         { UT_TYPE_STR,0,0,(nm),(sm),(hl),v,0,0,0,0 }
#define UT_INT(nm,sm,v,hl)         { UT_TYPE_INT,0,0,(nm),(sm),(hl),v,0,0,0,0 }
#define UT_COMMENT(nm)             { UT_TYPE_COMMENT,0,0,(nm),0,0,{0},0,0,0,0 }
#define UT_ROOT(title,kids)        { UT_TYPE_MENU,0,0,(title),0,0,{0},(kids),UT_NITEMS(kids),0,0 }

/* Application description passed to the engine. */
typedef struct {
    const char *backtitle;      /* top status-bar text                         */
    const char *title;          /* root dialog title                           */
    const char *config_file;    /* default save/load path                      */
    const char *sym_prefix;     /* CONFIG_<prefix>_<SYM>; "" => CONFIG_<SYM>    */
    const char *instructions;   /* header blurb (NULL => built-in menuconfig)   */
    const char *app_name;       /* shown by --version (NULL => "utui")          */
    const char *saved_epilogue; /* printed after a saved exit (NULL => none)    */
    int   ascii;                /* 1 force ASCII, 0 force Unicode, <0 auto      */
} UtApp;

/*
 * Run the interactive editor over `root` (a UT_TYPE_MENU item).  The engine
 * fills in parent/idx, auto-loads app->config_file if present, then drives the
 * UI until the user exits.  Returns 1 if the user saved, 0 if not, -1 if
 * stdin/stdout is not a terminal.
 */
int ut_run(UtItem *root, const UtApp *app);

/*
 * Run the editor on a terminal the host provides instead of stdin/stdout: `in_fd`
 * and `out_fd` (may be the same fd, e.g. a pseudo-terminal's slave). For a host with
 * no terminal of its own, such as an app that draws a terminal view over a pty.
 * In this mode the engine installs no signal handlers and no atexit hook: the host
 * reports a size change with ut_notify_resize() (after setting the pty's size), and
 * ends a run with ut_request_quit() or by hanging the terminal up (closing the pty's
 * master), which the engine reads as a quit without saving.  One run at a time: the
 * engine's state is global.  Returns as ut_run(); -1 on Windows (not available).
 */
int ut_run_fd(UtItem *root, const UtApp *app, int in_fd, int out_fd);

/* For a running editor, from any thread or a signal handler. */
void ut_notify_resize(void);   /* the terminal changed size: re-read it and redraw  */
void ut_request_quit(void);    /* quit as Ctrl-C does (nothing is saved)           */

/* Load / save a .config-style file without entering the UI. 0 on success. */
int ut_load(UtItem *root, const UtApp *app, const char *path);
int ut_save(UtItem *root, const UtApp *app, const char *path);

/*
 * Convenience main(): parses --ascii/--unicode/--config FILE/--version/--help,
 * checks the terminal, runs the UI, prints the epilogue.  `app` may be modified
 * (ascii/config_file) from the command line.  Returns a process exit code.
 */
int ut_main(UtItem *root, UtApp *app, int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* UTUI_H */
