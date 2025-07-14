# Data card

The data card deals with binary data, thus there must be a simple format.

## Hamming ECC

Each nibble (4-bit sequence) uses a Hamming (7, 4) code encoded in 1 byte, with the ability to detect 2 bit errors (but not correct them)

Given N bytes to encode, the final encoded output will take 2N bytes.
This also means when decoding, the length must be even.

A byte is split into 2 nibbles, with the one comprised of the lowest bits first.
Each nibble is ECC encoded separately.
In a nibble there are 4 bits d0, d1, d2 and d3, from lowest to highest.
In the output, there are p0, p1, p2, p3 bits, where p1 and above represent hamming code parity data, and p0 is a parity check on the whole block.

### Nibble arrangement

```
| d0 | d1 | d2 | d3 |
```

### Hamming code arrangement

```
| p0 | p1 | p2 | d0 |
| p3 | d1 | d2 | d3 |
```

The bits are arranged such that p0 is the lowest bit and d3 is the highest.

This creates a fairly generic hamming code structure. Do note that p0 is the bit for checking the whole block's parity.
