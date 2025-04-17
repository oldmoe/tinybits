#!/bin/bash

# Create dist directory if it doesn't exist
mkdir -p dist

# Output file path
OUTPUT_FILE="dist/tinybits.h"

# Start with a header comment
echo "/**" > "$OUTPUT_FILE"
echo " * TinyBits Amalgamated Header" >> "$OUTPUT_FILE"
echo " * Generated on: $(date)" >> "$OUTPUT_FILE"
echo " */" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"

# Add main include guard
echo "#ifndef TINY_BITS_H" >> "$OUTPUT_FILE"
echo "#define TINY_BITS_H" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"

# Process common.h first (since it's included by others)
echo "/* Begin common.h */" >> "$OUTPUT_FILE"
cat src/common.h | sed '/^#ifndef/d' | sed '/^#define.*_H$/d' | sed '/^#endif/d' >> "$OUTPUT_FILE"
echo "/* End common.h */" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"

# Process packer.h
echo "/* Begin packer.h */" >> "$OUTPUT_FILE"
cat src/packer.h | grep -v "#include" | sed '/^#ifndef/d' | sed '/^#define.*_H/d' | sed '/^#endif/d' >> "$OUTPUT_FILE"
echo "/* End packer.h */" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"

# Process unpacker.h
echo "/* Begin unpacker.h */" >> "$OUTPUT_FILE"
cat src/unpacker.h | grep -v "#include" | sed '/^#ifndef/d' | sed '/^#define.*_H/d' | sed '/^#endif/d' >> "$OUTPUT_FILE"
echo "/* End unpacker.h */" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"

# End main include guard
echo "#endif /* TINY_BITS_AMALGAMATED_H */" >> "$OUTPUT_FILE"

echo "Amalgamated header created at $OUTPUT_FILE"
