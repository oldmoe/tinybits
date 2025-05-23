#ifndef TINY_BITS_PACKER_H
#define TINY_BITS_PACKER_H

#include "common.h"

typedef struct tiny_bits_packer {
    unsigned char *buffer;      // Pointer to the allocated buffer
    size_t capacity;         // Total allocated size of the buffer
    size_t current_pos;      // Current position in the buffer (write position)
    HashTable encode_table; // Add the hash table here
    HashTable dictionary;
    uint8_t features;
    // Add any other encoder-specific state here if needed (e.g., string deduplication table later)
} tiny_bits_packer;

static inline unsigned char *tiny_bits_packer_ensure_capacity(tiny_bits_packer *encoder, size_t needed_size) {
    if (!encoder) return NULL;

    size_t available_space = encoder->capacity - encoder->current_pos;
    if (needed_size > available_space) {
        size_t new_capacity = encoder->capacity + needed_size + (encoder->capacity);
        unsigned char *new_buffer = (unsigned char *)realloc(encoder->buffer, new_capacity);
        if (!new_buffer) return NULL;
        encoder->buffer = new_buffer;
        encoder->capacity = new_capacity;
    }
    return encoder->buffer + encoder->current_pos;
}

/**
 * @brief allocates and initializes a new packer
 * 
 * @return pointer to new packer instance
 * 
 * @note the returned packer object must be freed using tiny_bits_packer_destroy()
 */
tiny_bits_packer *tiny_bits_packer_create(size_t initial_capacity, uint8_t features) {
    tiny_bits_packer *encoder = (tiny_bits_packer *)malloc(sizeof(tiny_bits_packer));
    if (!encoder) return NULL;

    encoder->buffer = (unsigned char *)malloc(initial_capacity);
    if (!encoder->buffer) {
        free(encoder);
        return NULL;
    }
    encoder->capacity = initial_capacity;
    encoder->current_pos = 0;
    encoder->features = features;

    // Only allocate hash table if deduplication is enabled
    if (features & TB_FEATURE_STRING_DEDUPE) {
        encoder->encode_table.cache = (HashEntry*)malloc(sizeof(HashEntry) * TB_HASH_CACHE_SIZE);
        if (!encoder->encode_table.cache) {
            //free(encoder->encode_table.buckets);
            free(encoder->buffer);
            free(encoder);
            return NULL;
        }
        encoder->encode_table.cache_size = TB_HASH_CACHE_SIZE;
        encoder->encode_table.cache_pos = 0;
        encoder->encode_table.next_id = 0;
    } else {
        encoder->encode_table.cache = NULL;
        encoder->encode_table.cache_size = 0;
        encoder->encode_table.cache_pos = 0;
        encoder->encode_table.next_id = 0;
    }

    return encoder;
}

/**
 * @brief Resets internal data structure of the packer object
 * 
 * @param encoder The packer instance
 *
 * @note This function allows for more efficient packing by reusing the same packer object
 */
inline void tiny_bits_packer_reset(tiny_bits_packer *encoder) {
    if (!encoder) return;
    encoder->current_pos = 0;  
    if (encoder->features & TB_FEATURE_STRING_DEDUPE) {
        encoder->encode_table.next_id = 0;
        encoder->encode_table.cache_pos = 0;
        memset(encoder->encode_table.bins, 0, TB_HASH_SIZE * sizeof(uint8_t));
    }
    
}

/**
 * @brief Deallocate the packer object and its internal data structures
 * 
 * @param encoder The unpacker instance
 *
 */
void tiny_bits_packer_destroy(tiny_bits_packer *encoder) {
    if (!encoder) return;
    
    if (encoder->features & TB_FEATURE_STRING_DEDUPE) {
        free(encoder->encode_table.cache);
    }
    free(encoder->buffer);
    free(encoder);
}

/**
 * @brief Packs an array header into the buffer
 * 
 * @param encoder Pointer to the packer instance
 * @param arr_len Number of elements in the array
 * @return Number of bytes written, or 0 on error
 * 
 * @note This function only writes the array header, not the elements themselves
 */
