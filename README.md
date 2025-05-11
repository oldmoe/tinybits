<img src="https://github.com/oldmoe/tinybits/blob/main/TinyBitsLogo.svg"/>

# TinyBits

A compact and efficient binary serialization library designed for performance and small message sizes.

## Overview

TinyBits is a lightweight C library for serializing and deserializing data in a compact binary format. It features string deduplication, optimized floating-point encoding, and a straightforward API.

## Features

- Minimal dependencies (standard C library only)
- Single header implementation
- Fast encoding and decoding
- String deduplication
- Optimized floating-point representation
- Support for integers, strings, arrays, maps, doubles, booleans, null, and binary blobs
- Configurable feature flags

## Building

TinyBits is designed as a single-header library. The repository contains separate implementation files that are combined into a single header via the build script.

```bash
# Generate the amalgamated header
./build.sh

# The resulting file will be created at dist/tinybits.h
```

Simply include this generated header in your project to use TinyBits.

## Usage

### Basic Example

```c
#include "tinybits.h"
#include <stdio.h>

int main() {
    // Create a packer
    tiny_bits_packer *packer = tiny_bits_packer_create(1024, 
        TB_FEATURE_STRING_DEDUPE | TB_FEATURE_COMPRESS_FLOATS);
    
    // Pack some values
    pack_map(packer, 3);
    
    // Add a string key-value pair
    pack_str(packer, "name", 4);
    pack_str(packer, "TinyBits Library", 16);
    
    // Add a number key-value pair
    pack_str(packer, "version", 7);
    pack_double(packer, 1.0);
    
    // Add a nested array
    pack_str(packer, "features", 8);
    pack_arr(packer, 3);
    pack_str(packer, "compact", 7);
    pack_str(packer, "fast", 4);
    pack_str(packer, "flexible", 8);
    
    // Create unpacker and set buffer
    tiny_bits_unpacker *unpacker = tiny_bits_unpacker_create();
    tiny_bits_unpacker_set_buffer(unpacker, packer->buffer, packer->current_pos);
    
    // Unpack and process values
    tiny_bits_value value;
    enum tiny_bits_type type = unpack_value(unpacker, &value);
    
    // Process the data...
    
    // Clean up
    tiny_bits_packer_destroy(packer);
    tiny_bits_unpacker_destroy(unpacker);
    
    return 0;
}
```

## API Reference

### Encoder API

```c
// Create a new packer with initial capacity and features
// Features:
// - TB_FEATURE_STRING_DEDUPE (0x01): Enable string deduplication
// - TB_FEATURE_COMPRESS_FLOATS (0x02): Enable float compression
tiny_bits_packer *tiny_bits_packer_create(size_t initial_capacity, uint8_t features);

// Reset the packer (reuse existing memory)
void tiny_bits_packer_reset(tiny_bits_packer *encoder);

// Free all resources
void tiny_bits_packer_destroy(tiny_bits_packer *encoder);

// Core packing functions
int pack_int(tiny_bits_packer *encoder, int64_t value);
int pack_str(tiny_bits_packer *encoder, char *str, uint32_t str_len);
int pack_double(tiny_bits_packer *encoder, double val);
int pack_arr(tiny_bits_packer *encoder, int arr_len);
int pack_map(tiny_bits_packer *encoder, int map_len);
int pack_null(tiny_bits_packer *encoder);
int pack_true(tiny_bits_packer *encoder);
int pack_false(tiny_bits_packer *encoder);
int pack_blob(tiny_bits_packer *encoder, const char *blob, int blob_size);

// Special float values
int pack_nan(tiny_bits_packer *encoder);
int pack_infinity(tiny_bits_packer *encoder);
int pack_negative_infinity(tiny_bits_packer *encoder);
```

### Decoder API

```c
// Create a new unpacker
tiny_bits_unpacker *tiny_bits_unpacker_create(void);

// Set the buffer to decode
void tiny_bits_unpacker_set_buffer(tiny_bits_unpacker *decoder, 
                                  const unsigned char *buffer, 
                                  size_t size);

// Reset the unpacker to start position
void tiny_bits_unpacker_reset(tiny_bits_unpacker *decoder);

// Free all resources
void tiny_bits_unpacker_destroy(tiny_bits_unpacker *decoder);

// Unpack the next value
enum tiny_bits_type unpack_value(tiny_bits_unpacker *decoder, tiny_bits_value *value);
```

### Return Types

```c
// Value types returned by the unpacker
enum tiny_bits_type {
    TINY_BITS_ARRAY,    // Array of values
    TINY_BITS_MAP,      // Map of key-value pairs
    TINY_BITS_INT,      // Integer value
    TINY_BITS_DOUBLE,   // Floating-point value
    TINY_BITS_STR,      // String value
    TINY_BITS_BLOB,     // Binary blob
    TINY_BITS_TRUE,     // Boolean true
    TINY_BITS_FALSE,    // Boolean false
    TINY_BITS_NULL,     // Null value
    TINY_BITS_NAN,      // Not-a-Number
    TINY_BITS_INF,      // Positive infinity
    TINY_BITS_N_INF,    // Negative infinity
    TINY_BITS_EXT,      // Extension type (reserved)
    TINY_BITS_FINISHED, // End of buffer
    TINY_BITS_ERROR     // Parsing error
};
```

## Working with Collections

When encoding arrays and maps, first call `pack_arr()` or `pack_map()` with the number of elements, then encode each element in sequence:

```c
// Array with 3 strings
pack_arr(packer, 3);
pack_str(packer, "one", 3);
pack_str(packer, "two", 3);
pack_str(packer, "three", 5);

// Map with 2 key-value pairs
pack_map(packer, 2);
pack_str(packer, "key1", 4);
pack_int(packer, 42);
pack_str(packer, "key2", 4);
pack_str(packer, "value", 5);
```

When decoding, the unpacker will return `TINY_BITS_ARRAY` or `TINY_BITS_MAP` with the count in `value.length`, then you should read that many values:

```c
tiny_bits_value value;
enum tiny_bits_type type = unpack_value(unpacker, &value);

if (type == TINY_BITS_ARRAY) {
    size_t count = value.length;
    // Read 'count' elements from the array
    for (size_t i = 0; i < count; i++) {
        // Unpack the next value
    }
}
```

## Memory Management

- `tiny_bits_packer_create()` allocates memory for the encoder
- `tiny_bits_packer_reset()` reuses existing memory
- `tiny_bits_packer_destroy()` frees all allocated memory
- The encoder automatically grows its buffer as needed

## Feature Flags

### String Deduplication

When `TB_FEATURE_STRING_DEDUPE` is enabled, the packer maintains a hash table of previously encoded strings (2-128 bytes) and sends references instead of duplicating data.

### Float Compression

When `TB_FEATURE_COMPRESS_FLOATS` is enabled, floating-point values with 12 or fewer decimal places are encoded as scaled integers for space efficiency.

## Performance Considerations

- Enable string deduplication for data with many repeated strings
- Reuse encoder/decoder instances when processing multiple messages
- Floating point compression is a little bit expensive

## Todo
- [x] Make sure all buffer reads while unpacking don't go beyond the buffer size
- [ ] Convert the hash entry references to pointers instead of array indexes
- [ ] Make linked lists ordered by the first character of the referenced string
- [ ] Experiment with a doubly linked list to choose the shortest path to search

