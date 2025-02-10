// AT6P decompressor by phoenixbound
// THIS IS A PROOF OF CONCEPT!
// It has no bounds checking! I should not have written it in C
// Please make something better than this

// Naturally, almost all of the files in 999 have Japanese file names... so they're impossible to open
// unless you handle Unicode paths.
#ifdef _WIN32
#define _UNICODE
#include <fcntl.h>
#include <io.h>
#include <tchar.h>
#define CHAR _TCHAR
#define FOPEN _tfopen
#define FPRINTF _ftprintf
#define MAIN _tmain
#define OUTPUT_UNICODE_ON_WINDOWS() do {   \
    _setmode(_fileno(stderr), _O_U16TEXT); \
} while (0)
#define TEXT(...) _TEXT(__VA_ARGS__)
#else
#define CHAR char
#define FOPEN fopen
#define FPRINTF fprintf
#define MAIN main
#define OUTPUT_UNICODE_ON_WINDOWS() do {} while (0)
#define TEXT(...) __VA_ARGS__
#endif

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool decompress_at6p_data(uint8_t const *src /* r9 */, uint8_t *dest /* r10 */, int32_t decompressedSize /* sp0 */) {
	uint8_t current = *src;          // r6
	src += 2;
	int bitsAvailable = 0;           // r11
	int bitsUsed = 0;                // r4
	uint32_t flags = 0;              // r5

	int32_t i = 1;                   // r7
	dest[0] = current;
	uint8_t previous = current;      // r1
	if (decompressedSize <= 1) {
		return true;
	}
	int delta;                       // r8

	for (; i < decompressedSize; ++i) {
		bool emit = true;

		// printf("bitsAvailable: %d\nbitsUsed: %d\nflags: %X\n", bitsAvailable, bitsUsed, flags);
		if (bitsAvailable - bitsUsed < 16) {
			flags &= ~(0xFFFFFFFF << (bitsAvailable - bitsUsed));
			flags |= (uint32_t)(src[0] | (src[1] << 8))  << (bitsAvailable - bitsUsed);
			src += 2;
			bitsAvailable -= bitsUsed;
			bitsAvailable += 16;
			bitsUsed = 0;
			// printf("CHANGED: bitsAvailable: %d\nbitsUsed: %d\nflags: %X\n", bitsAvailable, bitsUsed, flags);
		}

		// We first need to undo the Exponential-Golomb stuff that was done to shrink `delta`
		// To do this, we count how many 0-bits there are at the bottom part of `flags`.
		// That tells us how many bits to read past the first 1. We then add a bias so that
		// there is only one way to encode every number and it's more efficient.
		if (flags & 1) {
			// delta would decode to +0, if any bits were devoted to it
			// printf("Delta = same\n");
			dest[i] = current;
			++bitsUsed;
			flags >>= 1;
			emit = false;
		} else if (flags & 2) {
			delta = ((flags >> 2) & 1) + 1;
			bitsUsed += 3;
			flags >>= 3;
			// delta decodes to either -0 (delta == 1) or +1 (delta == 2)
			if (delta == 1) {
				// If it's -0, that actually means "use the previous byte"
				// printf("Delta = memory\n");
				dest[i] = previous;
				previous = current;
				current = dest[i];
				emit = false;
			}
		} else if (flags & 4) {
			delta = ((flags >> 3) & 3) + 3;
			bitsUsed += 5;
			flags >>= 5;
		} else if (flags & 8) {
			delta = ((flags >> 4) & 7) + 7;
			bitsUsed += 7;
			flags >>= 7;
		} else if (flags & 0x10) {
			delta = ((flags >> 5) & 0xF) + 0xF;
			bitsUsed += 9;
			flags >>= 9;
		} else if (flags & 0x20) {
			delta = ((flags >> 6) & 0x1F) + 0x1F;
			bitsUsed += 11;
			flags >>= 11;
		} else if (flags & 0x40) {
			delta = ((flags >> 7) & 0x3F) + 0x3F;
			bitsUsed += 13;
			flags >>= 13;
		} else if (flags & 0x80) {
			delta = ((flags >> 8) & 0x7F) + 0x7F;
			bitsUsed += 15;
			flags >>= 15;
		} else if (flags & 0x100) {
			bitsUsed += 9;
			flags >>= 9;
			if (bitsAvailable - bitsUsed < 16) {
				flags &= ~(0xFFFFFFFF << (bitsAvailable - bitsUsed));
				flags |= (src[0] | ((uint32_t)src[1] << 8)) << (bitsAvailable - bitsUsed);
				src += 2;
				bitsAvailable -= bitsUsed;
				bitsAvailable += 16;
				bitsUsed = 0;
				// printf("CHANGED: bitsAvailable: %d\nbitsUsed: %d\nflags: %X\n", bitsAvailable, bitsUsed, flags);
			}
			delta = (flags & 0xFF) + 0xFF;
			bitsUsed += 8;
			flags >>= 8;
		} else {
			delta = 0;
			fprintf(stderr, "%s: Decompression failed: found 9+ clear bits after writing %" PRIX32 " bytes\n",
			        __func__, (uint32_t)i);
			return false;
		}

		if (emit) {
			// printf("Delta = %d\n", delta);
			// Decode delta: sign is (delta & 1), magnitude is (delta >> 1)
			if ((delta & 1) == 0) {
				delta >>= 1;
			} else {
				delta = ~(delta - 2) >> 1 | 0x80;
			}
			previous = current;
			current += delta;
			dest[i] = current;
		}
	}
	return true;
}

