#include "utui.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static UtItem items[] = {
    UT_STR("Text", "TEXT", "initial", NULL),
    UT_INT("Number", "NUMBER", "7", NULL),
    UT_BOOL(2, "Flag", "FLAG", NULL),
};
static UtItem root = UT_ROOT("Test", items);
static UtApp app = { "Test", "Test", "test.config", "TEST", NULL, NULL, NULL, 1, 0, 0 };

int main(void)
{
    FILE *f;
    char disk[1024];
    size_t nread;

    strcpy(items[0].strval, "quote\" slash\\ line\n");
    assert(ut_save(&root, &app, "test.config") == 0);
    f = fopen("test.config", "r");
    assert(f != NULL);
    nread = fread(disk, 1, sizeof disk - 1, f);
    assert(nread > 0);
    fclose(f);
    disk[nread] = 0;
    assert(strstr(disk, "CONFIG_TEST_TEXT=\"quote\\\" slash\\\\ line\\n\"") != NULL);

    strcpy(items[0].strval, "changed");
    strcpy(items[1].strval, "8");
    assert(ut_load(&root, &app, "test.config") == 0);
    assert(strcmp(items[0].strval, "quote\" slash\\ line\n") == 0);

    f = fopen("test.config", "w");
    assert(f != NULL);
    fputs("CONFIG_TEST_NUMBER=12oops\nCONFIG_TEST_FLAG=yes\n", f);
    fclose(f);
    strcpy(items[1].strval, "8");
    assert(ut_load(&root, &app, "test.config") == 0);
    assert(strcmp(items[1].strval, "8") == 0);
    assert(items[2].value == 0); /* reset, but malformed 'yes' is not accepted */

    /* 負の整数値を受理する (decimal_value が先頭の符号を許可) */
    f = fopen("test.config", "w");
    assert(f != NULL);
    fputs("CONFIG_TEST_NUMBER=-5\n", f);
    fclose(f);
    strcpy(items[1].strval, "8");
    assert(ut_load(&root, &app, "test.config") == 0);
    assert(strcmp(items[1].strval, "-5") == 0);

    /* 未知エスケープ (\b) は素の '\' として保持する = 旧データの後方互換 */
    f = fopen("test.config", "w");
    assert(f != NULL);
    fputs("CONFIG_TEST_TEXT=\"a\\bc\"\n", f);
    fclose(f);
    strcpy(items[0].strval, "x");
    assert(ut_load(&root, &app, "test.config") == 0);
    assert(strcmp(items[0].strval, "a\\bc") == 0);

    /* UTF-8 の長い値 (v1.1: strval は 512 バイト) の保存/読込ラウンドトリップ */
    {
        /* Windows パス風の日本語文字列を ~240 バイトに伸ばす */
        char jp[512] = "";
        char big[512];
        int rep;
        for (rep = 0; rep < 6; rep++)
            strcat(jp, "C:\\\xE3\x82\xB5\xE3\x82\xA6\xE3\x83\xB3\xE3\x83\x89"
                       "\\\xE6\xB1\xBA\xE5\xAE\x9A\xE3\x83\x9C\xE3\x82\xBF\xE3\x83\xB3.wav;");
        assert(strlen(jp) > 64);                  /* 旧上限を超えていること */
        strcpy(items[0].strval, jp);
        assert(ut_save(&root, &app, "test.config") == 0);
        strcpy(items[0].strval, "wiped");
        assert(ut_load(&root, &app, "test.config") == 0);
        assert(strcmp(items[0].strval, jp) == 0);

        /* 500 バイトの ASCII 値 (上限近く) */
        memset(big, 'a', 500); big[500] = 0;
        strcpy(items[0].strval, big);
        assert(ut_save(&root, &app, "test.config") == 0);
        strcpy(items[0].strval, "wiped");
        assert(ut_load(&root, &app, "test.config") == 0);
        assert(strcmp(items[0].strval, big) == 0);
    }

    items[0].sym = "BAD\nINJECT";
    assert(ut_save(&root, &app, "test.config") == -1);
    remove("test.config");
    return 0;
}
