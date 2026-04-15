//
//  lsd_page_store.c
//  libud
//
//  Created by kejinlu on 2026/04/11.
//

#include "lsd_page_store.h"
#include "lsd_bitstream.h"
#include "lsd_decoder.h"
#include <stdlib.h>
#include <string.h>
#include "lsd_utils.h"

#define LSD_PAGE_SIZE 512
#define LSD_LEAF_CACHE_CAPACITY 8

// ============================================================
// Internal type definitions
// ============================================================

// LRU cache node (for leaf page storage)
typedef struct lsd_leaf_cache_node {
    uint16_t page_number;
    lsd_leaf_page *page;
    struct lsd_leaf_cache_node *prev;
    struct lsd_leaf_cache_node *next;
} lsd_leaf_cache_node;

// Page store full definition
struct lsd_page_store {
    // Loading context (reference, not owned)
    lsd_bitstream *bstr;
    lsd_decoder *decoder;
    uint32_t pages_offset;

    // Non-leaf node cache (fully cached, indexed by page number)
    lsd_node_page **node_pages;
    size_t node_capacity;
    size_t node_count;

    // Leaf node LRU cache
    lsd_leaf_cache_node *leaf_head;
    lsd_leaf_cache_node *leaf_tail;
    size_t leaf_capacity;
    size_t leaf_count;
};

// Internal function forward declarations
static void lsd_node_page_destroy(lsd_node_page *page);
static void lsd_leaf_page_destroy(lsd_leaf_page *page);

// ============================================================
// Page header operations
// ============================================================

static bool lsd_page_header_load(lsd_page_header *header, lsd_bitstream *bstr) {
    if (!header || !bstr) return false;
    
    header->is_leaf = (bool)lsd_bitstream_read(bstr, 1);
    header->number = (uint16_t)lsd_bitstream_read(bstr, 16);
    header->prev = (uint16_t)lsd_bitstream_read(bstr, 16);
    header->parent = (uint16_t)lsd_bitstream_read(bstr, 16);
    header->next = (uint16_t)lsd_bitstream_read(bstr, 16);
    header->headings_count = (uint16_t)lsd_bitstream_read(bstr, 16);
    
    lsd_bitstream_align_to_byte(bstr);
    
    return true;
}

// ============================================================
// Entry operations
// ============================================================

static bool lsd_heading_load(lsd_heading *heading,
                         lsd_decoder *decoder,
                         lsd_bitstream *bstr,
                         const uint16_t *known_prefix,
                         size_t known_prefix_len,
                         uint16_t **new_prefix,
                         size_t *new_prefix_len) {
    if (!heading || !decoder || !bstr) return false;
    
    memset(heading, 0, sizeof(lsd_heading));
    
    // Decode prefix length
    uint32_t prefix_len;
    if (!lsd_decoder_decode_prefix_len(decoder, bstr, &prefix_len)) {
        return false;
    }
    
    // Decode postfix length
    uint32_t postfix_len;
    if (!lsd_decoder_decode_postfix_len(decoder, bstr, &postfix_len)) {
        return false;
    }
    
    // Decode postfix text
    uint16_t *postfix = NULL;
    size_t postfix_length = 0;
    if (!lsd_decoder_decode_heading(decoder, bstr, postfix_len, &postfix, &postfix_length)) {
        return false;
    }
    
    // Read article reference
    if (!lsd_decoder_read_reference2(decoder, bstr, &heading->reference)) {
        free(postfix);
        return false;
    }
    
    // Build complete entry text: prefix[0:prefix_len] + postfix
    size_t actual_prefix_len = prefix_len;
    if (actual_prefix_len > known_prefix_len) {
        actual_prefix_len = known_prefix_len;
    }
    
    heading->text = lsd_utf16_concat(known_prefix, actual_prefix_len, 
                                         postfix, postfix_length, 
                                         &heading->text_length);
    
    // Read extended data (if any)
    if (lsd_bitstream_read(bstr, 1)) {
        uint32_t ext_len = lsd_bitstream_read(bstr, 8);
        if (ext_len > 0) {
            heading->ext_data_length = ext_len * 3;  // 3 bytes per extension item
            heading->ext_data = malloc(heading->ext_data_length);
            if (heading->ext_data) {
                for (uint32_t i = 0; i < ext_len; i++) {
                    heading->ext_data[i * 3] = (uint8_t)lsd_bitstream_read(bstr, 8);
                    heading->ext_data[i * 3 + 1] = (uint8_t)lsd_bitstream_read(bstr, 8);
                    heading->ext_data[i * 3 + 2] = (uint8_t)lsd_bitstream_read(bstr, 8);
                }
            }
        }
    }
    
    // Set new prefix
    if (new_prefix && new_prefix_len) {
        *new_prefix = lsd_utf16_dup(heading->text, heading->text_length);
        *new_prefix_len = heading->text_length;
    }
    
    free(postfix);
    return true;
}