static inline int pack_arr(tiny_bits_packer *encoder, int arr_len){
    int written = 0;
    int needed_size;
    uint8_t *buffer;

    if(arr_len < TB_ARR_LEN){
      needed_size = 1;
    } else {
      needed_size = 1 + varint_size((uint64_t)(arr_len - 7));
    }
    buffer = tiny_bits_packer_ensure_capacity(encoder, needed_size);
    if (!buffer) return 0; // Handle error

    if(arr_len < TB_ARR_LEN){
      buffer[0] = TB_ARR_TAG | arr_len;
      written = 1;
    } else {
      buffer[0] = TB_ARR_TAG | TB_ARR_LEN;
      written = 1;
      written += encode_varint((uint64_t)(arr_len - TB_ARR_LEN), buffer + written);
    }
    encoder->current_pos += written;
    return written;
}

/**
 * @brief Packs a map header into the buffer
 * 
 * @param encoder Pointer to the packer instance
 * @param map_len Number of key-value pairs in the map
 * @return Number of bytes written, or 0 on error
 * 
 * @note This function only writes the map header, not the key-value pairs themselves
 */
static inline int pack_map(tiny_bits_packer *encoder, int map_len){
    int written = 0;
    int needed_size;
    uint8_t *buffer;

    if(map_len < TB_MAP_LEN){
      needed_size = 1;
    } else {
      needed_size = 1 + varint_size((uint64_t)(map_len - 15));
    }
    buffer = tiny_bits_packer_ensure_capacity(encoder, needed_size);
    if (!buffer) return 0; // Handle error

    if(map_len < TB_MAP_LEN){
      buffer[0] = TB_MAP_TAG | map_len;
      written = 1;
    } else {
      buffer[0] = TB_MAP_TAG | TB_MAP_LEN;
      written = 1;
      written += encode_varint((uint64_t)(map_len - TB_MAP_LEN), buffer + written);
    }
    encoder->current_pos += written;
    return written;
}

/**
 * @brief Packs an integer value into the buffer
 * 
 * @param encoder Pointer to the packer instance
 * @param value The integer value to pack
 * @return Number of bytes written, or 0 on error
 * 
 * @note Uses a compact representation for small values and SQLite4 like integer encoding for larger values
 */
static inline int pack_int(tiny_bits_packer *encoder, int64_t value){
    int written = 0;
    int needed_size = 10;
    uint8_t *buffer;
    buffer = tiny_bits_packer_ensure_capacity(encoder, needed_size);
    if (!buffer) return 0; // Handle error
    //printf("value is %ld\n", value);

    if (value >= 0 && value < 120) {
        buffer[0] = (uint8_t)(TB_INT_TAG | value);  // No continuation
        //printf("value is %ld, wrote to buffer %x\n", value, buffer[0]);
        encoder->current_pos += 1;
        return 1;
    } else if (value >= 120) {
        buffer[0] = 248;  // Tag for positive with continuation
        value -= 120;
    } else if (value > -7) {
        buffer[0] = (uint8_t)(248 + (-value));  // No continuation
        encoder->current_pos += 1;
        return 1;
    } else {
        buffer[0] = 255;  // Tag for negative with continuation
        value = -(value + 7);  // Store positive magnitude
    }
    // Encode continuation bytes in BER format (7 bits per byte)
    written += encode_varint(value, buffer + 1) + 1 ;
    encoder->current_pos += written;
    return written;
}

static inline int _pack_tag_only(tiny_bits_packer *encoder, uint8_t tag){
    uint8_t *buffer = tiny_bits_packer_ensure_capacity(encoder, 1);
    if (!buffer) return 0; // Handle error
    buffer[0] = tag;
    encoder->current_pos += 1;
    return 1;

}

/**
 * @brief Packs a separator tag into the buffer
 * 
 * @param encoder Pointer to the packer instance
 * @return Number of bytes written, or 0 on error
 */
static inline int pack_separator(tiny_bits_packer *encoder){
    return _pack_tag_only(encoder, (uint8_t)TB_SEP_TAG);
}

/**
 * @brief Packs a NULL value into the buffer
 * 
 * @param encoder Pointer to the packer instance
 * @return Number of bytes written, or 0 on error
 */
static inline int pack_null(tiny_bits_packer *encoder){
    return _pack_tag_only(encoder, (uint8_t)TB_NIL_TAG);
}

