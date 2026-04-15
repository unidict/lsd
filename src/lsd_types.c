//
//  lsd_types.c
//  libud
//
//  Created by kejinlu on 2026/04/11.
//

#include "lsd_types.h"
#include <stdlib.h>
#include <string.h>

void lsd_heading_destroy(lsd_heading *heading) {
    if (!heading) return;

    if (heading->text) free(heading->text);
    if (heading->ext_data) free(heading->ext_data);

    memset(heading, 0, sizeof(lsd_heading));
}

const uint16_t *lsd_heading_get_text(const lsd_heading *heading) {
    return heading ? heading->text : NULL;
}

uint32_t lsd_heading_get_reference(const lsd_heading *heading) {
    return heading ? heading->reference : 0;
}
