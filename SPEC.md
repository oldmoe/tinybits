# TinyBits Binary Format Specification

## Introduction

TinyBits is a compact binary serialization format designed for efficient encoding and decoding of structured data. The format supports various data types including integers, floating-point numbers, strings, arrays, maps, blobs, and special values like null, boolean, and IEEE floating-point special values.

## Version

This specification describes TinyBits format as of April 2025.

## Design Goals

- Compact representation of data
- Fast encoding and decoding
- Support for common data types
- String deduplication for memory efficiency
- Optimized floating-point encoding

## Type System

TinyBits uses a tag-based encoding system where the first byte of each value contains a type tag that determines how to interpret the following bytes.

### Data Types

| Type | Description |
|------|-------------|
| Integer | Signed 64-bit integers |
| String | UTF-8 encoded strings |
| Array | Ordered sequence of values |
| Map | Collection of key-value pairs |
| Double | IEEE 754 64-bit floating-point |
| Compressed Float | Space-efficient floating-point representation |
| Boolean | True or false values |
| Null | Absence of a value |
| Special Float | NaN, +Infinity, -Infinity |
| Blob | Raw binary data |

## Binary Format

### Tag Byte Layout

The first byte of each encoded value indicates its type:

```
0x80-0xFF: Integer
0x40-0x5F: String (inline)
0x60-0x7F: String (reference)
0x20-0x2F: Positive floating-point
0x30-0x3F: Negative floating-point
0x2D: NaN
0x3D: Positive infinity
0x2E: Negative infinity
0x3E: Float16
0x2F: Float32
0x3F: Float64 (IEEE double)
0x10-0x1F: Map
0x08-0x0F: Array
0x04: Extension (reserved)
0x03: Blob
0x02: Null
0x01: True
0x00: False
```

### Integer Encoding

Integers use the high bit (0x80) as a type identifier:

- For integers 0-119: Encoded as `0x80 | value`
- For integers 120 and above: Encoded as `0xF8` followed by a varint encoding of `value - 120`
- For integers -1 to -6: Encoded as `0xF9 + |value|` (249-254)
- For integers below -6: Encoded as `0xFF` followed by a varint encoding of `-(value + 7)`

### String Encoding

Strings are encoded with two different methods:

1. Inline String (0x40-0x5F):
   - For strings 0-30 bytes: `(0x40 | length)` followed by the string data
   - For strings 31+ bytes: `0x5F` followed by a varint encoding of `length - 31`, then the string data

2. Reference String (0x60-0x7F):
   - For referencing previously encoded strings with ID 0-30: `(0x60 | id)`
   - For referencing previously encoded strings with ID 31+: `0x7F` followed by a varint encoding of `id - 31`

### Array Encoding

Arrays are encoded with the 0x08 tag:

- For arrays with 0-6 elements: `(0x08 | length)`
- For arrays with 7+ elements: `0x0F` followed by a varint encoding of `length - 7`

### Map Encoding

Maps are encoded with the 0x10 tag:

- For maps with 0-14 key-value pairs: `(0x10 | length)`
- For maps with 15+ key-value pairs: `0x1F` followed by a varint encoding of `length - 15`

### Double-Precision Floating-Point Encoding

Two encoding methods are used:

1. Raw IEEE-754 double (0x3F):
   - Encoded as `0x3F` followed by 8 bytes containing the IEEE 754 bit representation

2. Compressed floating-point (0x20-0x2F for positive, 0x30-0x3F for negative):
   - Format: `tag` followed by a varint
   - The lower 4 bits of the tag represent the number of decimal places
   - The varint represents the integer value of the scaled number
   - Example: 3.14 is represented as 314 with 2 decimal places

### Special Floating-Point Values

- NaN: Encoded as `0x2D`
- Positive Infinity: Encoded as `0x3D`
- Negative Infinity: Encoded as `0x2E`

### Boolean and Null Encoding

- True: Encoded as `0x01`
- False: Encoded as `0x00`
- Null: Encoded as `0x02`

### Blob Encoding

Blobs are encoded as:
- `0x03` (Blob tag)
- Varint encoding of the blob length
- Raw blob data

## Variable Integer (VarInt) Encoding

TinyBits uses a custom variable-length integer encoding based on the first byte value:

- For values 0-240: Encoded directly as a single byte
- For values 241-2287: Encoded as `241 + (value-241)/256` followed by `(value-241)%256`
- For values 2288-67823: Encoded as `249` followed by two bytes representing `(value-2288)/256` and `(value-2288)%256`
- For larger values (up to 64-bit):
  - `250`: 3-byte big-endian
  - `251`: 4-byte big-endian
  - `252`: 5-byte big-endian
  - `253`: 6-byte big-endian
  - `254`: 7-byte big-endian
  - `255`: 8-byte big-endian

## String Deduplication

The TinyBits encoder maintains a hash table to deduplicate string values:
- Strings between 2-128 bytes in length can be deduplicated
- First occurrence of a string is encoded inline
- Subsequent occurrences use reference encoding
- The hash table uses a 32-bit hash based on string length and content

## Float Compression

Floating-point values can be compressed when they have a relatively small number of decimal places:
- Threshold is 12 decimal places or fewer
- Values are multiplied by the appropriate power of 10
- The resulting integer is encoded as a varint
- The tag byte indicates the number of decimal places and the sign

## Feature Flags

TinyBits supports optional features that can be enabled at encoder creation:
- `TB_FEATURE_STRING_DEDUPE` (0x01): Enable string deduplication
- `TB_FEATURE_COMPRESS_FLOATS` (0x02): Enable floating-point compression

## Implementation Notes

1. The encoder grows its buffer dynamically as needed
2. String deduplication is limited to 256 unique strings
3. The maximum string length for deduplication is 128 bytes
4. The encoder can be reset to reuse memory
5. All multi-byte integer values are stored in big-endian format

## References

For complete implementation details, refer to the TinyBits source code, including:
- `packer.h`: Functions for encoding values
- `unpacker.h`: Functions for decoding values
- `common.h`: Common utilities and constant definitions
