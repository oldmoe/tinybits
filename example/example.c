#include "../dist/tinybits.h"
#include <stdio.h>

void unpack(tiny_bits_unpacker *unpacker){
    tiny_bits_value value;
    enum tiny_bits_type type = unpack_value(unpacker, &value);    
    switch(type){
      case TINY_BITS_STR: {
        printf("\"%.*s\"", (int) value.str_blob_val.length, value.str_blob_val.data);  
        break;
      }
      case TINY_BITS_MAP: {
        printf("{");
        for(int i= 0; i < value.length; i++){
          unpack(unpacker); // key
          printf(": ");
          unpack(unpacker); // value
          if(i < value.length - 1) printf(", ");
        }
        printf("}");
        break;      
      }
      case TINY_BITS_ARRAY: {
        printf("[");
        for(int i= 0; i < value.length; i++){
          unpack(unpacker);
          if(i < value.length - 1) printf(", ");
        }
        printf("]");           
        break; 
      }
      case TINY_BITS_NAN: {
        printf("NaN");
        break;
      }
      case TINY_BITS_INF: {
        printf("Inf");
        break;
      }
      case TINY_BITS_N_INF: {
        printf("-Inf");
        break;
      }
      case TINY_BITS_DOUBLE: {
        printf("%g", value.double_val);
        break;
      }
      case TINY_BITS_INT: {
        printf("%ld", value.int_val);
        break;
      }
      case TINY_BITS_FINISHED: {
        printf("\n");
        return;
      }
      defult: 
        return;
    }
}

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
    pack_double(packer, 0.2);
    
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
    //tiny_bits_value value;
    //enum tiny_bits_type type = unpack_value(unpacker, &value);    
    // Process the data...
    unpack(unpacker);
    
    // Clean up
    tiny_bits_packer_destroy(packer);
    tiny_bits_unpacker_destroy(unpacker);
    
    return 0;
}
