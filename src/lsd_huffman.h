//
//  lsd_huffman.h
//  libud
//
//  Created by kejinlu on 2026/04/11.
//

#ifndef lingvo_huffman_h
#define lingvo_huffman_h

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration
typedef struct lsd_bitstream lsd_bitstream;

// ============================================================
// Huffman tree (opaque type)
// ============================================================
typedef struct lsd_huffman_tree lsd_huffman_tree;

// ============================================================
// Create and destroy
// ============================================================

/**
 * Create a Huffman tree
 * @return Huffman tree, or NULL on failure
 */
lsd_huffman_tree *lsd_huffman_tree_create(void);

/**
 * Destroy a Huffman tree
 */
void lsd_huffman_tree_destroy(lsd_huffman_tree *tree);

// ============================================================
// Read and decode
// ============================================================

/**
 * Read a Huffman tree from a bitstream
 * @param tree Huffman tree
 * @param bstr Bitstream reader
 * @return true on success
 */
bool lsd_huffman_tree_read(lsd_huffman_tree *tree, lsd_bitstream *bstr);

/**
 * Decode a symbol using the Huffman tree
 * @param tree Huffman tree
 * @param bstr Bitstream reader
 * @param sym_idx Output symbol index
 * @return Number of bits read
 */
int lsd_huffman_tree_decode(const lsd_huffman_tree *tree, 
                                   lsd_bitstream *bstr, 
                                   uint32_t *sym_idx);

/**
 * Get the maximum code length of the Huffman tree
 */
uint32_t lsd_huffman_tree_get_max_len(const lsd_huffman_tree *tree);

#ifdef __cplusplus
}
#endif

#endif /* lingvo_huffman_h */
