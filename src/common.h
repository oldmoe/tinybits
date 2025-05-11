#ifndef TINY_BITS_COMMON_H
#define TINY_BITS_COMMON_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h> // for size_t
#include <math.h>
#include <stdio.h>

#define TB_HASH_SIZE 128
#define TB_HASH_CACHE_SIZE 256
#define MAX_BYTES 9
#define TB_DDP_STR_LEN_MAX 128

// main tags
#define TB_INT_TAG 0x80     // +/- integer
#define TB_REF_TAG 0x60     // deduped string
#define TB_STR_TAG 0x40     // string
#define TB_DBL_TAG 0x20     // double value
#define TB_PFP_TAG 0x20     // + compressed double
#define TB_NFP_TAG 0x30     // - compressed double
#define TB_NAN_TAG 0x2D     // NaN
#define TB_INF_TAG 0x3D     // Infinity
#define TB_NNF_TAG 0x2E     // -Infinity
#define TB_F16_TAG 0x3E     // f16 
#define TB_F32_TAG 0x2F     // float (32bit)
#define TB_F64_TAG 0x3F     // double (64bit)
#define TB_MAP_TAG 0x10     // map { key: value}
#define TB_ARR_TAG 0x08     // array [element1, element2]
#define TB_DTM_TAG 0x07     // datetime
#define TB_NXT_TAG 0x06     // native extensions (multibyte tags)
#define TB_SEP_TAG 0x05     // separator (for group deduplication)
#define TB_EXT_TAG 0x04     // extension (user extentions)
#define TB_BLB_TAG 0x03     // blob
#define TB_NIL_TAG 0x02     // NULL
#define TB_TRU_TAG 0x01     // TRUE
#define TB_FLS_TAG 0x00     // FALSE

// Length values (for string, map & array)
#define TB_STR_LEN 0x1F     // max embedded string length
#define TB_REF_LEN 0x1F     // max embedded reference id
#define TB_MAP_LEN 0x0F     // max embedded map length
#define TB_ARR_LEN 0x07     // max embedded array length

// native extensions TR_NXT_TAG

// Feature flags (from encoder)
#define TB_FEATURE_STRING_DEDUPE    0x01
#define TB_FEATURE_COMPRESS_FLOATS  0x02

static double powers[] = {
    1.0, 
    10.0, 
    100.0, 
    1000.0, 
    10000.0, 
    100000.0, 
    1000000.0, 
    10000000.0, 
    100000000.0, 
    1000000000.0, 
    10000000000.0, 
    100000000000.0, 
    1000000000000.0
};

typedef struct HashEntry {
    uint32_t hash;          // 32-bit hash from fast_hash_32
    uint32_t length;
    uint32_t offset;
    uint32_t next_index;
} HashEntry;

typedef struct HashTable {
    HashEntry* cache; // HASH_SIZE is 2048, use directly or define HASH_SIZE in header
    uint32_t next_id;
    uint32_t cache_size;
    uint32_t cache_pos;
    uint8_t bins[TB_HASH_SIZE];
} HashTable;

static inline uint32_t fast_hash_32(const char* str, uint16_t len) {
    uint32_t hash = len;
    hash = (hash << 24) | 
           ((unsigned char)str[0] << 16) | 
           ((unsigned char)str[1] << 8 ) | 
           ((unsigned char)str[len-1]);
    //hash ^= (((unsigned char)str[len-2] << 24) | ((unsigned char)str[len-1] << 16));
    return hash;
}