void *decode_at_p_data(uint8_t const *src, uint32_t *out_size) {
	uint32_t src_size = src[5] | ((unsigned)src[6] << 8);
	uint32_t src_index;
    uint8_t *dest;
    uint32_t dest_index = 0;
    unsigned flag_byte = 0;
    int bit_index = 8;
    
	if (src[0] == 'A' && src[1] == 'T' && src[2] == '4' && src[3] == 'P') {
		*out_size = src[0x10] | ((unsigned)src[0x11] << 8);
		src_index = 0x12;
	} else if (src[0] == 'A' && src[1] == 'T' && src[2] == '5' && src[3] == 'P') {
		*out_size = src[0x10] | ((unsigned)src[0x11] << 8) | ((uint32_t)src[0x12] << 16);
		src_size |= src[0x13] << 16;
		src_index = 0x14;
	} else if (src[0] == 'A' && src[1] == 'T' && src[2] == '3' && src[3] == 'P') {
		src_index = 0x10;
		if (src[4] != 'N') {
			fprintf(stderr, "%s: Formats that don't include the size like AT3P are not supported\n", __func__);
			return NULL;
		}
	} else if (src[0] == 'A' && src[1] == 'T' && src[2] == '6' && src[3] == 'P') {
		*out_size = src[0x10] | ((unsigned)src[0x11] << 8) | ((uint32_t)src[0x12] << 16);
		dest = malloc(*out_size);
		if (!dest) {
			fprintf(stderr, "%s: malloc of 0x%" PRIX32 " bytes failed\n", __func__, *out_size);
			return NULL;
		}
		if (!decompress_at6p_data(&src[0x14], dest, *out_size)) {
			free(dest);
			return NULL;
		}
		return dest;
	} else {
		fprintf(stderr, "%s: Unknown file format\n", __func__);
		return NULL;
	}

	if (src[4] == 'N') {
		dest = malloc(src_size);
		if (!dest) {
			fprintf(stderr, "%s: malloc of 0x%" PRIX32 " bytes failed\n", __func__, src_size);
			return NULL;
		}
        *out_size = src_size;
		return memcpy(dest, &src[7], src_size);
	}

    dest = malloc(*out_size);
    if (!dest) {
        fprintf(stderr, "%s: malloc of 0x%" PRIX32 " bytes failed\n", __func__, src_size);
        return NULL;
    }

	const int xxxx_length = src[7] + 3;
	const int xyyy_plus_length = src[8] + 3;
	const int xyxx_minus_length = src[9] + 3;
	const int xxyx_minus_length = src[0xA] + 3;
	const int xxxy_minus_length = src[0xB] + 3;
	const int xyyy_minus_length = src[0xC] + 3;
	const int xyxx_plus_length = src[0xD] + 3;
	const int xxyx_plus_length = src[0xE] + 3;
	const int xxxy_plus_length = src[0xF] + 3;

	for (; src_index < src_size; ++bit_index, flag_byte <<= 1) {
		if (*out_size != 0 && dest_index >= *out_size) {
			return 0;
		}
		if (bit_index == 8) {
			// Read next flag byte
			flag_byte = src[src_index];
			++src_index;
			bit_index = 0;
		}
		if ((flag_byte & 0x80) == 0) {
			int lz_length = ((src[src_index] & 0xF0) >> 4) + 3;
			int x = src[src_index] & 0xF;

			if (lz_length == xxxx_length)       lz_length = 0x1F;
			if (lz_length == xyyy_plus_length)  lz_length = 0x1E;
			if (lz_length == xyxx_minus_length) lz_length = 0x1D;
			if (lz_length == xxyx_minus_length) lz_length = 0x1C;
			if (lz_length == xxxy_minus_length) lz_length = 0x1B;
			if (lz_length == xyyy_minus_length) lz_length = 0x1A;
			if (lz_length == xyxx_plus_length)  lz_length = 0x19;
			if (lz_length == xxyx_plus_length)  lz_length = 0x18;
			if (lz_length == xxxy_plus_length)  lz_length = 0x17;

			switch (lz_length) {
			case 0x1F:
				// Emit XX XX
				++src_index;
				dest[dest_index] = x | (x << 4);
				dest[dest_index + 1] = (x & 0xF) | ((x & 0xF) << 4);
				dest_index += 2;
				break;
			case 0x1E:
				// Y = X + 1; emit XY YY
				++src_index;
				int nybbleUp = (x + 1) & 0xF;
				dest[dest_index] = nybbleUp | (x << 4);
				dest[dest_index + 1] = nybbleUp | (nybbleUp << 4);
				dest_index += 2;
				break;
			case 0x1D:
				// Y = X - 1; emit XY XX
				++src_index;
				dest[dest_index] = ((x - 1) & 0xF) | (x << 4);
				dest[dest_index + 1] = (x & 0xF) | (x << 4);
				dest_index += 2;
				break;
			case 0x1C:
				// Y = X - 1; emit XX YX
				++src_index;
				dest[dest_index] = x | (x << 4);
				dest[dest_index + 1] = (((x - 1) & 0xF) << 4) | (x & 0xF);
				dest_index += 2;
				break;
			case 0x1B:
				// Y = X - 1; emit XX XY
				++src_index;
				dest[dest_index] = x | (x << 4);
				dest[dest_index + 1] = (x << 4) | ((x - 1) & 0xF);
				dest_index += 2;
				break;
			case 0x1A:
				// Y = X - 1; emit XY YY
				++src_index;
				int nybbleDown = (x - 1) & 0xF;
				dest[dest_index] = nybbleDown | (x << 4);
				dest[dest_index + 1] = nybbleDown | (nybbleDown << 4);
				dest_index += 2;
				break;
			case 0x19:
				// Y = X + 1; emit XY XX
				++src_index;
				dest[dest_index] = ((x + 1) & 0xF) | (x << 4);
				dest[dest_index + 1] = (x & 0xF) | (x << 4);
				dest_index += 2;
				break;
			case 0x18:
				// Y = X + 1; emit XX YX
				++src_index;
				dest[dest_index] = x | (x << 4);
				dest[dest_index + 1] = (x & 0xF) | (((x + 1) & 0xF) << 4);
				dest_index += 2;
				break;
			case 0x17:
				// Y = X + 1; emit XX XY
				++src_index;
				dest[dest_index] = x | (x << 4);
				dest[dest_index + 1] = ((x + 1) & 0xF) | ((x & 0xF) << 4);
				dest_index += 2;
				break;
			default:;
				int lz_index = (x << 8) + src[src_index + 1] + (dest_index - 0x1000);
				src_index += 2;

				for (int i = 0; i < lz_length; ++i) {
					dest[dest_index++] = dest[lz_index++];
				}
				break;
			}
		} else {
			dest[dest_index++] = src[src_index++];
		}
	}

	return dest;
}

