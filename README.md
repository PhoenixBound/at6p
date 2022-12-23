# AT6P decompressor

This tool implements a decompressor for AT6P-compressed files. This file format is apparently used
in Chunsoft games for the DS, like *999: Nine Hours, Nine Persons, Nine Doors*.

It doesn't support other formats very well (AT3P, AT4P, and AT5P), but in the meantime, there are
already tools for those made by Pokemon Mystery Dungeon reverse engineers (pxutil). You should be
able to find them [here](https://github.com/PsyCommando/ppmdu_2).

## Format Overview

The only thing AT6P shares with its predecessors is a kind of similar header format:

|Offset (hex)|Size|Purpose|
||||
|0|4|Magic (`41 54 36 50`)|
|4|1|Flag byte. I don't know what it means and it's not essential to decompression.|
|5|2|Size of the compressed file, stored little-endian|
|7|9|Zeroes (not used)|
|10|3|Size of the decompressed file, stored little-endian|
|13|1|Zero (not used)|
|14|1|First byte of data|
|15|1|Zero (not used)|
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
* Add 2\^`count` - 1 to `data`. (x\^y means "x raised to the power of y.")
* `data` is the decoded version of the bits.

For simplicity for me, the following table will list bits in their
"numerical" order -- least significant bit on the right, most significant on the left.

|Bit pattern|Exponential-Golomb coding value|Command|
||||
|1|0|Add to this byte: +0|
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

* Store the value "(`data` mod 2) \* -1" into a number variable called `sign`. (mod is the
  mathematical "modulo" operation, which will return either 0 or 1 in this case.)
* Store the value "`data` / 2" into a number variable called `magnitude`. (`/` refers to integer
  division -- the result of division after rounding down to the nearest integer, or truncating
  towards 0. We're not dealing with negative decoded numbers so it's all the same.)
* If `magnitude` is 0 and `sign` is -1, the command is a "use the value of the previous byte"
  command.
* Otherwise, the command is to add the value "`sign * magnitude`" to the current byte.

These procedures are not necessarily the most efficient ways to read the data, but they should yield
the same results as the in-game decompression code in 999.

Compression should be relatively simple, you just need to reverse the process. There's very little
opportunity for creativity in this process. The only places where there's more than one option you
can take is recognizing when the next byte is the same as the previous byte. You should always
take that option, because it uses fewer bits and doesn't affect any bytes that follow.

## License

To the extent permissible by law, any software in this repository is licensed under the MIT license.
