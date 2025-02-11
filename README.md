# AT6P decompressor

This tool implements a decompressor for AT6P-compressed files. This file format is apparently used
in Chunsoft games for the DS, like *999: Nine Hours, Nine Persons, Nine Doors*.

It doesn't support other formats very well (AT3P, AT4P, and AT5P), but in the meantime, there are
already tools for those made by Pokemon Mystery Dungeon reverse engineers (pxutil). You should be
able to find them [here](https://github.com/PsyCommando/ppmdu_2).

## Format Overview

The only thing AT6P shares with its predecessors is a kind of similar header format:

|Offset (hex)|Size|Purpose|
|-|-|-|
|0|4|Magic (`41 54 36 50`)|
|4|1|Not used (seems to always be a multiple of 8...?)|
|5|2|Size of the compressed file, stored little-endian|
|7|9|Not used (often 00s, but not always??)|
|10|3|Size of the decompressed file, stored little-endian|
|13|1|Not used (often 00)|
|14|1|First byte of data|
|15|1|Not used (often 00, but not always??)|
|16|...|Compressed data|

The compressed data is a stream of bits, intended to be read in sequence from least significant to
most significant in each byte. AT6P has two strategies for using these bits to describe the value of
the next byte:

* Encode the difference between this byte and the next byte using a relatively small number of bits
* Recognize that the next byte is the same as the byte before this one

A backwards variant of exponential-Golomb coding (I don't know the technical names for these things)
is used to store commands. Numbers closest to 0 use very few bits, while numbers further away from 0
use progressively more bits. The decoding process works like this:

* Start with a `count` of 0.
* Read a bit.
    * If it is a 0, increase `count` by 1, and then go back and read another bit.
    * If it is a 1, continue on to the next step
* Excluding the 1 that you just read, read `count` more bits, and store them in a number variable
  called `data`. The first bit should become the least significant bit of `data`; the second bit,
  the next bit greater; etc. in little endian bit order.
* Add (2\^`count`) - 1 to `data`. (x\^y means "x raised to the power of y.")
* `data` is the decoded version of the bits.

For simplicity for me, the following table will list bits in their
"numerical" order -- least significant bit on the right, most significant on the left.

|Bit pattern|Exponential-Golomb coding value|Command|
|-|-|-|
|1|0|Repeat the current byte (i.e., Add to this byte: +0)|
|010|1|Use previous byte's value|
|110|2|Add to this byte: +1|
|00100|3|Add to this byte: -1|
|01100|4|Add to this byte: +2|
|10100|5|Add to this byte: -2|
|11100|6|Add to this byte: +3|
|0001000|7|Add to this byte: -3|
|...|...|...|
|111111110000000|254|Add to this byte: +127|
|00000000100000000|255|Add to this byte: -127|
|00000001100000000|256|Add to this byte: +128|

*999* only supports bit patterns with 0-8 trailing zeroes (i.e., it is a rule that `count <= 8`);
any more zeroes and the game will crash.

As the table shows, the value you get from decoding the bits can be interpreted as a sign-magnitude
value. The least significant bit is the sign, and the rest of the bits make up the value. In
practice, this means you can use the following procedure to transform the second column (`data`)
into the third column:

* Store the value "(`data` mod 2) \* -2 + 1" into a number variable called `sign`. (mod is the
  mathematical "modulo" operation, which will return either 0 or 1 in this case.)
* Store the value "`data` / 2" into a number variable called `magnitude`. (`/` refers to integer
  division -- the result of division after rounding down to the nearest integer, or truncating
  towards 0. We're not dealing with negative decoded numbers so it's all the same.)
* If `magnitude` is 0 and `sign` is -1, the command is a "use the value of the previous byte"
  command.
* Otherwise, the command is to add the value "`sign * magnitude`" to the current byte.

Regarding the values of the "previous" and "current" byte, they're kept track of as follows:

* When using the command with sign = 1 and magnitude = 0, the previous byte and current byte
  are not updated.
* When using all other commands, including the "use previous byte's value" command, the "current
  byte" for this step becomes the previous byte for the next step, and the byte outputted by this
  step becomes the "current byte" for the next step.

These procedures are not necessarily the most efficient ways to read the data, but they should yield
the same results as the in-game decompression code in 999.

Compression should be relatively simple, you just need to reverse the process. There's very little
opportunity for creativity in this process. Wikipedia's page on Exponential-Golomb coding reveals a
fairly simple method for encoding a non-negative number `d` into 999's format:

* Let `n` = `d` + 1
* Let `shift` = floor(logbase(2, n))
* Let `power` = 2\^`shift` -- that is, the greatest power of 2 less than or equal to `n`.
* Output `power` bit by bit, beginning from the least significant bit, for a total of (`shift` + 1)
  bits. This will output the appropriate number of 0s and a 1.
* Output `n` mod `power` -- that is, `n` without its most significant bit -- bit by bit, beginning
  from the least significant bit, for a total of `shift` bits.

*Matching* the original compressed files is made more difficult by the inclusion of purposeless
bytes in the header whose values are not properly zeroed out. I'm not aware of how they could be
generated or calculated, if there is a reasonable way to calculate them at all. Deeper statistical
analysis (or an interview with the developers who wrote Chunsoft's compression tooling) is probably
necessary to say anything concrete about their values, since nothing uses those values.

## Other formats (AT3P, AT4P, AT5P)

*999* doesn't only feature AT6P-compressed files; it also has several AT5P-compressed files, like
the 3D models for items. The AT5P format is very similar to the older AT3P and AT4P formats, which
appeared in older and more popular Chunsoft games like *Pokémon Mystery Dungeon: Explorers of Sky*.
With popularity comes pre-existing research and a greater likelihood of tools that can work with
those formats. And there are indeed several repositories with so-called "PX" decompression code (due
to the 5th byte in the file nearly always being "X" for these types of files, for some reason):

* [pxutil / ppmdu_2](https://github.com/PsyCommando/ppmdu_2) by PsyCommando (CC0 Dedication)
* [SkyTemple](https://github.com/SkyTemple/skytemple-rust/blob/f8ac17e8cc22ff69a323e10cddf4558d7f966d78/src/compression/px.rs)
  by various authors (GPLv3 License)

For completeness, that family of compression formats will also be described here, as it is
implemented in *999*.

All of these formats are LZSS-based formats where certain back-reference lengths, which depend on
the file, are set aside to be given a special meaning different from "copy (this number of) bytes."
These tradeoffs seem fairly well suited to compressing 4bpp graphics.

The header's format changes depending on the exact state of the first 5 bytes of the header.

### AT3P header

AT3P files are distinguished by their file magic of `AT3P` (`41 54 33 50`). This is the simplest
format and it does not include the size of the decompressed file anywhere in its header.

|Offset (hex)|Size|Purpose|
|-|-|-|
|0|4|Magic (`41 54 33 50`)|
|4|1|Mode (see [mode "N"](#at3p-mode--n-) below)|
|5|2|Size of the compressed file, stored little-endian|
|7|9|List of the 9 LZ length nybbles that have special meaning in this file|
|10|...|The compressed data stream|

All the other file headers build off of this one.

### AT4P header

AT4P files add in a 16-bit size that the file should have once it's been decompressed.

|Offset (hex)|Size|Purpose|
|-|-|-|
|0|4|Magic (`41 54 34 50`)|
|4|1|Mode (see [mode "N"](#at3p-mode--n-) below)|
|5|2|Size of the compressed file, stored little-endian|
|7|9|List of the 9 LZ length nybbles that have special meaning in this file|
|**10**|**2**|**Size of the decompressed data, stored little-endian**|
|**12**|...|The compressed data stream|

The size after decompression is used in two ways:

* If the game calls the decompression function with a non-zero buffer size, the game verifies that
  this buffer size matches the decompressed data size in the file **exactly**. If it does not match,
  the function reports that it decompressed 0 bytes of data and exit early.
* The game checks before processing every new compression token whether it has already reached the
  size of the destination buffer, if the game specified a non-zero buffer size. If it has reached or
  exceeded the destination buffer size, the function reports that it decompresed 0 bytes of data and
  exits early. (Note that the decompression function does *not* do full bounds checking. As long as
  the buffer size is at least big enough to hold the first byte of the last token, the game will
  fully decompress the data, reporting the actual number of bytes that it wrote to the buffer.

### AT5P header

AT5P files expand the sizes of the compressed and decompressed file to 24-bit numbers by adding two
additional bytes to the header, compared to AT4P.

|Offset (hex)|Size|Purpose|
|-|-|-|
|0|4|Magic (`41 54 35 50`)|
|4|1|Mode (see [mode "N"](#at3p-mode--n-) below)|
|5|2|**Least significant 16 bits of** the compressed file's size, stored little-endian|
|7|9|List of the 9 LZ length nybbles that have special meaning in this file|
|10|**3**|**24-bit** size of the decompressed data, stored little-endian|
|**13**|**1**|**Most significant 8 bits of the compressed file's size**|
|**14**|...|The compressed data stream|

Everything else is exactly the same as with AT4P.

### AT3P - mode "N"

When the first byte after the 4-byte file magic is `N` (0x4E), it means the file has not been
compressed at all. In this case, the reported "size of the compressed file" is instead treated as a
size of the *uncompressed data*, and that many bytes beginning at offset 7 in the header are copied
into the destination buffer (without bounds checking).

While this mode "N" is checked for all three of the file headers, it would be difficult to use
properly with AT4P files (since the file contents overlap with the "size after decompression" field
checked earlier in the process) and even more difficult to use properly with AT5P files (since the
file contents also overlap with the most significant byte of the "size of the compressed file" --
that is, the size of the uncompressed data at offset 7). So it only really makes sense to use this
"N" mode with AT3P unless you happen to be able to guarantee that your source data file has the
decompressed and compressed size's relevant bytes at the right positions.

### The compression stream and the special LZ lengths

As mentioned earlier, the compressed data takes a format very similar to your average implementation
of [LZSS](https://en.wikipedia.org/wiki/Lempel%E2%80%93Ziv%E2%80%93Storer%E2%80%93Szymanski). The
compression stream consists of interspersed "literal byte" tokens and "back-reference" tokens. As in
most implementations of LZSS, these are distinguished by adding a "flag byte" at the beginning of
every group of 8 tokens to minimize overhead. It uses a 12-bit sliding window size (allowing access
to up to 4096 previously-outputted bytes, with cycles allowed) and a 4-bit length value for these
back-references.

The innovative feature of the compression is the list of 9 bytes in the header. Each byte is
intended to have a unique integer value on the closed interval [0, 15]. Dictionary back-references
in the compressed stream dedicate 4 bits to the length, meaning each byte can be a valid length for
such a back reference (with a bias of -3 added to represent the smallest length as 0). These 9
lengths are treated as special commands with a 4-bit (*not* 12-bit) argument that will output
various two-byte patterns with 3 or more of the same nybble.

Full decoding code after interpreting the header can be implemented as follows:

* Let `mask` = 0.
* Repeat until all bytes in the compressed file have been read:
    * Try to read the next compression token.
        * If `mask` = 0, read a byte from the compressed file. Let `flags` be this byte. Set `mask`
          equal to 128.
        * Let `bit` be "`flags` div `mask`." Set `flags`'s value to "`flags` mod `mask`."
    * If `bit` = 1, the next token is a "literal byte" token. Read one byte from the compressed file
      and append it to the decompression buffer.
    * Otherwise, `bit` = 0. The next token is a "back-reference" token. Read one byte from the
      compressed file. Let `length` be "(the byte read) div 16" and let `x` be "(the byte read)
      mod 16."
        * Let `i` be the first index where `length` can be found in the list of special nybbles, or
          -1 if `length` is not present.
        * If `i` = -1, this token indicates a back-reference. Read an additional byte from the
          compressed file. Let `y` be this byte.
            * Append `length + 3` bytes, beginning from `4096 - (x * 256 + y)` bytes ago in the
              decompression buffer, to the end of the buffer. Copy the bytes 1 at a time, so as to
              allow newly appended bytes to be re-appended to the end of the buffer again if
              `length` and `x * 256 + y` are sufficiently large.
        * Otherwise, `i` indicates a special command to output 2 bytes composed of two nybbles. Let
          `h` be `(x + 1) mod 16` and let `l` be `(x - 1) mod 16`.
            * If `i` = 0, append the bytes "`x` * 16 + `x`" and "`x` * 16 + `x`" to the buffer.
            * If `i` = 1, append the bytes "`x` * 16 + `h`" and "`h` * 16 + `h`" to the buffer, in
              that order.
            * If `i` = 2, append the bytes "`x` * 16 + `l`" and "`x` * 16 + `x`" to the buffer, in
              that order.
            * If `i` = 3, append the bytes "`x` * 16 + `x`" and "`l` * 16 + `x`" to the buffer, in
              that order.
            * If `i` = 4, append the bytes "`x` * 16 + `x`" and "`x` * 16 + `l`" to the buffer, in
              that order.
            * If `i` = 5, append the bytes "`x` * 16 + `l`" and "`l` * 16 + `l`" to the buffer, in
              that order.
            * If `i` = 6, append the bytes "`x` * 16 + `h`" and "`x` * 16 + `x`" to the buffer, in
              that order.
            * If `i` = 7, append the bytes "`x` * 16 + `x`" and "`h` * 16 + `x`" to the buffer, in
              that order.
            * Otherwise, `i` must be 8. Append the bytes "`x` * 16 + `x`" and "`x` * 16 + `h`" to
              the buffer, in that order.
    * Set `mask`'s value to `mask div 2`.

Some things to note about the list of special nybbles:

* Even if duplicate values are found in the list, only the first one counts. (I'm not aware of any
  files created by Chunsoft that have duplicate values in the list of nybbles.)
* If the value of an entry in this list of "nybbles" is outside of the range [0, 15], it effectively
  disables that command, giving you freedom to use that length for dictionary references. (I'm not
  aware of any files created by Chunsoft that use out-of-range values in AT5P files.)
* There is no guarantee that the list of nybbles is in any sorted order. (Every file in *999* that
  I've checked has had the nybbles not sorted in numerical order.)
* Use of these can reduce two literal-byte tokens down to one one-byte token, at the cost of turning
  certain dictionary back-references into multiple back references in a row.

### Compressing

One approach for compression is documented on
[the Project Pokémon website](https://projectpokemon.org/docs/mystery-dungeon-nds/pmd2-px-compression-r45/).
I didn't really internalize everything in that page, but I did read it at least once. My ideas may
have been influenced a bit by it.

I haven't written a compressor for this format yet, so take everything after this point as a brain
dump and treat it with appropriate skepticism/scrutiny.

The way I would approach this is to try compressing the data normally down to a sequence of LZSS
tokens and then do a "peephole optimization" pass where qualifying sequences of two literal bytes
are replaced with commands, adjusting the lengths of back references to valid unclaimed lengths at
the end. But even within this vague initial framework, there are many more potential avenues for
creativity in implementing the compression:

* Is the LZSS compression algorithm used greedy, or can it look ahead for better back-references?
* Are special commands inserted based on which appears first in the compressed file, or based on
  which occurs most often in the file?
* Relatedly, when multiple commands could apply to overlapping areas, which one wins?
* How do you ensure that back reference lengths can be easily represented once a command makes one
  length unusable?
    * It is most difficult to compensate for the loss of the smallest lengths (nybbles 0, 1, and 2
      -- that is, lengths 3, 4, and 5). All the others can be emulated using multiple dictionary
      back-references in a row. Assuming there is a peephole optimization pass that can't see inside
      of back-references and pull literal bytes out of them, that means `00`, `01`, and `02` must
      never show up in the nybble list, leaving four other nybbles to be kept out of the list.
    * It is possible to choose the back-reference lengths such that each back reference expands to
      at most two back-reference tokens. You don't even need to use all seven lengths:
        * Assume 3 is a valid length
        * Assume 4 is a valid length
        * Assume 5 is a valid length
        * 6 = 3 + 3
        * 7 = 3 + 4
        * 8 = 3 + 5
        * 9 = 4 + 5
        * 10 = 5 + 5
        * Assume 11 is a valid length
        * Assume 12 is a valid length
        * Assume 13 is a valid length
        * 14 = 11 + 3
        * 15 = 12 + 3
        * 16 = 13 + 3
        * 17 = 13 + 4
        * 18 = 13 + 5
    * This could reduce the problem to choosing *one* back reference length that occurs most often
      and benefits the most from being turned into a single back-reference length, and hardcoding
      all the other disallowed nybble values (`00`, `01`, `02`, `08`, `09`, and `0A`). But in
      practice, it's probably better compression-wise to let the most common back-references be
      represented by 3 tokens to reduce other very common tokens to 1 token, meaning you're back to
      4 lengths to choose.
    * Is it possible to get optimal compression using a peephole optimization pass with an approach
      like this?
* Is it realistic to brute-force the compression by trying all possible parameters? (Ignoring the
  order they're assigned to the various commands)
    * If we have 16 options for every nybble and *up to* 9 nybbles, the number of combinations is
      16 + 120 + 560 + 1820 + 4368 + 8008 + 11440 + 12870 + 11440 = **50642**.
    * If we have 16 options for every nybble and *exactly* 9 nybbles, the number of combinations is
      only 11440.
    * If we have 13 options for every nybble and *up to* 9 nybbles, that gives
      715 + 1287 + 1716 + 1716 + 1287 + 715 + 286 + 78 + 13 = **7813** combinations.
    * If we have 13 options for every nybble and *exactly* 9 nybbles, that gives only 715
      combinations.
    * So while the full state space may be a bit intensive to brute-force search, some initial
      assumptions about which back-reference lengths will be needed can substantially reduce the
      number of considered possibilities.

## License

To the extent permissible by law, any software in this repository is licensed under the MIT license.
