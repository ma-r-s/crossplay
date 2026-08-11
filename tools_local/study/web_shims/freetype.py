"""freetype-py's surface, backed by the wasm FreeType the site ships.

Importable only under Pyodide, and found only there: sync_site.py puts this
directory on the page's sys.path ahead of everything, while the CLI never
sees it and keeps importing the real freetype-py. mono_cpfont.py does
`import freetype` and cannot tell the difference, which is the entire idea.

The backing module is the same FreeType release the freetype-py wheel
bundles (2.13.2), compiled by tools_local/wasm-ft/build.py, so the bitmaps
that come back are bit-for-bit the CLI's. The surface here is exactly what
mono_cpfont.rasterize_mono touches; anything else raises AttributeError
loudly rather than approximating.
"""

import pathlib

import js
from pyodide.ffi import to_js

FT_LOAD_RENDER = 0x4
FT_LOAD_FORCE_AUTOHINT = 0x20
# (FT_RENDER_MODE_MONO & 15) << 16, as freetype-py defines it.
FT_LOAD_TARGET_MONO = 2 << 16


def _module():
    module = getattr(js, "ftModule", None)
    if module is None:
        raise RuntimeError("ft.js is not loaded; the worker must create ftModule first")
    return module


class _SizeMetrics:
    """face.size, 26.6 fixed point, like freetype-py's SizeMetrics."""

    def __init__(self, face_ptr):
        module = _module()
        self.height = int(module._ft_size_height(face_ptr))
        self.ascender = int(module._ft_size_ascender(face_ptr))
        self.descender = int(module._ft_size_descender(face_ptr))


class _Bitmap:
    def __init__(self, face_ptr):
        module = _module()
        self.pitch = int(module._ft_bitmap_pitch(face_ptr))
        self.width = int(module._ft_bitmap_width(face_ptr))
        self.rows = int(module._ft_bitmap_rows(face_ptr))
        length = abs(self.pitch) * self.rows
        if length:
            pointer = int(module._ft_bitmap_buffer(face_ptr))
            self.buffer = bytes(
                module.HEAPU8.subarray(pointer, pointer + length).to_py()
            )
        else:
            self.buffer = b""


class _GlyphSlot:
    def __init__(self, face_ptr):
        module = _module()
        self.bitmap = _Bitmap(face_ptr)
        self.bitmap_left = int(module._ft_bitmap_left(face_ptr))
        self.bitmap_top = int(module._ft_bitmap_top(face_ptr))
        self.linearHoriAdvance = int(module._ft_linear_hori_advance(face_ptr))


class Face:
    def __init__(self, path):
        module = _module()
        if module._ft_init():
            raise RuntimeError("FT_Init_FreeType failed")
        data = pathlib.Path(path).read_bytes()
        self._buffer = int(module._malloc(len(data)))
        if not self._buffer:
            raise MemoryError(f"malloc({len(data)}) failed in the ft module")
        module.HEAPU8.subarray(self._buffer, self._buffer + len(data)).set(to_js(data))
        self._face = int(module._ft_new_memory_face(self._buffer, len(data), 0))
        if not self._face:
            module._free(self._buffer)
            raise RuntimeError(f"FreeType cannot open {path}")
        self.glyph = None

    def set_char_size(self, width, height, hres, vres):
        error = _module()._ft_set_char_size(self._face, width, height, hres, vres)
        if error:
            raise RuntimeError(f"FT_Set_Char_Size failed: {error}")

    def get_char_index(self, code):
        return int(_module()._ft_get_char_index(self._face, code))

    def load_glyph(self, index, flags):
        error = _module()._ft_load_glyph(self._face, index, flags)
        if error:
            raise RuntimeError(f"FT_Load_Glyph({index}) failed: {error}")
        # Snapshot the slot: the next load overwrites it C-side, and callers
        # hold `face.glyph` across loads the way freetype-py lets them.
        self.glyph = _GlyphSlot(self._face)
        return self.glyph

    @property
    def size(self):
        return _SizeMetrics(self._face)


def version():
    module = _module()
    module._ft_init()
    # UTF8ToString lives on the module when exported; the pointer alone is
    # enough for a version triple, read byte by byte to avoid depending on it.
    pointer = int(module._ft_version())
    raw = bytearray()
    heap = module.HEAPU8
    while heap[pointer + len(raw)] != 0:
        raw.append(heap[pointer + len(raw)])
    text = raw.decode()
    return tuple(int(part) for part in text.split("."))
