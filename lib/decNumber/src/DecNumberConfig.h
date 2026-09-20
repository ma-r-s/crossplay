#pragma once

// How the vendored decNumber is built, in one place, because every one of these
// is load-bearing and three of them are silently wrong by default.
//
// WHY THIS LIBRARY IS HERE AT ALL: a calculator that prints 0.1 + 0.2 as
// 0.30000000000000004 is a calculator people call broken. Rounding a double to
// twelve significant digits hides that one, and it does NOT hide 0.1 + 0.2 - 0.3
// -- the error is the whole answer there, so it comes out as 5.55e-17 whatever
// the display does. Casio and TI show 0 because their arithmetic is decimal, not
// because their displays are cleverer. This is that arithmetic.
//
// IBM decNumber, ICU licence (see third_party/ICU-license.html), the reference
// implementation of the General Decimal Arithmetic specification. Only the two
// core modules are vendored: decNumber and decContext. The decDouble/decQuad and
// decimal64/128 encodings are the IEEE 754-2008 fixed-width formats and nothing
// here needs them; decDPD.h is not vendored either, because its tables are
// already inlined into decNumberLocal.h and nothing includes the file.

// The coefficient a decNumber can hold. Sized to the WORKING precision the
// calculator sets on its context (sixteen), not to the ten digits it shows: this
// is the struct's capacity, and 34 would make every decNumber 36 bytes instead
// of 24 for nothing.
//
// It also has to stay well under DECBUFFER. decDivideOp's working buffer is
// sized from DECBUFFER, and it falls back to malloc once the context's precision
// passes about DECBUFFER - 4 -- so a precision of 34 against the default buffer
// of 36 would allocate on EVERY divide, on a device where a failed allocation
// mid-sum has nowhere to go.
#define DECNUMDIGITS 20

// Little-endian. ESP32-S3 and the host both are. decNumber does not detect this:
// left wrong, the unit arithmetic reads coefficients backwards.
#define DECLITEND 1

// decNumber allocates only when a working buffer would exceed DECBUFFER digits.
// At sixteen context digits nothing comes near 36, so the allocation path never runs
// -- which matters on a device where a failed malloc in the middle of a sum has
// nowhere to go.
#define DECBUFFER 36

// Off in both cases: DECCHECK adds per-call argument validation and printf, and
// DECALLOC adds guard blocks around every allocation. Development aids, and the
// second one pulls in malloc on a path that is otherwise never taken.
#define DECCHECK 0
#define DECALLOC 0