static inline int encode_varint(uint64_t value, uint8_t* buffer) {
    if (value <= 240) {
        buffer[0] = (uint8_t)value;  // 1 byte
        return 1;
    } else if (value < 2288) {  // 241 to 248
        value -= 240;
        int prefix = 241 + (value / 256);
        buffer[0] = (uint8_t)prefix;  // A0
        buffer[1] = (uint8_t)(value % 256);  // A1
        return 2;
    } else if (value <= 67823) {  // Up to 249
        value -= 2288;
        buffer[0] = 249;  // A0
        buffer[1] = (uint8_t)(value / 256);  // A1
        buffer[2] = (uint8_t)(value % 256);  // A2
        return 3;
    } else if (value < (1ULL << 24)) {  // 250: 3-byte big-endian
        buffer[0] = 250;  // A0
        buffer[1] = (uint8_t)(value >> 16);  // A1 (most significant)
        buffer[2] = (uint8_t)(value >> 8);   // A2
        buffer[3] = (uint8_t)value;          // A3 (least significant)
        return 4;
    } else if (value < (1ULL << 32)) {  // 251: 4-byte big-endian
        buffer[0] = 251;  // A0
        buffer[1] = (uint8_t)(value >> 24);
        buffer[2] = (uint8_t)(value >> 16);
        buffer[3] = (uint8_t)(value >> 8);
        buffer[4] = (uint8_t)value;
        return 5;
    } else if (value < (1ULL << 40)) {  // 252: 5-byte big-endian
        buffer[0] = 252;  // A0
        buffer[1] = (uint8_t)(value >> 32);
        buffer[2] = (uint8_t)(value >> 24);
        buffer[3] = (uint8_t)(value >> 16);
        buffer[4] = (uint8_t)(value >> 8);
        buffer[5] = (uint8_t)value;
        return 6;
    } else if (value < (1ULL << 48)) {  // 253: 6-byte big-endian
        buffer[0] = 253;  // A0
        buffer[1] = (uint8_t)(value >> 40);
        buffer[2] = (uint8_t)(value >> 32);
        buffer[3] = (uint8_t)(value >> 24);
        buffer[4] = (uint8_t)(value >> 16);
        buffer[5] = (uint8_t)(value >> 8);
        buffer[6] = (uint8_t)value;
        return 7;
    } else if (value < (1ULL << 56)) {  // 254: 7-byte big-endian
        buffer[0] = 254;  // A0
        buffer[1] = (uint8_t)(value >> 48);
        buffer[2] = (uint8_t)(value >> 40);
        buffer[3] = (uint8_t)(value >> 32);
        buffer[4] = (uint8_t)(value >> 24);
        buffer[5] = (uint8_t)(value >> 16);
        buffer[6] = (uint8_t)(value >> 8);
        buffer[7] = (uint8_t)value;
        return 8;
    } else {  // 255: 8-byte big-endian
        buffer[0] = 255;  // A0
        buffer[1] = (uint8_t)(value >> 56);
        buffer[2] = (uint8_t)(value >> 48);
        buffer[3] = (uint8_t)(value >> 40);
        buffer[4] = (uint8_t)(value >> 32);
        buffer[5] = (uint8_t)(value >> 24);
        buffer[6] = (uint8_t)(value >> 16);
        buffer[7] = (uint8_t)(value >> 8);
        buffer[8] = (uint8_t)value;
        return 9;
    }
}

static inline int varint_size(uint64_t value){
    if (value < (1ULL << 48)) {  // 253: 6-byte big-endian
        return 7;
    } else if(value  < (1ULL << 52)){
        return 8;
    }
    return 9;
}

static inline int8_t decode_varint(const uint8_t* buffer, size_t size, size_t pos, uint64_t *value) {
    if(pos >= size) return 0;
    uint8_t prefix = buffer[pos];
    if (prefix <= 240) {
        *value = prefix;
        return 1;
    } else if (prefix >= 241 && prefix <= 248) {
        if (pos + 1 >= size) return 0; // Not enough bytes
        *value = 240 + 256 * (prefix - 241) + buffer[pos+1];
        return 2;
    } else if (prefix == 249){
        if (pos + 2 >= size) return 0; // Not enough bytes
        *value = 2288 + 256 * buffer[pos+1] + buffer[pos+2];
        return 3;
    } else if (prefix == 250){
        if (pos + 3 >= size) return 0; // Not enough bytes
        *value = ((uint64_t)buffer[pos+1] << 16) | ((uint64_t)buffer[pos+2] << 8) | buffer[pos+3];
        return 4;
    } else if (prefix == 251){
        if (pos + 4 >= size) return 0; // Not enough bytes
        *value = ((uint64_t)buffer[pos+1] << 24) | ((uint64_t)buffer[pos+2] << 16) |
               ((uint64_t)buffer[pos+3] << 8) | buffer[pos+4];
        return 5;    
    } else if (prefix == 252){
        if (pos + 5 >= size) return 0; // Not enough bytes
        *value = ((uint64_t)buffer[pos+1] << 32) | ((uint64_t)buffer[pos+2] << 24) |
               ((uint64_t)buffer[pos+3] << 16) | ((uint64_t)buffer[pos+4] << 8) | buffer[pos+5];
        return 6;        
    } else if (prefix == 253){
        if (pos + 6 >= size) return 0; // Not enough bytes
        *value = ((uint64_t)buffer[pos+1] << 40) | ((uint64_t)buffer[pos+2] << 32) |
               ((uint64_t)buffer[pos+3] << 24) | ((uint64_t)buffer[pos+4] << 16) |
               ((uint64_t)buffer[pos+5] << 8) | buffer[pos+6];
        return 7;            
    } else if (prefix == 254){
        if (pos + 7 >= size) return 0; // Not enough bytes
        *value = ((uint64_t)buffer[pos+1] << 48) | ((uint64_t)buffer[pos+2] << 40) |
               ((uint64_t)buffer[pos+3] << 32) | ((uint64_t)buffer[pos+4] << 24) |
               ((uint64_t)buffer[pos+5] << 16) | ((uint64_t)buffer[pos+6] << 8) | buffer[pos+7];
        return 8;            
    } else if (prefix == 255){
        if (pos + 8 >= size) return 0; // Not enough bytes
        *value = ((uint64_t)buffer[pos+1] << 56) | ((uint64_t)buffer[pos+2] << 48) |
               ((uint64_t)buffer[pos+3] << 40) | ((uint64_t)buffer[pos+4] << 32) |
               ((uint64_t)buffer[pos+5] << 24) | ((uint64_t)buffer[pos+6] << 16) |
               ((uint64_t)buffer[pos+7] << 8) | buffer[pos+8];
        return 9;            
    } else {
        return 0;
    }

}

