/*
 * demo.c — a tiny standalone example of the Universal TUI.
 *
 * It configures a fictional "acme-server" and shows every item kind the engine
 * supports.  Build:  cc -std=c99 -O2 -I.. -o demo demo.c ../utui.c
 * Run:  ./demo   (writes/reads ./acme-server.config)
 */
#include "utui.h"

/* ---- help texts ---- */
static const char H_WORKERS[] =
"Number of worker processes to spawn.  A good starting point is one per CPU "
"core.  0 means autodetect.";
static const char H_TLS[] =
"Enable Transport Layer Security.  When on, the server will require a "
"certificate and key and refuse plaintext connections.";
static const char H_LOGLEVEL[] =
"How chatty the server log should be.  'debug' is very verbose and should not "
"be used in production.";

/* ---- leaf menus ---- */
static UtItem tls_ciphers[] = {
    UT_TRI(2, "AES-128-GCM", "CIPHER_AES128", NULL),
    UT_TRI(2, "AES-256-GCM", "CIPHER_AES256", NULL),
    UT_TRI(1, "ChaCha20-Poly1305", "CIPHER_CHACHA", NULL),
};
static UtItem tls_items[] = {
    UT_STR("Certificate file", "TLS_CERT", "/etc/acme/server.pem", NULL),
    UT_STR("Private key file", "TLS_KEY", "/etc/acme/server.key", NULL),
    UT_SUBMENU(2, "Enabled cipher suites", "TLS_CIPHERS", NULL, tls_ciphers),
};
static UtItem loglevel_choice[] = {
    UT_RADIO("error", "LOG_ERROR"),
    UT_RADIO("warning", "LOG_WARNING"),
    UT_RADIO("info", "LOG_INFO"),
    UT_RADIO("debug", "LOG_DEBUG"),
};
static UtItem net_items[] = {
    UT_STR("Listen address", "LISTEN_ADDR", "0.0.0.0", NULL),
    UT_INT("Listen port", "LISTEN_PORT", "8080", NULL),
    UT_INT("Worker processes (0=auto)", "WORKERS", "0", H_WORKERS),
    UT_BOOL(2, "Enable HTTP/2", "HTTP2", NULL),
    UT_BOOL(0, "Enable IPv6", "IPV6", NULL),
    UT_SUBMENU(2, "TLS / HTTPS", "TLS", H_TLS, tls_items),
    /* v1.1: UTF-8 の値と全角ラベルの動作確認用 (日本語で編集できる) */
    UT_STR("Banner (UTF-8 / 日本語も可)", "BANNER", "ようこそ acme-server へ",
           "Any UTF-8 text works here, including double-width CJK characters. "
           "String values can hold up to 511 bytes."),
};
static UtItem storage_choice[] = {
    UT_RADIO("SQLite (embedded)", "DB_SQLITE"),
    UT_RADIO("PostgreSQL", "DB_POSTGRES"),
    UT_RADIO("MySQL / MariaDB", "DB_MYSQL"),
};
static UtItem storage_items[] = {
    UT_CHOICE(0, "Database backend", storage_choice),
    UT_STR("Data directory", "DATA_DIR", "/var/lib/acme", NULL),
    UT_INT("Cache size (MB)", "CACHE_MB", "256", NULL),
    UT_BOOL(2, "Compress on-disk data", "COMPRESS", NULL),
};
static UtItem log_items[] = {
    UT_CHOICE_H(2, "Log verbosity", H_LOGLEVEL, loglevel_choice),
    UT_STR("Log file", "LOG_FILE", "/var/log/acme.log", NULL),
    UT_BOOL(0, "Log to syslog", "LOG_SYSLOG", NULL),
    UT_COMMENT("Rotation"),
    UT_BOOL(2, "Rotate logs daily", "LOG_ROTATE", NULL),
    UT_INT("Keep N rotated files", "LOG_KEEP", "7", NULL),
};

static UtItem main_items[] = {
    UT_MENU("Networking", net_items),
    UT_MENU("Storage", storage_items),
    UT_MENU("Logging", log_items),
    UT_FORCED("Core scheduler", "CORE", "The core event loop; always built in."),
    UT_BOOL(0, "Run as a daemon", "DAEMON", NULL),
    UT_STR("Run as user", "RUN_USER", "acme", NULL),
    UT_TRI(1, "Prometheus metrics exporter", "METRICS", NULL),
};

static UtItem root = UT_ROOT("acme-server Configuration", main_items);

int main(int argc, char **argv)
{
    UtApp app = {
        ".config - acme-server Configuration",  /* backtitle           */
        "acme-server Configuration",             /* title               */
        "acme-server.config",                    /* config file         */
        "ACME",                                  /* CONFIG_ACME_<SYM>   */
        NULL,                                    /* default instructions*/
        "acme-server demo",                      /* --version name      */
        "\n*** Configuration saved. ***\n\n",    /* saved epilogue      */
        -1,                                      /* ascii: auto         */
        0,                                       /* min_cols: 80        */
        0                                        /* instructions: always*/
    };
    return ut_main(&root, &app, argc, argv);
}
