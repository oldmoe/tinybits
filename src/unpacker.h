#ifndef TINY_BITS_UNPACKER_H
#define TINY_BITS_UNPACKER_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"


// Decoder return types
enum tiny_bits_type {
    TINY_BITS_ARRAY,    // length: number of elements
    TINY_BITS_MAP,      // length: number of key-value pairs
    TINY_BITS_INT,      // int_val: integer value
    TINY_BITS_DOUBLE,   // double_val: double value
    TINY_BITS_STR,      // length: byte length of string
    TINY_BITS_BLOB,     // length: byte length of blob
    TINY_BITS_TRUE,     // No value
    TINY_BITS_FALSE,    // No value
    TINY_BITS_NULL,     // No value
    TINY_BITS_NAN,     // No value
    TINY_BITS_INF,     // No value
    TINY_BITS_N_INF,     // No value
    TINY_BITS_EXT,     // No value
    TINY_BITS_FINISHED, // End of buffer
    TINY_BITS_ERROR     // Parsing error
};

typedef union tiny_bits_value {
    int64_t int_val;    // TINY_BITS_INT
    double double_val;  // TINY_BITS_DOUBLE
    size_t length;      // TINY_BITS_ARRAY, TINY_BITS_MAP,
    struct {            // TINY_BITS_STR, TINY_BITS_BLOB
        const char *data; 
        size_t length;
        int32_t id;
    } str_blob_val;
} tiny_bits_value;

typedef struct tiny_bits_unpacker {
    const unsigned char *buffer;  // Input buffer (read-only)
    size_t size;                  // Total size of buffer
    size_t current_pos;           // Current read position
    struct {
        char *str;    // Pointer to decompressed string data (owned by strings array)
        size_t length; // Length of string
    } *strings;           // Array of decoded strings
    size_t strings_size;  // Capacity of strings array
    size_t strings_count; // Number of strings stored
    HashTable dictionary;
} tiny_bits_unpacker;

tiny_bits_unpacker *tiny_bits_unpacker_create(void) {

    tiny_bits_unpacker *decoder = (tiny_bits_unpacker *)malloc(sizeof(tiny_bits_unpacker));
    if (!decoder) return NULL;
    // String array setup
    decoder->strings_size = 8; // Initial capacity
    decoder->strings = (void *)malloc(decoder->strings_size * sizeof(*decoder->strings));
    if (!decoder->strings) {
        free(decoder);
        return NULL;
    }
    decoder->strings_count = 0;
    return decoder;
}

void tiny_bits_unpacker_set_buffer(tiny_bits_unpacker *decoder, const unsigned char *buffer, size_t size) {
    if (!decoder) return;
    if (!buffer || size < 1) return;
    decoder->buffer = buffer;
    decoder->size = size;
    decoder->current_pos = 0;
    decoder->strings_count = 0;
}

static inline void tiny_bits_unpacker_reset(tiny_bits_unpacker *decoder) {
    if (!decoder) return;
    decoder->current_pos = 0;
    decoder->strings_count = 0;
}

void tiny_bits_unpacker_destroy(tiny_bits_unpacker *decoder) {
    if (!decoder) return;
    if (decoder->strings) {
        free(decoder->strings);
    }
    free(decoder);
}

static inline enum tiny_bits_type _unpack_int(tiny_bits_unpacker *decoder, uint8_t tag, tiny_bits_value *value){
        size_t pos = decoder->current_pos;
        if (tag < 248) { // Small positive (128-247)
            value->int_val = tag - 128;
            return TINY_BITS_INT;
        } else if (tag == 248) { // Positive with continuation
            uint64_t val = decode_varint(decoder->buffer, decoder->size, &pos);
            value->int_val = val + 120;
            decoder->current_pos = pos;
            return TINY_BITS_INT;
        } else if (tag > 248 && tag < 255) { // Small negative (248-254)
            value->int_val = -(tag - 248);
            return TINY_BITS_INT;
        } else { // 255: Negative with continuation
            uint64_t val = decode_varint(decoder->buffer, decoder->size, &pos);
            value->int_val = -(val + 7);
            decoder->current_pos = pos;
            return TINY_BITS_INT;
        }
}

static inline enum tiny_bits_type _unpack_arr(tiny_bits_unpacker *decoder, uint8_t tag, tiny_bits_value *value){
        size_t pos = decoder->current_pos;
        if (tag < 0b00001111) { // Small array (0-30)
            value->length = tag & 0b00000111;
        } else { // Large array
            value->length = decode_varint(decoder->buffer, decoder->size, &pos) + 7;
            decoder->current_pos = pos;
        }
        return TINY_BITS_ARRAY;
}

static inline enum tiny_bits_type _unpack_map(tiny_bits_unpacker *decoder, uint8_t tag, tiny_bits_value *value){
        size_t pos = decoder->current_pos;
        if (tag < 0x1F) { // Small map (0-14)
            value->length = tag & 0x0F;
        } else { // Large map
            value->length = decode_varint(decoder->buffer, decoder->size, &pos) + 15;
            decoder->current_pos = pos;
        }
        return TINY_BITS_MAP;
}