static inline uint64_t decode_varint_old(const uint8_t* buffer, size_t size, size_t *pos) {
    if (*pos >= size) {
        return 0; // not enough buffer
    }

    uint8_t prefix = buffer[*pos];
    if (prefix <= 240) {
        *pos += 1;
        return prefix;
    } else if (prefix >= 241 && prefix <= 248) {
        if (*pos + 1 >= size) return 0; // Not enough bytes
        uint64_t value = 240 + 256 * (prefix - 241) + buffer[*pos+1];
        *pos += 2;
        return value;
    } else if (prefix == 249) {
        if (*pos + 2 >= size) return 0; // Not enough bytes
        uint64_t value = 2288 + 256 * buffer[*pos+1] + buffer[*pos+2];
        *pos += 3;
        return value;
    } else if (prefix == 250) {
        if (*pos + 3 >= size) return 0; // Not enough bytes
        uint64_t value = ((uint64_t)buffer[*pos+1] << 16) | ((uint64_t)buffer[*pos+2] << 8) | buffer[*pos+3];
        *pos += 4;
        return value;
    } else if (prefix == 251) {
        if (*pos + 4 >= size) return 0; // Not enough bytes
        uint64_t value = ((uint64_t)buffer[*pos+1] << 24) | ((uint64_t)buffer[*pos+2] << 16) |
               ((uint64_t)buffer[*pos+3] << 8) | buffer[*pos+4];
        *pos += 5;
        return value;
    } else if (prefix == 252) {
        if (*pos + 5 >= size) return 0; // Not enough bytes
        uint64_t value = ((uint64_t)buffer[*pos+1] << 32) | ((uint64_t)buffer[*pos+2] << 24) |
               ((uint64_t)buffer[*pos+3] << 16) | ((uint64_t)buffer[*pos+4] << 8) | buffer[*pos+5];
        *pos += 6;
        return value;
    } else if (prefix == 253) {
        if (*pos + 6 >= size) return 0; // Not enough bytes
        uint64_t value = ((uint64_t)buffer[*pos+1] << 40) | ((uint64_t)buffer[*pos+2] << 32) |
               ((uint64_t)buffer[*pos+3] << 24) | ((uint64_t)buffer[*pos+4] << 16) |
               ((uint64_t)buffer[*pos+5] << 8) | buffer[*pos+6];
        *pos += 7;
        return value;       
    } else if (prefix == 254) {
        if (*pos + 7 >= size) return 0; // Not enough bytes
        uint64_t value = ((uint64_t)buffer[*pos+1] << 48) | ((uint64_t)buffer[*pos+2] << 40) |
               ((uint64_t)buffer[*pos+3] << 32) | ((uint64_t)buffer[*pos+4] << 24) |
               ((uint64_t)buffer[*pos+5] << 16) | ((uint64_t)buffer[*pos+6] << 8) | buffer[*pos+7];
        *pos += 8;
        return value;       
    } else if (prefix == 255) {
        if (*pos + 8 >= size) return 0; // Not enough bytes
        uint64_t value = ((uint64_t)buffer[*pos+1] << 56) | ((uint64_t)buffer[*pos+2] << 48) |
               ((uint64_t)buffer[*pos+3] << 40) | ((uint64_t)buffer[*pos+4] << 32) |
               ((uint64_t)buffer[*pos+5] << 24) | ((uint64_t)buffer[*pos+6] << 16) |
               ((uint64_t)buffer[*pos+7] << 8) | buffer[*pos+8];
        *pos += 9;
        return value;
    } else {
        return 0;  // Error case
    }
}

