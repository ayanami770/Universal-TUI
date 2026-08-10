/* UTF-8 対応 (v1.1) の単体テスト: エンジンの static 関数を直接検証する。
 *   cc -std=c99 -Wall -Wextra -pedantic -I. -o test-utf8 tests/test_utf8.c
 * (utui.c を #include するので、リンクに utui.o は不要) */
#include "../utui.c"

#include <assert.h>

static void test_decode_encode(void)
{
    unsigned cp;
    char enc[4];

    assert(u8_decode("A", &cp) == 1 && cp == 0x41 && cp_width(cp) == 1);
    assert(u8_decode("\xC3\xA9", &cp) == 2 && cp == 0xE9 && cp_width(cp) == 1);   /* é */
    assert(u8_decode("\xE3\x81\x82", &cp) == 3 && cp == 0x3042 && cp_width(cp) == 2); /* あ */
    assert(u8_decode("\xF0\xA0\x80\x8B", &cp) == 4 && cp == 0x2000B && cp_width(cp) == 2); /* 𠀋 */

    assert(u8_encode(0x41, enc) == 1 && enc[0] == 'A');
    assert(u8_encode(0xE9, enc) == 2 && memcmp(enc, "\xC3\xA9", 2) == 0);
    assert(u8_encode(0x3042, enc) == 3 && memcmp(enc, "\xE3\x81\x82", 3) == 0);
    assert(u8_encode(0x2000B, enc) == 4 && memcmp(enc, "\xF0\xA0\x80\x8B", 4) == 0);

    /* 不正列は 1 バイト消費で U+FFFD */
    assert(u8_decode("\xC3", &cp) == 1 && cp == 0xFFFD);       /* 途切れ */
    assert(u8_decode("\x80x", &cp) == 1 && cp == 0xFFFD);      /* 迷子継続 */
    assert(u8_decode("\xC0\xAF", &cp) == 1 && cp == 0xFFFD);   /* 過長表現 */
    assert(u8_decode("\xED\xA0\x80", &cp) == 1 && cp == 0xFFFD); /* サロゲート */
}

static void test_widths(void)
{
    assert(u8_swidth("abc") == 3);
    assert(u8_swidth("abc\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E") == 9); /* abc日本語 */
    assert(u8_swidth("\xEF\xBC\xA1") == 2);                    /* 全角Ａ */
    assert(u8_swidth("") == 0);
}

static void test_boundaries(void)
{
    const char *s = "a\xE3\x81\x82\xF0\xA0\x80\x8B";           /* a あ 𠀋 */
    assert(u8_next(s, 0) == 1);
    assert(u8_next(s, 1) == 4);
    assert(u8_next(s, 4) == 8);
    assert(u8_next(s, 8) == 8);                                 /* 終端で停止 */
    assert(u8_prev(s, 8) == 4);
    assert(u8_prev(s, 4) == 1);
    assert(u8_prev(s, 1) == 0);
    assert(u8_prev(s, 0) == 0);
}

static void test_prefix(void)
{
    char out[64];
    /* 日本語です: 語頭 2 文字 (幅 4) までしか幅 5 に入らない */
    u8_prefix(out, sizeof out, "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", 5);
    assert(strcmp(out, "\xE6\x97\xA5\xE6\x9C\xAC") == 0);
    u8_prefix(out, sizeof out, "abcdef", 3);
    assert(strcmp(out, "abc") == 0);
    u8_prefix(out, 4, "abcdef", 10);                            /* cap 優先 */
    assert(strcmp(out, "abc") == 0);
}

static void test_wrap_lines(void)
{
    size_t st[16], ln[16];
    int n, i;
    /* 幅 8: "aaaa bbbb"(9桁) も "日本語 cc"(9桁) も折れて 4 行になる */
    const char *t = "aaaa bbbb \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E cc";
    n = wrap_lines(t, 8, st, ln, 16);
    assert(n == 4);
    assert(ln[0] == 5 && memcmp(t + st[0], "aaaa ", ln[0]) == 0);
    assert(ln[1] == 5 && memcmp(t + st[1], "bbbb ", ln[1]) == 0);
    assert(ln[2] == 10 && memcmp(t + st[2], "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E ", 10) == 0);
    assert(ln[3] == 2 && memcmp(t + st[3], "cc", 2) == 0);
    for (i = 0; i < n; i++)
        assert(u8_swidth_n(t + st[i], ln[i]) <= 8);

    n = wrap_lines("x\n\ny", 8, st, ln, 16);
    assert(n == 3 && ln[0] == 1 && ln[1] == 0 && ln[2] == 1);

    assert(wrap_lines("", 8, NULL, NULL, 0) == 1);
    assert(wrap_lines("abc", 0, NULL, NULL, 0) == 1);           /* 防御 */
}

static void test_ib_insert(void)
{
    char buf[8] = "abc";
    size_t cur = 3;
    ib_insert(buf, sizeof buf, &cur, 0x3042, 0);                /* あ (3B) */
    assert(strcmp(buf, "abc\xE3\x81\x82") == 0 && cur == 6);
    ib_insert(buf, sizeof buf, &cur, 'x', 0);
    assert(strcmp(buf, "abc\xE3\x81\x82x") == 0 && cur == 7);
    ib_insert(buf, sizeof buf, &cur, 'y', 0);                   /* 満杯: 拒否 */
    assert(strcmp(buf, "abc\xE3\x81\x82x") == 0);
    cur = 0;
    ib_insert(buf, sizeof buf, &cur, 0x3042, 1);                /* digits_only */
    assert(buf[0] == 'a' && cur == 0);
}

static void test_wide_cells(void)
{
    Cell *c;
    g_cols = 20; g_rows = 3;
    buf_resize();

    put(0, 0, "\xE3\x81\x82", C_WHITE, C_BLUE, 0);              /* あ */
    c = &g_buf[0];
    assert(c->wide == 1 && strcmp(c->g, "\xE3\x81\x82") == 0);
    assert(g_buf[1].g[0] == 1);                                 /* 継続セル */

    put(1, 0, "X", C_WHITE, C_BLUE, 0);                         /* 継続を上書き */
    assert(g_buf[0].wide == 0 && strcmp(g_buf[0].g, " ") == 0); /* リード消滅 */
    assert(strcmp(g_buf[1].g, "X") == 0);

    put(5, 0, "\xE3\x81\x82", C_WHITE, C_BLUE, 0);
    put(5, 0, "Z", C_WHITE, C_BLUE, 0);                         /* リードを上書き */
    assert(strcmp(g_buf[5].g, "Z") == 0 && g_buf[5].wide == 0);
    assert(strcmp(g_buf[6].g, " ") == 0);                       /* 継続消滅 */

    put(19, 0, "\xE3\x81\x82", C_WHITE, C_BLUE, 0);             /* 右端 */
    assert(strcmp(g_buf[19].g, "?") == 0 && g_buf[19].wide == 0);
}

int main(void)
{
    test_decode_encode();
    test_widths();
    test_boundaries();
    test_prefix();
    test_wrap_lines();
    test_ib_insert();
    test_wide_cells();
    printf("test_utf8: all ok\n");
    return 0;
}
