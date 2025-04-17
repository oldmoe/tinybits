# tinyBits

TinyBits is a lightweight serialization library designed for efficient encoding and decoding of structured data. It provides compact binary representations for various data types, including integers, strings, arrays, maps, and more.

## Features

- **Compact Serialization**: Optimized for minimal size and fast encoding/decoding.
- **String Deduplication**: Reduces redundancy by reusing previously encoded strings.
- **Floating-Point Compression**: Supports compressed encoding for floating-point numbers.
- **Customizable**: Easily extendable for additional data types or features.

## File Structure

- `src/`: Contains the core implementation of TinyBits.
  - `tinybits.h`: Main header file for the library.
  - `internal/`: Internal implementation details.
    - `common.h`: Shared utilities and constants.
    - `packer.h`: Functions for encoding data.
    - `unpacker.h`: Functions for decoding data.
- `test/`: Placeholder for unit tests.
- `bench/`: Placeholder for benchmarking tools.
- `build.sh`: Script to generate an amalgamated header file.

## Usage

### Building the Amalgamated Header

Run the `build.sh` script to generate a single header file (`dist/tinybits_amalgamated.h`) that includes all necessary components:

```bash
[build.sh](http://_vscodecontentref_/1)