/**
 * @brief Packs a TRUE boolean value into the buffer
 * 
 * @param encoder Pointer to the packer instance
 * @return Number of bytes written, or 0 on error
 */
static inline int pack_true(tiny_bits_packer *encoder){
    return _pack_tag_only(encoder, (uint8_t)TB_TRU_TAG);
}

/**
 * @brief Packs a FALSE boolean value into the buffer
 * 
 * @param encoder Pointer to the packer instance
 * @return Number of bytes written, or 0 on error
 */
static inline int pack_false(tiny_bits_packer *encoder){
    return _pack_tag_only(encoder, (uint8_t)TB_FLS_TAG);
}

/**
 * @brief Packs a NaN (Not a Number) value into the buffer
 * 
 * @param encoder Pointer to the packer instance
 * @return Number of bytes written, or 0 on error
 */
static inline int pack_nan(tiny_bits_packer *encoder){
    return _pack_tag_only(encoder, (uint8_t)TB_NAN_TAG);
}

/**
 * @brief Packs a positive infinity value into the buffer
 * 
 * @param encoder Pointer to the packer instance
 * @return Number of bytes written, or 0 on error
 */
static inline int pack_infinity(tiny_bits_packer *encoder){
    return _pack_tag_only(encoder, (uint8_t)TB_INF_TAG);
}

/**
 * @brief Packs a negative infinity value into the buffer
 * 
 * @param encoder Pointer to the packer instance
 * @return Number of bytes written, or 0 on error
 */
static inline int pack_negative_infinity(tiny_bits_packer *encoder){
    return _pack_tag_only(encoder, (uint8_t)TB_NNF_TAG);
}

/**
 * @brief Packs a string into the buffer
 * 
 * @param encoder Pointer to the packer instance
 * @param str Pointer to the string data
 * @param str_len Length of the string in bytes
 * @return Number of bytes written, or 0 on error
 * 
 * @note If string deduplication is enabled, this may store a reference to a previously stored string
 */
static inline int pack_str(tiny_bits_packer *encoder, char* str, uint32_t str_len) {
    uint32_t id = 0;
    int found = 0;
    int written = 0;
    int needed_size = 0;
    uint8_t *buffer;
    uint32_t hash_code = 0;
    uint32_t hash = 0;
    if ((encoder->features & TB_FEATURE_STRING_DEDUPE) && str_len >= 2 && str_len <= 128) {
        hash_code = fast_hash_32(str, str_len);
        hash = hash_code % TB_HASH_SIZE;
        uint8_t index = encoder->encode_table.bins[hash];
        while (index > 0) {
            HashEntry entry = encoder->encode_table.cache[index - 1];
            if (hash_code == entry.hash 
                && str_len == entry.length
                && fast_memcmp(str, encoder->buffer + entry.offset, str_len) == 0 ) {
                id = index - 1;
                found = 1;
                break;
            }
            index = entry.next_index;
        }
    }

    if (found) {
        // Encode existing string ID
        if (id < 31) {
            needed_size = 1;
        } else {
            needed_size = 1 + varint_size(id - 31);
        }
        buffer = tiny_bits_packer_ensure_capacity(encoder, needed_size);
        if (!buffer) return 0;

        if (id < TB_REF_LEN) {
            buffer[0] = TB_REF_TAG | id;
            written = 1;
        } else {
            buffer[0] = TB_REF_TAG | TB_REF_LEN;
            written = 1;
            written += encode_varint(id - TB_REF_LEN, buffer + written);
        }
    } else {
       needed_size = 10 + str_len;
        buffer = tiny_bits_packer_ensure_capacity(encoder, needed_size);
        if (!buffer) return 0;

        if (str_len < TB_STR_LEN) {
            buffer[0] = TB_STR_TAG | str_len;
            written = 1;
            fast_memcpy(buffer + written, str, str_len);
            written += str_len;
        } else {
            buffer[0] = TB_STR_TAG | TB_STR_LEN;
            written = 1;
            written += encode_varint(str_len - TB_STR_LEN, buffer + written);
            memcpy(buffer + written, str, str_len);
            written += str_len;
        }
        
        if ((encoder->features & TB_FEATURE_STRING_DEDUPE) 
            && encoder->encode_table.cache_pos < TB_HASH_CACHE_SIZE
            && str_len >= 2 && str_len <= 128){ 
            HashEntry* new_entry = &encoder->encode_table.cache[encoder->encode_table.cache_pos++];
            new_entry->hash = hash_code; 
            new_entry->length = str_len;
            new_entry->offset = encoder->current_pos + written - str_len;
            new_entry->next_index = encoder->encode_table.bins[hash];
            encoder->encode_table.bins[hash] = encoder->encode_table.cache_pos;
        }

    }

    encoder->current_pos += written;
    return written;
}