static void *read_all_bytes(FILE *f, size_t *f_len) {
	// I'll fix this function to not have undefined behavior later.
	// C is a terrible language...
	if (fseek(f, 0, SEEK_END)) {
		fprintf(stderr, "Seeking into infile failed\n");
		return NULL;
	}
	long length = ftell(f);
	if (length == -1L) {
		perror("Getting length of infile failed");
		return NULL;
	}
	if (fseek(f, 0, SEEK_SET)) {
		fprintf(stderr, "Seeking into infile failed the second time, somehow...\n");
		return NULL;
	}
	void *p = malloc(length);
	if (!p) {
		fprintf(stderr, "%s: malloc of 0x%lX bytes failed\n", __func__, length);
		return NULL;
	}
	if (fread(p, 1, length, f) < length || fgetc(f) != EOF) {
		fprintf(stderr, "%s: read a different number of bytes than expected?!\n", __func__);
		free(p);
		return NULL;
	}
	*f_len = length;
	return p;
}

int MAIN(int argc, CHAR *argv[]) {
	if (argc < 3) {
		OUTPUT_UNICODE_ON_WINDOWS();
        FPRINTF(stderr, TEXT("Usage: %s infile.bin outfile.bin\n"), argv[0] ? argv[0] : TEXT("999decompress"));
		return 1;
	}

	FILE *infile = FOPEN(argv[1], TEXT("rb"));
	if (!infile) {
		OUTPUT_UNICODE_ON_WINDOWS();
        FPRINTF(stderr, TEXT("Failed to open '%s' for reading\n"), argv[1]);
		return 1;
	}
	size_t infile_length;
	uint8_t *data = read_all_bytes(infile, &infile_length);
	if (!data) {
		fclose(infile);
		return 1;
	}
	FILE *outfile = FOPEN(argv[2], TEXT("wb"));
	if (!outfile) {
		OUTPUT_UNICODE_ON_WINDOWS();
        FPRINTF(stderr, TEXT("Failed to open '%s' for writing\n"), argv[2]);
		fclose(infile);
		free(data);
		return 1;
	}
	uint32_t out_size;
	uint8_t *output = decode_at_p_data(data, /* infile_length, */ &out_size);
	if (!output) {
		fprintf(stderr, "Decompression failed\n");
		fclose(infile);
		fclose(outfile);
		free(data);
		return 1;
	}
	if (fwrite(output, 1, out_size, outfile) != out_size) {
		OUTPUT_UNICODE_ON_WINDOWS();
        FPRINTF(stderr, TEXT("A problem occurred while writing to '%s'.\n"), argv[2]);
		fclose(infile);
		fclose(outfile);
		free(data);
		free(output);
		return 1;
	}
	if (fclose(outfile)) {
		OUTPUT_UNICODE_ON_WINDOWS();
        FPRINTF(stderr, TEXT("A problem occurred while flushing writes to '%s'.\n"), argv[2]);
		fclose(infile);
		free(data);
		free(output);
		return 1;
	}
	fclose(infile);
	free(data);
	free(output);
	return 0;
}