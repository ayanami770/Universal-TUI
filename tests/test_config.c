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
static UtApp app = { "Test", "Test", "test.config", "TEST", NULL, NULL, NULL, 1 };

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

    items[0].sym = "BAD\nINJECT";
    assert(ut_save(&root, &app, "test.config") == -1);
    remove("test.config");
    return 0;
}