// ============================================================
// Non-leaf node page operations
// ============================================================

static lsd_node_page *lsd_node_page_create(void) {
    return calloc(1, sizeof(lsd_node_page));
}

static void lsd_node_page_destroy(lsd_node_page *page) {
    if (!page) return;
    
    if (page->children) free(page->children);
    if (page->keys) {
        for (size_t i = 0; i < page->key_count; i++) {
            if (page->keys[i]) free(page->keys[i]);
        }
        free(page->keys);
    }
    if (page->key_lengths) free(page->key_lengths);
    
    free(page);
}

static bool lsd_node_page_load_body(lsd_node_page *page, 
                                 lsd_bitstream *bstr, 
                                 lsd_decoder *decoder) {
    if (!page || !bstr || !decoder) return false;
    
    uint32_t key_count = page->header.headings_count;
    
    // Allocate children array (key_count children)
    page->child_count = key_count;
    page->children = calloc(page->child_count, sizeof(uint32_t));
    if (!page->children) {
        return false;
    }
    
    // Read first child reference
    if (!lsd_decoder_read_reference1(decoder, bstr, &page->children[0])) {
        return false;
    }
    
    if (key_count > 0) {
        page->keys = calloc(key_count, sizeof(uint16_t *));
        page->key_lengths = calloc(key_count, sizeof(size_t));
        if (!page->keys || !page->key_lengths) {
            return false;
        }
        page->key_count = key_count;
        
        // For prefix compression: save the last complete key
        uint16_t *prev_key = NULL;
        size_t prev_key_len = 0;
        
        for (uint32_t i = 0; i < key_count; i++) {
            if (i == key_count - 1) {
                // Last key is an empty string
                page->keys[i] = malloc(sizeof(uint16_t));
                if (page->keys[i]) {
                    page->keys[i][0] = 0;
                }
                page->key_lengths[i] = 0;
                continue;
            }
            
            // Decode shared prefix length
            uint32_t shared_len;
            if (!lsd_decoder_decode_prefix_len(decoder, bstr, &shared_len)) {
                if (prev_key) free(prev_key);
                return false;
            }
            
            // Decode postfix length
            uint32_t suffix_len;
            if (!lsd_decoder_decode_postfix_len(decoder, bstr, &suffix_len)) {
                if (prev_key) free(prev_key);
                return false;
            }
            
            // Decode postfix text
            uint16_t *suffix = NULL;
            size_t suffix_length = 0;
            if (!lsd_decoder_decode_heading(decoder, bstr, suffix_len, &suffix, &suffix_length)) {
                if (prev_key) free(prev_key);
                return false;
            }
            
            // Build complete key
            size_t actual_shared_len = shared_len;
            if (actual_shared_len > prev_key_len) {
                actual_shared_len = prev_key_len;
            }
            
            page->keys[i] = lsd_utf16_concat(prev_key, actual_shared_len, 
                                                 suffix, suffix_length, 
                                                 &page->key_lengths[i]);
            
            // Update prev_key
            if (prev_key) free(prev_key);
            prev_key = lsd_utf16_dup(page->keys[i], page->key_lengths[i]);
            prev_key_len = page->key_lengths[i];
            
            free(suffix);
            
            // Read next child reference
            if (!lsd_decoder_read_reference1(decoder, bstr, &page->children[i + 1])) {
                if (prev_key) free(prev_key);
                return false;
            }
        }
        
        if (prev_key) free(prev_key);
    }
    
    return true;
}

// ============================================================
// Leaf page operations
// ============================================================

static lsd_leaf_page *lsd_leaf_page_create(void) {
    return calloc(1, sizeof(lsd_leaf_page));
}

static void lsd_leaf_page_destroy(lsd_leaf_page *page) {
    if (!page) return;
    
    if (page->headings) {
        for (size_t i = 0; i < page->heading_count; i++) {
            lsd_heading_destroy(&page->headings[i]);
        }
        free(page->headings);
    }
    
    free(page);
}

