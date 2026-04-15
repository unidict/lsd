//
//  lsa_reader.h
//  libud
//
//  Created by kejinlu on 2026/04/14.
//

#ifndef lsa_reader_h
#define lsa_reader_h

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lsa_reader lsa_reader;

// ============================================================
// Creation and destruction
// ============================================================

lsa_reader *lsa_reader_open(const char *filename);
void lsa_reader_close(lsa_reader *reader);

// ============================================================
// Index access
// ============================================================

/** Get the number of valid entries */
size_t lsa_reader_get_entry_count(const lsa_reader *reader);

/** Get the name of the entry at the given index (returns NULL if out of bounds; lifetime tied to reader) */
const char *lsa_reader_get_entry_name(const lsa_reader *reader, size_t index);

// ============================================================
// Audio decoding
// ============================================================

/**
 * Decode an entry to PCM data by index
 * Output format: 16-bit signed, little-endian
 *
 * @param pcm_data Output PCM data (caller must free)
 * @param size     Output size in bytes
 * @param rate     Output sample rate (can be NULL)
 * @param channels Output number of channels (can be NULL)
 */
bool lsa_reader_decode(lsa_reader *reader, size_t index,
                        int16_t **pcm_data, size_t *size,
                        int *rate, int *channels);

/**
 * Decode an entry to PCM data by name
 * Output format: 16-bit signed, little-endian
 */
bool lsa_reader_decode_by_name(lsa_reader *reader, const char *name,
                                int16_t **pcm_data, size_t *size,
                                int *rate, int *channels);

/**
 * Batch export all entries as WAV files (16-bit signed PCM)
 * @param output_dir Output directory (must already exist)
 * @return Number of successfully exported entries
 */
size_t lsa_reader_dump(lsa_reader *reader, const char *output_dir);

#ifdef __cplusplus
}
#endif

#endif /* lsa_reader_h */
