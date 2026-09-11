# zstd, decompressor only

`src/zstddeclib.c` is Facebook's single-file amalgamation of the zstd 1.5.7
decoder (BSD-3 / GPL-2 dual licence, see the header of the file), with
`zstd.h` and `zstd_errors.h` beside it so callers can include the public API.
The compressor is not here: packs are built on a computer, the device only
reads them (see `docs/apps/wikipedia-pack-format.md`).

Regenerate from a zstd checkout:

```bash
git clone --depth 1 --branch v1.5.7 https://github.com/facebook/zstd.git
cd zstd/build/single_file_libs && ./create_single_file_decoder.sh
cp zstddeclib.c ../../lib/zstd.h ../../lib/zstd_errors.h <this dir>/src/
```

The amalgamation defines `ZSTD_DISABLE_ASM`, `ZSTD_STRIP_ERROR_STRINGS` and
`ZSTD_LEGACY_SUPPORT 0`. Callers that want the static-allocation API
(`ZSTD_initStaticDCtx`, `ZSTD_estimateDCtxSize`) define
`ZSTD_STATIC_LINKING_ONLY` before including `zstd.h`, the way
`src/apps_local/wikipedia/WikipediaPack.cpp` does.
