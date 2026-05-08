# lsd

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C](https://img.shields.io/badge/C-11-blue.svg)](https://en.wikipedia.org/wiki/C11_(C_standard))
[![CI](https://github.com/unidict/lsd/actions/workflows/ci.yml/badge.svg)](https://github.com/unidict/lsd/actions/workflows/ci.yml)

**lsd** — A C library for reading **ABBYY Lingvo** dictionary files (LSD, DSL, LSA), extracting entries, articles, annotations, embedded resources, and audio pronunciations.

## Features

- **LSD dictionaries** — Parse Lingvo binary dictionaries (versions 11–15: user, system, and abbreviation types)
- **DSL text dictionaries** — Read Lingvo text dictionaries (UTF-8, UTF-16LE, UTF-16BE) with dictzip (`.dsl.dz`) support
- **LSA audio archives** — Decode Lingvo pronunciation audio to 16-bit PCM via Ogg Vorbis
- B+ tree index with Huffman-coded entries
- Exact-match lookup and prefix search (autocomplete)
- Extract embedded overlay resources (images, etc.)
- Read dictionary annotations
- LRU page cache for efficient repeated lookups
- Cross-platform: Linux, macOS, Windows

## Building

### Prerequisites

- C compiler with C11 support
- CMake 3.14+
- zlib
- libvorbis (+ libogg)
- libunistring

### Install Dependencies

**macOS:**
```bash
brew install cmake zlib libvorbis libunistring
```

**Ubuntu/Debian:**
```bash
sudo apt-get install cmake zlib1g-dev libvorbis-dev libunistring-dev
```

**Fedora/RHEL:**
```bash
sudo dnf install cmake zlib-devel libvorbis-devel libunistring-devel
```

**Windows:**
Install dependencies using [vcpkg](https://vcpkg.io/):
```cmd
vcpkg install zlib:x64-windows libvorbis:x64-windows libunistring:x64-windows
cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
```

### Build from Source

```bash
git clone https://github.com/unidict/lsd.git
cd lsd

cmake -B build
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure

# Install (optional)
sudo cmake --install build
```

#### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `LSD_BUILD_TESTS` | `ON` | Build test suite |
| `BUILD_SHARED_LIBS` | `OFF` | Build shared library instead of static |

## Supported Formats

### LSD Binary Dictionaries

| Type | Version | Hex | Encryption |
|------|---------|-----|------------|
| User | 11 | `0x00110001` | None |
| User | 12 | `0x00120001` | None |
| User (legacy) | 13 | `0x00131001` | None |
| User | 13 | `0x00132001` | None |
| User | 14 | `0x00142001` | None |
| User | 15 | `0x00152001` | None |
| System | 14 | `0x00141004` | None |
| System | 15 | `0x00151005` | XOR (bitstream) |
| Abbreviation | 14 | `0x00145001` | XOR (symbols) |
| Abbreviation | 15 | `0x00155001` | XOR (symbols) |

### DSL Text Dictionaries

- Plain `.dsl` and dictzip-compressed `.dsl.dz` files
- Auto-detected encoding: UTF-8, UTF-16LE, UTF-16BE
- Sequential article iteration with metadata extraction

### LSA Audio Archives

- Ogg Vorbis audio with `L9SA` magic header
- Entry-by-index or entry-by-name decoding
- Output: 16-bit signed PCM with sample rate and channel info

## Quick Start

### Reading an LSD Dictionary

```c
#include "lsd_reader.h"
#include <stdio.h>

int main(void) {
    lsd_reader *reader = lsd_reader_open("dictionary.lsd");
    if (!reader) {
        fprintf(stderr, "Failed to open file\n");
        return 1;
    }

    // Get dictionary info
    const lsd_header *hdr = lsd_reader_get_header(reader);
    printf("Version:  0x%08X\n", hdr->version);
    printf("Entries:  %u\n", hdr->entries_count);

    char *name = NULL;
    lsd_reader_get_name(reader, &name);
    printf("Name:     %s\n", name);

    // Iterate entries
    lsd_heading_iter *it = lsd_heading_iter_create(reader);
    const lsd_heading *h;
    int count = 0;
    while ((h = lsd_heading_iter_next(it)) != NULL && count < 10) {
        char *text = NULL;
        lsd_utf16_to_utf8(h->text, h->text_length, &text);
        printf("%s\n", text);
        free(text);
        count++;
    }
    lsd_heading_iter_destroy(it);

    free(name);
    lsd_reader_close(reader);
    return 0;
}
```

### Looking Up and Reading an Article

```c
// Exact-match lookup (case-insensitive)
lsd_heading heading;
memset(&heading, 0, sizeof(heading));
if (lsd_reader_find_heading(reader, "Haus", &heading)) {
    char *article = NULL;
    lsd_reader_read_article(reader, heading.reference, &article);
    printf("Article: %s\n", article);
    free(article);
    lsd_heading_destroy(&heading);
}

// Prefix search (autocomplete)
lsd_heading *results = NULL;
size_t result_count = 0;
if (lsd_reader_prefix(reader, "Ab", 0, &results, &result_count)) {
    for (size_t i = 0; i < result_count; i++) {
        char *text = NULL;
        lsd_utf16_to_utf8(results[i].text, results[i].text_length, &text);
        printf("  %s\n", text);
        free(text);
        lsd_heading_destroy(&results[i]);
    }
    free(results);
}
```

### Reading a DSL Text Dictionary

```c
#include "dsl_reader.h"

dsl_reader *dr = dsl_reader_open("dictionary.dsl");
if (!dr) return 1;

dsl_article article;
while (dsl_reader_next_article(dr, &article) == 0) {
    printf("Heading: %s\n", article.heading);
    printf("Body:    %s\n", article.body);
    dsl_article_free(&article);
}

dsl_reader_close(dr);
```

### Decoding LSA Audio

```c
#include "lsa_reader.h"

lsa_reader *lsa = lsa_reader_open("phrasebook.lsa");
if (!lsa) return 1;

size_t count = lsa_reader_get_entry_count(lsa);
printf("Audio entries: %zu\n", count);

for (size_t i = 0; i < count && i < 5; i++) {
    printf("  %zu: %s\n", i, lsa_reader_get_entry_name(lsa, i));

    int16_t *pcm = NULL;
    size_t size = 0;
    int rate = 0, channels = 0;
    if (lsa_reader_decode(lsa, i, &pcm, &size, &rate, &channels)) {
        printf("    %d Hz, %d ch, %zu bytes\n", rate, channels, size);
        free(pcm);
    }
}

lsa_reader_close(lsa);
```

## Architecture

```
src/
  lsd_reader.h/c       LSD binary dictionary reader (public)
  lsd_types.h/c        LSD file format types (public)
  dsl_reader.h/c       DSL text dictionary reader (public)
  lsa_reader.h/c       LSA audio archive reader (public)
  lsd_decoder.h/c      Version-aware Huffman decoder (private)
  lsd_huffman.h/c      Huffman tree codec (private)
  lsd_bitstream.h/c    Bit-level stream reader (private)
  lsd_page_store.h/c   B+ tree page cache with LRU eviction (private)
  lsd_utils.h/c        UTF-16/UTF-8 string utilities (private)
  lsd_platform.h       Platform abstraction (fseek64, strndup, TLS)
  dsl_dictzip.h/c      Dictzip random-access decompressor (private)
```

## Platform Support

- **Linux** (tested)
- **macOS** (tested)
- **Windows** (MSVC/MinGW)

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## License

```
MIT License

Copyright (c) 2026 kejinlu

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Acknowledgments

lsd incorporates the following third-party components:

- **[Unity](https://github.com/ThrowTheSwitch/Unity)** by ThrowTheSwitch (MIT License) — Test framework
- **[zlib](https://zlib.net/)** by Jean-loup Gailly and Mark Adler (zlib License) — Decompression (dictzip, overlay resources)
- **[libvorbis](https://xiph.org/vorbis/)** by Xiph.Org (BSD License) — Ogg Vorbis audio decoding
- **[libunistring](https://www.gnu.org/software/libunistring/)** by Bruno Haible (LGPL) — Unicode case folding