static bool lsd_leaf_page_load_body(lsd_leaf_page *page, 
                                 lsd_bitstream *bstr, 
                                 lsd_decoder *decoder,
                                 const uint16_t *known_prefix,
                                 size_t known_prefix_len) {
    if (!page || !bstr || !decoder) return false;
    
    uint32_t heading_count = page->header.headings_count;
    
    if (heading_count > 0) {
        page->headings = calloc(heading_count, sizeof(lsd_heading));
        if (!page->headings) {
            return false;
        }
        page->heading_count = heading_count;
        
        // Current prefix (for incremental decoding)
        uint16_t *current_prefix = NULL;
        size_t current_prefix_len = 0;
        
        if (known_prefix && known_prefix_len > 0) {
            current_prefix = lsd_utf16_dup(known_prefix, known_prefix_len);
            current_prefix_len = known_prefix_len;
        }
        
        for (uint32_t i = 0; i < heading_count; i++) {
            uint16_t *new_prefix = NULL;
            size_t new_prefix_len = 0;
            
            if (!lsd_heading_load(&page->headings[i], decoder, bstr,
                                      current_prefix, current_prefix_len,
                                      &new_prefix, &new_prefix_len)) {
                if (current_prefix) free(current_prefix);
                return false;
            }
            
            // Update current prefix
            if (current_prefix) free(current_prefix);
            current_prefix = new_prefix;
            current_prefix_len = new_prefix_len;
        }
        
        if (current_prefix) free(current_prefix);
    }
    
    return true;
}

// ============================================================
// Page store operations
// ============================================================

lsd_page_store *lsd_page_store_create(lsd_bitstream *bstr,
                                      lsd_decoder *decoder,
                                      uint32_t pages_offset,
                                      size_t total_pages) {
    lsd_page_store *store = calloc(1, sizeof(lsd_page_store));
    if (!store) return NULL;
    
    store->bstr = bstr;
    store->decoder = decoder;
    store->pages_offset = pages_offset;
    
    // Allocate non-leaf node array (indexed by page number)
    store->node_pages = calloc(total_pages, sizeof(lsd_node_page *));
    if (!store->node_pages) {
        free(store);
        return NULL;
    }
    store->node_capacity = total_pages;
    store->node_count = 0;
    
    // Initialize leaf node LRU cache
    store->leaf_head = NULL;
    store->leaf_tail = NULL;
    store->leaf_capacity = LSD_LEAF_CACHE_CAPACITY;
    store->leaf_count = 0;
    
    return store;
}

void lsd_page_store_destroy(lsd_page_store *store) {
    if (!store) return;
    
    // Free non-leaf node cache
    if (store->node_pages) {
        for (size_t i = 0; i < store->node_capacity; i++) {
            if (store->node_pages[i]) {
                lsd_node_page_destroy(store->node_pages[i]);
            }
        }
        free(store->node_pages);
    }
    
    // Free leaf node LRU cache
    lsd_leaf_cache_node *node = store->leaf_head;
    while (node) {
        lsd_leaf_cache_node *next = node->next;
        if (node->page) {
            lsd_leaf_page_destroy(node->page);
        }
        free(node);
        node = next;
    }
    
    free(store);
}

static lsd_node_page *cache_lookup_node(lsd_page_store *store, uint16_t page_number) {
    if (!store || page_number >= store->node_capacity) return NULL;
    return store->node_pages[page_number];
}

static void cache_insert_node(lsd_page_store *store, lsd_node_page *page) {
    if (!store || !page) return;
    
    uint16_t page_number = page->header.number;
    if (page_number >= store->node_capacity) {
        lsd_node_page_destroy(page);
        return;
    }
    
    // If already cached, free first
    if (store->node_pages[page_number]) {
        lsd_node_page_destroy(store->node_pages[page_number]);
    } else {
        store->node_count++;
    }
    
    store->node_pages[page_number] = page;
}

static lsd_leaf_page *cache_lookup_leaf(lsd_page_store *store, uint16_t page_number) {
    if (!store) return NULL;
    
    // Search in LRU linked list
    lsd_leaf_cache_node *node = store->leaf_head;
    while (node) {
        if (node->page_number == page_number) {
            // Found, move to list head (most recently used)
            if (node != store->leaf_head) {
                // Remove from current position
                if (node->prev) node->prev->next = node->next;
                if (node->next) node->next->prev = node->prev;
                if (node == store->leaf_tail) store->leaf_tail = node->prev;

                // Insert at head
                node->prev = NULL;
                node->next = store->leaf_head;
                if (store->leaf_head) store->leaf_head->prev = node;
                store->leaf_head = node;
            }
            return node->page;
        }
        node = node->next;
    }
    
    return NULL;
}

