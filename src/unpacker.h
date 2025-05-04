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
    TINY_BITS_STR,      // str_blob_val.length: byte length of string, str_blob_val.data: pointer to string
    TINY_BITS_BLOB,     // str_blob_val.length: byte length of blob, str_blob_val.data: pointer to blob
    TINY_BITS_TRUE,     // No value
    TINY_BITS_FALSE,    // No value
    TINY_BITS_NULL,     // No value
    TINY_BITS_NAN,      // No value
    TINY_BITS_INF,      // No value
    TINY_BITS_N_INF,    // No value
    TINY_BITS_EXT,      // No value
    TINY_BITS_SEP,      // No balue
    TINY_BITS_FINISHED, // End of buffer
    TINY_BITS_ERROR,     // Parsing error
    TINY_BITS_DATETIME   // double_val: double value
};

// value union
typedef union tiny_bits_value {
    int64_t int_val;    // TINY_BITS_INT
    double double_val;  // TINY_BITS_DOUBLE
    size_t length;      // TINY_BITS_ARRAY, TINY_BITS_MAP,
    struct {            // TINY_BITS_STR, TINY_BITS_BLOB
        const char *data; 
        size_t length;
        int32_t id;
    } str_blob_val;
    struct {            // TINY_BITS_STR, TINY_BITS_BLOB
        double unixtime;
        size_t offset;
    } datetime_val;   
} tiny_bits_value;

// The unpacker data structure
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

/**
 * @brief allocates and initializes a new unpacker
 * 
 * @return pointer to new unpacker instance
 * 
 * @note the returned unpacker object must be freed using tiny_bits_unpacker_destroy()
 */
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

/**
 * @breif Provides a buffer to the unpacker for unpacking
 * 
 * @param decoder The unpakcer instance
 *
 * @param buffer A pointer to the buffer
 *
 * @param size Size of the region to be unpacked
 *
 * @note This function implicitly resets the unpacker object so no need to call tiny_bits_unpacker_reset()
 */
static inline void tiny_bits_unpacker_set_buffer(tiny_bits_unpacker *decoder, const unsigned char *buffer, size_t size) {
    if (!decoder) return;
    if (!buffer || size < 1) return;
    decoder->buffer = buffer;
    decoder->size = size;
    decoder->current_pos = 0;
    decoder->strings_count = 0;
}

/**
 * @brief Resets internal data structure of the unpacker object
 * 
 * @param decoder The unpacker instance
 *
 * @note This function is useful if you want to operate on the same buffer again for some reason
 */
static inline void tiny_bits_unpacker_reset(tiny_bits_unpacker *decoder) {
    if (!decoder) return;
    decoder->current_pos = 0;
    decoder->strings_count = 0;
}


/**
 * @brief Deallocate the unpacker object and its internal data structures
 * 
 * @param decoder The unpacker instance
 *
 */
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

static inline enum tiny_bits_type _unpack_datetime(tiny_bits_unpacker *decoder, uint8_t tag, tiny_bits_value *value){
    size_t pos = decoder->current_pos;
    value->datetime_val.offset = decoder->buffer[pos] * (60*15); // convert offset back to seconds (from multiples of 15 minutes)
    //uint8_t dbl_tag = decoder->buffer[decoder->current_pos++];
    //tiny_bits_value dbl_val;
    //_unpack_double(decoder, dbl_tag, &dbl_val);
    //value->datetime_val.unixtime = dbl_val.double_val;
    uint64_t unixtime = decode_uint64(decoder->buffer + pos + 1);
    value->datetime_val.unixtime = itod_bits(unixtime);
    decoder->current_pos += 9;
    return TINY_BITS_DATETIME;
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
        if(decoder->strings_count < TB_HASH_CACHE_SIZE && len >= 2 && len <= 128){
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

/**
 * @brief Unpacks a value and returns its type while setting its value
 *
 * @param decoder The unpacker instance
 * @param[out] value A supplied tiny_bits_value instance
 * 
 * @return enum tiny_bits_type
 *
 * This is the entry point to unpacking tinybits structures. You keep calling
 * this method repeatedly until it returns TINY_BITS_FINISHED when it reaches end of buffer
 * or if it returns TINY_BITS_ERROR if it stumbles on a malformed or unknown structure.
 *
 * TINY_BITS_SEP means the current object was fully unpacked, and that there is potentially another one
 * this is specifically for stream unpacking multiple objects one after the other as they are being recieved 
 *
 * The location of the value you need in the value union will depend on the returned type as follows
 * 
 * TINY_BITS_TRUE, TINY_BITS_FALSE, TINY_BITS_NULL, TINY_BITS_NAN, TINY_BITS_INF & TINY_BITS_N_INF all
 * don't set the value, the type itself is sufficient information for client code to reconstruct the value.
 *
 * TINY_BITS_INT sets value.int_val
 *
 * TINY_BITS_DOUBLE sets value.double_val
 *
 * TINY_BITS_ARRAY and TINY_BITS_MAP both set value.length, for TINY_BITS_ARRAY it means the number of entries,
 * for TINY_BITS_MAP it means the number of key/value pairs. You have to keep calling unpac_value() afterwards to 
 * get all the members of the stored array/map. Please note that tinybits doesn't do size checks on the elements supplied
 * during packing of arrays/maps. It is the responsibility of client code to ensure a 3 element array actually packs 3 elements.
 * 
 * TINY_BITS_STR & TINY_BITS_BLOB both set the value.str_blob_val struct, which has two members, data, a pointer to the string/blob in the buffer and
 * length. Since some returned strings might be deduplicated, they will return the same data pointer and length value for their other instances, there is also an id
 * value in the struct, which will be only set for strings. You can use to quickly determine the state of the strings as follows
 * 
 * A positive value means the string is a duplicate of a previous string, speficially a duplicate of the (id-1)th unpacked, deduplicatable string
 * 
 * A negative value means the sting is not a duplicate but is deduplicatable
 *
 * A zero value means the string is not deduplicatable and no duplicates should be expected (this is a heuristic, as duplicates may still exist)
 */
static inline enum tiny_bits_type unpack_value(tiny_bits_unpacker *decoder, tiny_bits_value *value) {
    if (!decoder || !value || decoder->current_pos >= decoder->size) {
        return (decoder && decoder->current_pos >= decoder->size) ? TINY_BITS_FINISHED : TINY_BITS_ERROR;
    }

    uint8_t tag = decoder->buffer[decoder->current_pos++];
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
    } else if (tag == TB_DTM_TAG) {
        return _unpack_datetime(decoder, tag, value);
    } else if (tag == TB_SEP_TAG) {
        return TINY_BITS_SEP;
    } else if (tag == TB_EXT_TAG) {
        return TINY_BITS_EXT;
    } else if (tag == TB_TRU_TAG) {
        return TINY_BITS_TRUE;
    } else if (tag == TB_FLS_TAG) {
        return TINY_BITS_FALSE;
    }
    return TINY_BITS_ERROR; // Unknown tag
}

#endif // TINY_BITS_UNPACKER_H