/**
 * @brief Packs a double-precision floating point value into the buffer
 * 
 * @param encoder Pointer to the packer instance
 * @param val The double value to pack
 * @return Number of bytes written, or 0 on error
 * 
 * @note If TB_FEATURE_COMPRESS_FLOATS is enabled, this will use a more compact representation for some values
 */
static inline int pack_double(tiny_bits_packer *encoder, double val) {
    int written = 0;
    uint8_t *buffer = tiny_bits_packer_ensure_capacity(encoder, 10);
    if (!buffer) return 0;
    if(isnan(val)){
      return pack_nan(encoder);
    } 
    if(isinf(val)){
      if(val > 0){
        return pack_infinity(encoder);
      } else {
        return pack_negative_infinity(encoder);
      }
    }
    // scaled varint encoding
    if (encoder->features & TB_FEATURE_COMPRESS_FLOATS) {
        double abs_val = fabs(val); ///val >= 0 ? val : -val;
        double scaled; //= abs_val;
        int multiplies = decimal_places_count(abs_val, &scaled);
        if(multiplies >= 0){
            uint64_t integer = (uint64_t)scaled;
            if(integer < (1ULL << 48)) {
                if (!buffer) return 0;
                if(val >= 0){
                    buffer[0] = TB_PFP_TAG | (multiplies);
                } else {
                    buffer[0] = TB_NFP_TAG | (multiplies);
                }
                written++;
                written += encode_varint(integer, buffer + written);
                encoder->current_pos += written;
                return written;
            }
        }

    }
    // Fallback to raw double
    buffer[0] = TB_F64_TAG;
    written++;
    encode_uint64(dtoi_bits(val), buffer + written);
    written += 8;
    encoder->current_pos += written;
    return written;
}

/**
 * @brief Packs a unixtime double-precision floating point value, along with a time zone offset into the buffer
 * 
 * @param encoder Pointer to the packer instance
 * @param val The unixtime double value to pack
 * @param offset The timezone offset (as a +/- seconds)
 * @return Number of bytes written, or 0 on error
 * 
 */
static inline int pack_datetime(tiny_bits_packer *encoder, double val, int16_t offset) {
    int written = 0;
    uint8_t *buffer = tiny_bits_packer_ensure_capacity(encoder, 11);
    if (!buffer) return 0;
    buffer[0] = TB_DTM_TAG;
    buffer[1] = (int8_t) ((offset % 86400) / (60*15)); // convert seconds to multiples of 15 minutes
    written += 2;
    encode_uint64(dtoi_bits(val), buffer + written);
    written += 8;
    encoder->current_pos += written;
    //written += pack_double(encoder, val);
    return written;
}

/**
 * @brief Packs a binary blob (byte array) into the buffer
 * 
 * @param encoder Pointer to the packer instance
 * @param blob Pointer to the binary data
 * @param blob_size Size of the binary data in bytes
 * @return Number of bytes written, or 0 on error
 */
static inline int pack_blob(tiny_bits_packer *encoder, const char* blob, int blob_size){
    int written = 0;
    int needed_size;
    uint8_t *buffer;

    needed_size = 1 + varint_size((uint64_t)blob_size) + blob_size;
    buffer = tiny_bits_packer_ensure_capacity(encoder, needed_size);
    if (!buffer) return 0; // Handle error

    buffer[0] = (uint8_t)TB_BLB_TAG;
    written++;
    written += encode_varint((uint64_t)blob_size, buffer + written);
    memcpy(buffer + written, blob, blob_size);
    written += blob_size;
    encoder->current_pos += written;
    return written;
}

#endif // TINY_BITS_PACKER_H
