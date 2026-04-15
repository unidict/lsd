//
//  probe_prefix.c
//  libud
//
//  Created by kejinlu on 2026/04/14.
//

#include "lsd_reader.h"
#include "lsd_decoder.h"
#include "lsd_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void probe_prefix(const char *path, const char *prefix) {
    lsd_reader *r = lsd_reader_open(path);
    if (!r) { printf("  FAILED TO OPEN\n"); return; }

    const lsd_header *h = lsd_reader_get_header(r);
    if (!h || !lsd_is_version_supported(h->version)) {
        printf("  Unsupported\n");
        lsd_reader_close(r);
        return;
    }

    // 用 max=0（不限制）获取全部匹配
    lsd_heading *results = NULL;
    size_t count = 0;
    bool ok = lsd_reader_prefix(r, prefix, 0, &results, &count);
    printf("  prefix(\"%s\", max=0) = %s, count=%zu\n",
           prefix, ok ? "true" : "false", count);
    for (size_t i = 0; i < count && i < 3; i++) {
        char *t = NULL;
        lsd_utf16_to_utf8(results[i].text, results[i].text_length, &t);
        printf("    [%zu] \"%s\" ref=%u\n", i, t ? t : "(null)", results[i].reference);
        free(t);
        lsd_heading_destroy(&results[i]);
    }
    // destroy remaining
    for (size_t i = 3; i < count; i++) {
        lsd_heading_destroy(&results[i]);
    }
    free(results);
    lsd_reader_close(r);
}

int main(void) {
    const char *dir = "/Users/kejinlu/Documents/lsd/tests/data/";
    char path[512];

    printf("=== system_14 ===\n");
    snprintf(path, sizeof(path), "%s%s", dir, "system_14_activederu.lsd");
    probe_prefix(path, "Ab");

    printf("\n=== system_15 ===\n");
    snprintf(path, sizeof(path), "%s%s", dir, "system_15_activederu.lsd");
    probe_prefix(path, "Ab");

    printf("\n=== user_11 ===\n");
    snprintf(path, sizeof(path), "%s%s", dir, "user_11_international_lighting_vocabulary_cie_publ_no_17.lsd");
    probe_prefix(path, "Ab");

    printf("\n=== user_12 ===\n");
    snprintf(path, sizeof(path), "%s%s", dir, "user_12_accountingenru.lsd");
    probe_prefix(path, "acc");

    printf("\n=== user_13 ===\n");
    snprintf(path, sizeof(path), "%s%s", dir, "user_13_ru_be_false_friends_yzb_1_0_x3.lsd");
    probe_prefix(path, "баб");

    printf("\n=== user_14 ===\n");
    snprintf(path, sizeof(path), "%s%s", dir, "user_14_eng_rus_greatbritain_x5.lsd");
    probe_prefix(path, "ab");

    printf("\n=== user_legacy ===\n");
    snprintf(path, sizeof(path), "%s%s", dir, "user_legacy_accountingenru.lsd");
    probe_prefix(path, "acc");

    printf("\n=== abbr_14 ===\n");
    snprintf(path, sizeof(path), "%s%s", dir, "abbr_14_eng_rus_greatbritain_x5_abrv.lsd");
    probe_prefix(path, "ам");

    printf("\n=== abbr_15 ===\n");
    snprintf(path, sizeof(path), "%s%s", dir, "abbr_15_abbrev.lsd");
    probe_prefix(path, "ад");

    return 0;
}