static inline int fast_memcmp(const void *ptr1, const void *ptr2, size_t num) {
    if(num < 32){
        const unsigned char *p1 = (const unsigned char*)ptr1;
        const unsigned char *p2 = (const unsigned char*)ptr2;
        for(size_t i = 0; i < num; i++){
            if(p1[i] != p2[i]) return 1; 
        }
    }else{
        return memcmp(ptr1, ptr2, num); 
    }
    return 0;
}

static inline void *fast_memcpy(unsigned char *ptr1, const char *ptr2, size_t num) {
    for(size_t i = 0; i < num; i++){
        ptr1[i] = ptr2[i]; 
    }
    return ptr1;
}

#include <immintrin.h>
#include <stddef.h>
#include <stdint.h>

static inline uint64_t dtoi_bits(double d) {
    union {
        double d;
        uint64_t u;
    } converter;    
    converter.d = d;
    return converter.u;
}

static inline double itod_bits(uint64_t u) {
    union {
        double d;
        uint64_t u;
    } converter;
    converter.u = u;
    return converter.d;
}

static inline void encode_uint64( uint64_t value, uint8_t *buffer) {
    buffer[0] = (value >> 56) & 0xFF;
    buffer[1] = (value >> 48) & 0xFF;
    buffer[2] = (value >> 40) & 0xFF;
    buffer[3] = (value >> 32) & 0xFF;
    buffer[4] = (value >> 24) & 0xFF;
    buffer[5] = (value >> 16) & 0xFF;
    buffer[6] = (value >> 8) & 0xFF;
    buffer[7] = value & 0xFF;
}

static inline uint64_t decode_uint64(const uint8_t *buffer) {
    return ((uint64_t)buffer[0] << 56) |
           ((uint64_t)buffer[1] << 48) |
           ((uint64_t)buffer[2] << 40) |
           ((uint64_t)buffer[3] << 32) |
           ((uint64_t)buffer[4] << 24) |
           ((uint64_t)buffer[5] << 16) |
           ((uint64_t)buffer[6] << 8) |
            (uint64_t)buffer[7];
}

static inline int decimal_places_count(double abs_val, double *scaled) {
    //double abs_val = fabs(val);
    *scaled = abs_val;
    double temp = *scaled;
    if(*scaled == (uint64_t)(*scaled) && *scaled >= abs_val) { return 0;}

    *scaled = abs_val * 10000;
    temp = *scaled;
    if(*scaled == (uint64_t)(*scaled) && *scaled >= abs_val) { 
        *scaled = abs_val * 10;
        if(*scaled == (uint64_t)(*scaled) && *scaled >= abs_val) { return 1;}
        *scaled = abs_val * 100;
        if(*scaled == (uint64_t)(*scaled) && *scaled >= abs_val) { return 2;}
        *scaled = abs_val * 1000;
        if(*scaled == (uint64_t)(*scaled) && *scaled >= abs_val) { return 3;}
        *scaled = temp;
        return 4;
    }

    *scaled = abs_val * 100000000;
    temp = *scaled;
    if(*scaled == (uint64_t)(*scaled) && *scaled >= abs_val) { 
        *scaled = abs_val * 100000;
        if(*scaled == (uint64_t)(*scaled) && *scaled >= abs_val) { return 5;}
        *scaled = abs_val * 1000000;
        if(*scaled == (uint64_t)(*scaled) && *scaled >= abs_val) { return 6;}
        *scaled = abs_val * 10000000;
        if(*scaled == (uint64_t)(*scaled) && *scaled >= abs_val) { return 7;}
        *scaled = temp;
        return 8;
    }

    *scaled = abs_val * 1000000000000;
    temp = *scaled;
    if(*scaled == (uint64_t)(*scaled) && *scaled >= abs_val) { 
        *scaled = abs_val * 1000000000;
        if(*scaled == (uint64_t)(*scaled) && *scaled >= abs_val) { return 9;}
        *scaled = abs_val * 10000000000;
        if(*scaled == (uint64_t)(*scaled) && *scaled >= abs_val) { return 10;}
        *scaled = abs_val * 100000000000;
        if(*scaled == (uint64_t)(*scaled) && *scaled >= abs_val) { return 11;}
        *scaled = temp;
        return 12;
    }
    return -1;
}

#endif
