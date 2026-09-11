// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fforce-enable-int128 -fmcs251-keil -fsyntax-only -verify %s

// The sbit base and index are range-checked on their full APSInt before any
// narrowing. A 128-bit value that would truncate into range must be rejected
// rather than silently accepted by getZExtValue().

sbit A = (((unsigned __int128)1 << 64) + 0x88) ^ 1; // expected-error {{'sbit' base must be a bit-addressable SFR byte (0x80-0xFF with its low 3 bits zero)}}
sbit B = 0x88 ^ (((unsigned __int128)1 << 64) + 1); // expected-error {{'sbit' bit index must be a constant in the range 0-7}}
sbit C = ((unsigned __int128)1 << 64) + 0x24;       // expected-error {{'sbit' bit address 18446744073709551652 is outside the valid range 0-255}}
