//
//  lsd_huffman.c
//  libud
//
//  Created by kejinlu on 2026/04/11.
//

#include "lsd_huffman.h"
#include "lsd_bitstream.h"
#include <stdlib.h>
#include <string.h>

// ============================================================
// Internal structure definitions
// ============================================================
typedef struct lsd_huffman_node {
    int32_t left;    // Left child node index (positive: node index+1, negative: symbol index-1)
    int32_t right;   // Right child node index
    int32_t parent;  // Parent node index (-1 indicates root node)
    int32_t weight;  // Weight (used during construction)
} lsd_huffman_node;

struct lsd_huffman_tree {
    lsd_huffman_node *nodes;   // Node array
    size_t node_count;               // Number of nodes

    uint32_t *symidx_to_nodeidx;     // Symbol index to node index mapping
    size_t symbol_count;             // Number of symbols

    int next_node_pos;               // Next available node position
};

// ============================================================
// Creation and destruction
// ============================================================

lsd_huffman_tree *lsd_huffman_tree_create(void) {
    lsd_huffman_tree *tree = calloc(1, sizeof(lsd_huffman_tree));
    return tree;
}

void lsd_huffman_tree_destroy(lsd_huffman_tree *tree) {
    if (!tree) return;
    
    if (tree->nodes) {
        free(tree->nodes);
    }
    if (tree->symidx_to_nodeidx) {
        free(tree->symidx_to_nodeidx);
    }
    
    free(tree);
}

// ============================================================
// Internal helper functions
// ============================================================

/**
 * Place a symbol in the Huffman tree
 * @param tree Huffman tree
 * @param sym_idx Symbol index
 * @param node_idx Current node index
 * @param len Remaining depth
 * @return true on success
 */
static bool place_symbol(lsd_huffman_tree *tree, int sym_idx, int node_idx, int len) {
    if (len <= 0) return false;
    
    lsd_huffman_node *node = &tree->nodes[node_idx];
    
    if (len == 1) {
        // Reached target depth, place the symbol
        if (node->left == 0) {
            node->left = -1 - sym_idx;  // Negative indicates leaf node
            tree->symidx_to_nodeidx[sym_idx] = node_idx;
            return true;
        }
        if (node->right == 0) {
            node->right = -1 - sym_idx;
            tree->symidx_to_nodeidx[sym_idx] = node_idx;
            return true;
        }
        return false;
    }
    
    // Recursive placement
    // Try left subtree
    if (node->left == 0) {
        // Create new node
        tree->nodes[tree->next_node_pos].left = 0;
        tree->nodes[tree->next_node_pos].right = 0;
        tree->nodes[tree->next_node_pos].parent = node_idx;
        tree->nodes[tree->next_node_pos].weight = -1;
        node->left = ++tree->next_node_pos;
    }
    if (node->left > 0) {
        if (place_symbol(tree, sym_idx, node->left - 1, len - 1)) {
            return true;
        }
    }
    
    // Try right subtree
    if (node->right == 0) {
        tree->nodes[tree->next_node_pos].left = 0;
        tree->nodes[tree->next_node_pos].right = 0;
        tree->nodes[tree->next_node_pos].parent = node_idx;
        tree->nodes[tree->next_node_pos].weight = -1;
        node->right = ++tree->next_node_pos;
    }
    if (node->right > 0) {
        if (place_symbol(tree, sym_idx, node->right - 1, len - 1)) {
            return true;
        }
    }
    
    return false;
}

// ============================================================
// Reading and decoding
// ============================================================

bool lsd_huffman_tree_read(lsd_huffman_tree *tree, lsd_bitstream *bstr) {
    if (!tree || !bstr) return false;
    
    // Clean up old data
    if (tree->nodes) {
        free(tree->nodes);
        tree->nodes = NULL;
    }
    if (tree->symidx_to_nodeidx) {
        free(tree->symidx_to_nodeidx);
        tree->symidx_to_nodeidx = NULL;
    }
    
    // Read symbol count and bits per length
    int count = (int)lsd_bitstream_read(bstr, 32);
    int bits_per_len = (int)lsd_bitstream_read(bstr, 8);
    int idx_bit_size = lsd_bit_length(count);
    
    if (count <= 0) return false;
    
    // Allocate memory
    tree->symbol_count = count;
    tree->symidx_to_nodeidx = calloc(count, sizeof(uint32_t));
    if (!tree->symidx_to_nodeidx) return false;
    
    // Initialize symbol-to-node mapping to invalid values
    for (int i = 0; i < count; i++) {
        tree->symidx_to_nodeidx[i] = (uint32_t)-1;
    }
    
    // Allocate node array (at most count-1 internal nodes)
    tree->node_count = count - 1;
    tree->nodes = calloc(tree->node_count, sizeof(lsd_huffman_node));
    if (!tree->nodes) {
        free(tree->symidx_to_nodeidx);
        tree->symidx_to_nodeidx = NULL;
        return false;
    }
    
    // Initialize root node
    int root_idx = (int)tree->node_count - 1;
    tree->nodes[root_idx].left = 0;
    tree->nodes[root_idx].right = 0;
    tree->nodes[root_idx].parent = -1;
    tree->nodes[root_idx].weight = -1;
    tree->next_node_pos = 0;
    
    // Read each symbol's length and place it in the tree
    for (int i = 0; i < count; i++) {
        int symidx = (int)lsd_bitstream_read(bstr, idx_bit_size);
        int len = (int)lsd_bitstream_read(bstr, bits_per_len);
        
        if (symidx >= 0 && symidx < count && len > 0) {
            place_symbol(tree, symidx, root_idx, len);
        }
    }
    
    return true;
}

int lsd_huffman_tree_decode(const lsd_huffman_tree *tree, 
                                   lsd_bitstream *bstr, 
                                   uint32_t *sym_idx) {
    if (!tree || !bstr || !sym_idx || tree->node_count == 0) return -1;
    
    // Start from root node
    const lsd_huffman_node *node = &tree->nodes[tree->node_count - 1];
    int len = 0;
    
    for (;;) {
        len++;
        int bit = lsd_bitstream_read(bstr, 1);
        
        if (bit) {
            // Right branch
            if (node->right < 0) {
                // Leaf node
                *sym_idx = -1 - node->right;
                return len;
            }
            node = &tree->nodes[node->right - 1];
        } else {
            // Left branch
            if (node->left < 0) {
                // Leaf node
                *sym_idx = -1 - node->left;
                return len;
            }
            node = &tree->nodes[node->left - 1];
        }
    }
    
    // Unreachable
    return -1;
}

uint32_t lsd_huffman_tree_get_max_len(const lsd_huffman_tree *tree) {
    if (!tree || tree->symbol_count == 0) return 0;
    
    uint32_t max_len = 0;
    
    for (size_t i = 0; i < tree->symbol_count; i++) {
        uint32_t node_idx = tree->symidx_to_nodeidx[i];
        if (node_idx == (uint32_t)-1) continue;
        
        uint32_t len = 1;
        int32_t parent = tree->nodes[node_idx].parent;
        
        while (parent != -1) {
            len++;
            parent = tree->nodes[parent].parent;
        }
        
        if (len > max_len) {
            max_len = len;
        }
    }
    
    return max_len;
}


