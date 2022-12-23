// AT6P decompressor by phoenixbound
// THIS IS A PROOF OF CONCEPT!
// It has no bounds checking! I should not have written it in C
// Please make something better than this

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

void *decode_at_p_data(uint8_t const *in, uint32_t *out_size) {
	uint32_t in_size = in[5] | ((unsigned)in[6] << 8);
	uint32_t data_offset;
	if (in[0] == 'A' && in[1] == 'T' && in[2] == '4' && in[3] == 'P') {
		*out_size = in[0x10] | ((unsigned)in[0x11] << 8);
		data_offset = 0x12;
	} else if (in[0] == 'A' && in[1] == 'T' && in[2] == '5' && in[3] == 'P') {
		*out_size = in[0x10] | ((unsigned)in[0x11] << 8) | ((uint32_t)in[0x12] << 16);
		in_size |= in[0x13] << 16;
		data_offset = 0x14;
	} else if (in[0] == 'A' && in[1] == 'T' && in[2] == '3' && in[3] == 'P') {
		data_offset = 0x10;
		if (in[4] != 'N') {
			fprintf(stderr, "%s: Formats that don't include the size like AT3P are not supported\n", __func__);
			return NULL;
		}
	} else if (in[0] == 'A' && in[1] == 'T' && in[2] == '6' && in[3] == 'P') {
		*out_size = in[0x10] | ((unsigned)in[0x11] << 8) | ((uint32_t)in[0x12] << 16);
		uint8_t *data = malloc(*out_size);
		if (!data) {
			fprintf(stderr, "%s: malloc of 0x%" PRIX32 " bytes failed\n", __func__, *out_size);
			return NULL;
		}
		if (!decompress_at6p_data(&in[0x14], data, *out_size)) {
			free(data);
			return NULL;
		}
		return data;
	} else {
		fprintf(stderr, "%s: Unknown file format\n", __func__);
		return NULL;
	}

	if (in[4] == 'N') {
		uint8_t *data = malloc(in_size);
		if (!data) {
			fprintf(stderr, "%s: malloc of 0x%" PRIX32 " bytes failed\n", __func__, in_size);
			return NULL;
		}
		return memcpy(data, &in[7], in_size);
	}

	fprintf(stderr, "%s: Decompression of AT3P/AT4P/AT5P files is not implemented yet\n", __func__);
	return NULL;
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
		fprintf(stderr, "%s: malloc of 0x%" PRIX32 " bytes failed\n", __func__, length);
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

int main(int argc, char *argv[]) {
	if (argc < 3) {
		fprintf(stderr, "Usage: %s infile.bin outfile.bin\n", argv[0] ? argv[0] : "999decompress");
		return 1;
	}

	FILE *infile = fopen(argv[1], "rb");
	if (!infile) {
		fprintf(stderr, "Failed to open '%s' for reading\n", argv[1]);
		return 1;
	}
	size_t infile_length;
	uint8_t *data = read_all_bytes(infile, &infile_length);
	if (!data) {
		fclose(infile);
		return 1;
	}
	FILE *outfile = fopen(argv[2], "wb");
	if (!outfile) {
		fprintf(stderr, "Failed to open '%s' for writing\n", argv[2]);
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
		fprintf(stderr, "A problem occurred while writing to '%s'.\n", argv[2]);
		fclose(infile);
		fclose(outfile);
		free(data);
		free(output);
		return 1;
	}
	if (fclose(outfile)) {
		fprintf(stderr, "A problem occurred while flushing writes to '%s'.\n", argv[2]);
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