static inline enum tiny_bits_type _unpack_double(tiny_bits_unpacker *decoder, uint8_t tag, tiny_bits_value *value){
        size_t pos = decoder->current_pos;
        if (tag == TB_F64_TAG) { // Raw double
            uint64_t number = decode_uint64(decoder->buffer + pos);
            value->double_val = itod_bits(number);
            decoder->current_pos += 8;
        } else { // Compressed double
            uint64_t number = decode_varint(decoder->buffer, decoder->size, &pos);
            int order = (tag & 0x0F); 
            double fractional = (double)number / powers[order];
            //fractional /= powers[order];
            if(tag & 0x10) fractional = -fractional;        
            value->double_val = fractional;
            decoder->current_pos = pos;
        }
        return TINY_BITS_DOUBLE;
}

static inline enum tiny_bits_type _unpack_blob(tiny_bits_unpacker *decoder, uint8_t tag, tiny_bits_value *value){
        size_t pos = decoder->current_pos;
        size_t len = decode_varint(decoder->buffer, decoder->size, &pos);
        value->str_blob_val.data =  (const char *)decoder->buffer + pos;
        value->str_blob_val.length = len; 
        decoder->current_pos = pos + len;
        return TINY_BITS_BLOB;
}

static inline enum tiny_bits_type _unpack_str(tiny_bits_unpacker *decoder, uint8_t tag, tiny_bits_value *value){
        size_t pos = decoder->current_pos;
        size_t len;
        if (tag < 0x5F) { // Small string (0-30)
            len = tag & 0x1F;
            value->str_blob_val.data =  (const char *)decoder->buffer + pos;
            value->str_blob_val.length = len; 
            decoder->current_pos = pos + len;
        } else if (tag == 0x5F) { // Large string
            len = decode_varint(decoder->buffer, decoder->size, &pos) + 31;
            value->str_blob_val.data =  (const char *)decoder->buffer + pos;
            value->str_blob_val.length = len; 
            decoder->current_pos = pos + len;
        } else { // Deduplicated (small: < 0x7F, large: 0x7F)
            uint32_t id = (tag < 0x7F) ? (tag & 0x1F) : decode_varint(decoder->buffer, decoder->size, &pos) + 31;
            if (id >= decoder->strings_count) return TINY_BITS_ERROR;
            len = decoder->strings[id].length;
            value->str_blob_val.data = decoder->strings[id].str;
            value->str_blob_val.length = len;
            value->str_blob_val.id = id+1;
            decoder->current_pos = pos; // Update pos after varint
            return TINY_BITS_STR;
        }
        value->str_blob_val.id = 0;
        // Handle new string (not deduplicated)
        if(decoder->strings_count < TB_HASH_CACHE_SIZE){
            if (decoder->strings_count >= decoder->strings_size) {
                size_t new_size = decoder->strings_size * 2;
                void *new_strings = realloc(decoder->strings, new_size * sizeof(*decoder->strings));
                if (!new_strings) return TINY_BITS_ERROR;
                decoder->strings = new_strings;
                decoder->strings_size = new_size;
            }
            
            decoder->strings[decoder->strings_count].str =  (char *)decoder->buffer + pos;
            decoder->strings[decoder->strings_count].length = len;
            decoder->strings_count++;
            value->str_blob_val.id = -1 * decoder->strings_count;
        }
        return TINY_BITS_STR;
}

static inline enum tiny_bits_type unpack_value(tiny_bits_unpacker *decoder, tiny_bits_value *value) {
    if (!decoder || !value || decoder->current_pos >= decoder->size) {
        return (decoder && decoder->current_pos >= decoder->size) ? TINY_BITS_FINISHED : TINY_BITS_ERROR;
    }

    uint8_t tag = decoder->buffer[decoder->current_pos++];
    //printf("found tag %X\n", tag);
    // Dispatch based on tag
    if ((tag & TB_INT_TAG) == TB_INT_TAG) { // Integers
        return _unpack_int(decoder, tag, value);
    } else if ((tag & TB_STR_TAG) == TB_STR_TAG) { // Strings
        return _unpack_str(decoder, tag, value);
    } else if (tag == TB_NIL_TAG) {
        return TINY_BITS_NULL;
    } else if (tag == TB_NAN_TAG) {
        return TINY_BITS_NAN;
    } else if (tag == TB_INF_TAG) {
        return TINY_BITS_INF;
    } else if (tag == TB_NNF_TAG) {
        return TINY_BITS_N_INF;
    } else if ((tag & TB_DBL_TAG) == TB_DBL_TAG) { // Doubles
        return _unpack_double(decoder, tag, value);
    } else if ((tag & TB_MAP_TAG) == TB_MAP_TAG) { // Maps
        return _unpack_map(decoder, tag, value);
    } else if ((tag & TB_ARR_TAG) == TB_ARR_TAG) { // Arrays
        return _unpack_arr(decoder, tag, value);
    } else if (tag == TB_BLB_TAG) { // Blob
        return _unpack_blob(decoder, tag, value);
    } else if (tag == TB_TRU_TAG) {
        return TINY_BITS_TRUE;
    } else if (tag == TB_FLS_TAG) {
        return TINY_BITS_FALSE;
    }
    //printf("UNKOWN TAG\n");
    return TINY_BITS_ERROR; // Unknown tag
}

#endif // TINY_BITS_UNPACKER_H