static void cache_insert_leaf(lsd_page_store *store, lsd_leaf_page *page) {
    if (!store || !page) return;
    
    // Check if already exists
    lsd_leaf_cache_node *existing = store->leaf_head;
    while (existing) {
        if (existing->page_number == page->header.number) {
            // Already exists, update and move to head
            lsd_leaf_page_destroy(existing->page);
            existing->page = page;
            
            if (existing != store->leaf_head) {
                // Move to head
                if (existing->prev) existing->prev->next = existing->next;
                if (existing->next) existing->next->prev = existing->prev;
                if (existing == store->leaf_tail) store->leaf_tail = existing->prev;
                
                existing->prev = NULL;
                existing->next = store->leaf_head;
                if (store->leaf_head) store->leaf_head->prev = existing;
                store->leaf_head = existing;
            }
            return;
        }
        existing = existing->next;
    }
    
    // If cache is full, evict least recently used (tail)
    if (store->leaf_count >= store->leaf_capacity && store->leaf_tail) {
        lsd_leaf_cache_node *to_remove = store->leaf_tail;
        
        if (to_remove->prev) {
            to_remove->prev->next = NULL;
            store->leaf_tail = to_remove->prev;
        } else {
            store->leaf_head = NULL;
            store->leaf_tail = NULL;
        }
        
        if (to_remove->page) {
            lsd_leaf_page_destroy(to_remove->page);
        }
        free(to_remove);
        store->leaf_count--;
    }
    
    // Create new node and insert at head
    lsd_leaf_cache_node *new_node = calloc(1, sizeof(lsd_leaf_cache_node));
    if (!new_node) {
        lsd_leaf_page_destroy(page);
        return;
    }
    
    new_node->page_number = page->header.number;
    new_node->page = page;
    new_node->prev = NULL;
    new_node->next = store->leaf_head;
    
    if (store->leaf_head) {
        store->leaf_head->prev = new_node;
    }
    store->leaf_head = new_node;
    
    if (!store->leaf_tail) {
        store->leaf_tail = new_node;
    }
    
    store->leaf_count++;
}

// ============================================================
// Page store public API
// ============================================================

bool lsd_page_store_read_header(lsd_page_store *store,
                                uint32_t page_number,
                                lsd_page_header *header) {
    if (!store || !header) return false;
    
    size_t page_offset = store->pages_offset + LSD_PAGE_SIZE * page_number;
    lsd_bitstream_seek(store->bstr, page_offset);
    
    memset(header, 0, sizeof(lsd_page_header));
    return lsd_page_header_load(header, store->bstr);
}

lsd_node_page *lsd_page_store_get_node(lsd_page_store *store,
                                       uint16_t page_number) {
    if (!store) return NULL;
    
    // Check cache
    lsd_node_page *cached = cache_lookup_node(store, page_number);
    if (cached) return cached;
    
    // Cache miss, load from file
    size_t page_offset = store->pages_offset + LSD_PAGE_SIZE * page_number;
    lsd_bitstream_seek(store->bstr, page_offset);

    lsd_node_page *page = lsd_node_page_create();
    if (!page) return NULL;
    
    if (!lsd_page_header_load(&page->header, store->bstr)) {
        lsd_node_page_destroy(page);
        return NULL;
    }
    
    if (page->header.is_leaf) {
        lsd_node_page_destroy(page);
        return NULL;
    }
    
    if (!lsd_node_page_load_body(page, store->bstr, store->decoder)) {
        lsd_node_page_destroy(page);
        return NULL;
    }
    
    cache_insert_node(store, page);
    return page;
}

lsd_leaf_page *lsd_page_store_get_leaf(lsd_page_store *store,
                                       uint16_t page_number,
                                       const uint16_t *prefix,
                                       size_t prefix_len) {
    if (!store) return NULL;
    
    // Check LRU cache
    lsd_leaf_page *cached = cache_lookup_leaf(store, page_number);
    if (cached) return cached;
    
    // Cache miss, load from file
    size_t page_offset = store->pages_offset + LSD_PAGE_SIZE * page_number;
    lsd_bitstream_seek(store->bstr, page_offset);

    lsd_leaf_page *page = lsd_leaf_page_create();
    if (!page) return NULL;
    
    if (!lsd_page_header_load(&page->header, store->bstr)) {
        lsd_leaf_page_destroy(page);
        return NULL;
    }
    
    if (!page->header.is_leaf) {
        lsd_leaf_page_destroy(page);
        return NULL;
    }
    
    if (!lsd_leaf_page_load_body(page, store->bstr, store->decoder, prefix, prefix_len)) {
        lsd_leaf_page_destroy(page);
        return NULL;
    }
    
    cache_insert_leaf(store, page);
    return page;
}
