//
//  lsd_page_store.h
//  libud
//
//  Created by kejinlu on 2026/04/11.
//

#ifndef lsd_page_store_h
#define lsd_page_store_h

#include "lsd_types.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct lsd_bitstream lsd_bitstream;
typedef struct lsd_decoder lsd_decoder;

// ============================================================
// Page header
// ============================================================
typedef struct lsd_page_header {
    bool is_leaf;            // Whether this is a leaf page
    uint16_t number;         // Page number
    uint16_t prev;           // Previous page number
    uint16_t parent;         // Parent page number
    uint16_t next;           // Next page number
    uint16_t headings_count; // Number of entries (leaf) or keys (internal node)
} lsd_page_header;

// ============================================================
// Internal node page (B+ tree non-leaf node)
// ============================================================
typedef struct lsd_node_page {
    lsd_page_header header;  // Page header

    uint32_t *children;      // Child page number array (key_count + 1 entries)
    size_t child_count;      // Number of children
    uint16_t **keys;         // Separator key array (entry headings, defining subtree ranges)
    size_t *key_lengths;     // Separator key length array
    size_t key_count;        // Number of separator keys
} lsd_node_page;

// ============================================================
// Leaf page
// ============================================================
typedef struct lsd_leaf_page {
    lsd_page_header header;  // Page header

    lsd_heading *headings;   // Entry array
    size_t heading_count;       // Number of entries
} lsd_leaf_page;

// ============================================================
// Page store (opaque type)
// ============================================================
typedef struct lsd_page_store lsd_page_store;

// ============================================================
// Page store API
// ============================================================

/**
 * Create a page store
 * @param bstr Bitstream reader (reference, not owned)
 * @param decoder Decoder (reference, not owned, can be lazy-loaded)
 * @param pages_offset Offset of the page area in the file
 * @param total_pages Total number of pages
 * @return Page store, or NULL on failure
 */
lsd_page_store *lsd_page_store_create(lsd_bitstream *bstr,
                                      lsd_decoder *decoder,
                                      uint32_t pages_offset,
                                      size_t total_pages);

/**
 * Destroy the page store
 */
void lsd_page_store_destroy(lsd_page_store *store);

/**
 * Read page header information
 * @param store Page store
 * @param page_number Page number
 * @param header Output page header structure
 * @return true on success
 */
bool lsd_page_store_read_header(lsd_page_store *store,
                                uint32_t page_number,
                                lsd_page_header *header);

/**
 * Get a leaf page (automatically managed)
 * The returned page is managed by the store; callers should not free it.
 * @param store Page store
 * @param page_number Page number
 * @param prefix Known prefix (for incremental decoding, can be NULL)
 * @param prefix_len Prefix length
 * @return Leaf page, or NULL on failure
 */
lsd_leaf_page *lsd_page_store_get_leaf(lsd_page_store *store,
                                       uint16_t page_number,
                                       const uint16_t *prefix,
                                       size_t prefix_len);

/**
 * Get a non-leaf node page (automatically managed)
 * The returned page is managed by the store; callers should not free it.
 * @param store Page store
 * @param page_number Page number
 * @return Node page, or NULL on failure
 */
lsd_node_page *lsd_page_store_get_node(lsd_page_store *store,
                                       uint16_t page_number);



#ifdef __cplusplus
}
#endif

#endif /* lsd_page_store_h */
