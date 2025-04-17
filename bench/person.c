#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "../dist/tinybits.h"

#define ITERATIONS 10000000 // Bump up since reset is faster

// Timing helper
static inline long get_time_diff(struct timeval *start, struct timeval *end) {
    return (end->tv_sec - start->tv_sec) * 1000000L + (end->tv_usec - start->tv_usec);
}

// Encode the structure (unchanged)
tiny_bits_packer *encode_structure(tiny_bits_packer *enc) {
    pack_map(enc, 3);
    pack_str(enc, "first_name", 11);
    pack_str(enc, "Homer", 5);
    pack_str(enc, "last_name", 10);
    pack_str(enc, "Simpson", 7);
    pack_str(enc, "children", 8);
    pack_arr(enc, 3);
    pack_str(enc, "first_name", 11);
    pack_str(enc, "Bart", 4);
    pack_str(enc, "last_name", 10);
    pack_str(enc, "Simpson", 7);
    pack_str(enc, "children", 8);
    pack_arr(enc, 0);
    pack_str(enc, "first_name", 11);
    pack_str(enc, "Lisa", 4);
    pack_str(enc, "last_name", 10);
    pack_str(enc, "Simpson", 7);
    pack_str(enc, "children", 8);
    pack_arr(enc, 0);
    pack_str(enc, "first_name", 11);
    pack_str(enc, "Maggie", 6);
    pack_str(enc, "last_name", 10);
    pack_str(enc, "Simpson", 7);
    pack_str(enc, "children", 8);
    pack_arr(enc, 0);
    return enc;
}

// Decode with get_data (copy mode)
void decode_copy(tiny_bits_unpacker *dec) {
    tiny_bits_value val;
    char buf[256];
    while (1) {
        enum tiny_bits_type type = unpack_value(dec, &val);
        if (type == TINY_BITS_FINISHED) break;
        if (type == TINY_BITS_ERROR) {
            fprintf(stderr, "Decode copy error\n");
            break;
        }
        if (type == TINY_BITS_STR || type == TINY_BITS_BLOB) {
            size_t len = val.str_blob_val.length; 
            if (len < 0) {
                fprintf(stderr, "Get data error\n");
                break;
            }
        }
    }
}

int main() {
    struct timeval start, end;
    long encode_time = 0, decode_time = 0;

    uint8_t features = TB_FEATURE_STRING_DEDUPE | TB_FEATURE_COMPRESS_FLOATS;
    tiny_bits_packer *enc = tiny_bits_packer_create(256, features);
    if (!enc) return 1;

    // Single packer instance
    printf("Encoding structure once...\n");
    encode_structure(enc);

    // Single unpacker instance
    tiny_bits_unpacker *dec = tiny_bits_unpacker_create();
    if (!dec) {
        fprintf(stderr, "Decoder create failed\n");
        tiny_bits_packer_destroy(enc);
        return 1;
    }

    // Benchmark encoding
    printf("Benchmarking encoding (%d iterations)...\n", ITERATIONS);
    gettimeofday(&start, NULL);
    for (int i = 0; i < ITERATIONS; i++) {
        tiny_bits_packer_reset(enc);
        encode_structure(enc); // Re-encode into same buffer
    }
    gettimeofday(&end, NULL);
    encode_time = get_time_diff(&start, &end);
    printf("Encoding: %ld us (%f ns/iter)\n", encode_time, (double)encode_time * 1000.0 / ITERATIONS);
    printf("Encoded size: %ld bytes\n", enc->current_pos);
    // Benchmark decode with get_data (copy)
    printf("Benchmarking decode with get_data (%d iterations)...\n", ITERATIONS);
    gettimeofday(&start, NULL);
    for (int i = 0; i < ITERATIONS; i++) {
        tiny_bits_unpacker_set_buffer(dec, enc->buffer, enc->current_pos);
        decode_copy(dec);
    }
    gettimeofday(&end, NULL);
    decode_time = get_time_diff(&start, &end);
    printf("Decode (copy): %ld us (%f ns/iter)\n", decode_time, (double)decode_time * 1000.0 / ITERATIONS);

    // Cleanup
    tiny_bits_unpacker_destroy(dec);
    tiny_bits_packer_destroy(enc);

    // Summary
    printf("\nSummary:\n");
    printf("Encoding: %f ns/iter\n", (double)encode_time * 1000.0 / ITERATIONS);
    printf("Decoding: %f ns/iter\n", (double)decode_time * 1000.0 / ITERATIONS);

    return 0;
}
