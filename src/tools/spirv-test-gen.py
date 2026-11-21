#!/usr/bin/env python3
"""Generate the SPIR-V modules used by the software Vulkan ICD regression.
The two shaders are intentionally small: they exercise location-decorated
inputs, gl_Position, a colour varying and a fragment output, which is the
subset the interpreter implements.

usage: python tools/spirv-test-gen.py [output-header]
"""

import struct
import sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "user/lib/vulkan/test_shaders.h"

OP_CAPABILITY = 17
OP_MEMORY_MODEL = 14
OP_ENTRY_POINT = 15
OP_EXECUTION_MODE = 16
OP_DECORATE = 71
OP_TYPE_VOID = 19
OP_TYPE_FLOAT = 22
OP_TYPE_VECTOR = 23
OP_TYPE_POINTER = 32
OP_TYPE_FUNCTION = 33
OP_CONSTANT = 43
OP_FUNCTION = 54
OP_FUNCTION_END = 56
OP_LABEL = 248
OP_VARIABLE = 59
OP_LOAD = 61
OP_STORE = 62
OP_COMPOSITE_CONSTRUCT = 80
OP_RETURN = 253

STORAGE_INPUT = 1
STORAGE_OUTPUT = 3
DECORATION_BUILTIN = 11
DECORATION_LOCATION = 30
BUILTIN_POSITION = 0
EXECUTION_VERTEX = 0
EXECUTION_FRAGMENT = 4
EXECUTION_ORIGIN_UPPER_LEFT = 7


def inst(opcode, *args):
    """Encode one instruction: the first word carries the word count and opcode."""
    return [(1 + len(args)) << 16 | opcode, *args]


def spv_string(text):
    """Encode a NUL-terminated SPIR-V string, padded to 32-bit words."""
    data = bytearray(text.encode("utf-8"))
    data.append(0)
    while len(data) % 4 != 0:
        data.append(0)
    return [
        data[i] | data[i + 1] << 8 | data[i + 2] << 16 | data[i + 3] << 24
        for i in range(0, len(data), 4)
    ]


def float_bits(value):
    """Return the little-endian IEEE-754 binary32 bit pattern of a float."""
    return struct.unpack("<I", struct.pack("<f", value))[0]


def vertex_module():
    """Build the vertex shader word stream."""
    words = [0x07230203, 0x00010000, 0, 23, 0]
    words += inst(OP_CAPABILITY, 0)             # Shader
    words += inst(OP_MEMORY_MODEL, 0, 1)        # Logical, GLSL450
    words += inst(OP_ENTRY_POINT, EXECUTION_VERTEX, 11,
                  *spv_string("main"), 12, 13, 14, 15)
    words += inst(OP_DECORATE, 12, DECORATION_LOCATION, 0)
    words += inst(OP_DECORATE, 13, DECORATION_LOCATION, 1)
    words += inst(OP_DECORATE, 14, DECORATION_LOCATION, 0)
    words += inst(OP_DECORATE, 15, DECORATION_BUILTIN, BUILTIN_POSITION)
    words += inst(OP_TYPE_VOID, 1)
    words += inst(OP_TYPE_FUNCTION, 2, 1)
    words += inst(OP_TYPE_FLOAT, 3, 32)
    words += inst(OP_TYPE_VECTOR, 4, 3, 2)
    words += inst(OP_TYPE_VECTOR, 5, 3, 3)
    words += inst(OP_TYPE_VECTOR, 6, 3, 4)
    words += inst(OP_TYPE_POINTER, 7, STORAGE_INPUT, 4)
    words += inst(OP_TYPE_POINTER, 8, STORAGE_INPUT, 5)
    words += inst(OP_TYPE_POINTER, 9, STORAGE_OUTPUT, 5)
    words += inst(OP_TYPE_POINTER, 10, STORAGE_OUTPUT, 6)
    words += inst(OP_CONSTANT, 3, 16, float_bits(0.0))
    words += inst(OP_CONSTANT, 3, 17, float_bits(1.0))
    words += inst(OP_VARIABLE, 7, 12, STORAGE_INPUT)
    words += inst(OP_VARIABLE, 8, 13, STORAGE_INPUT)
    words += inst(OP_VARIABLE, 9, 14, STORAGE_OUTPUT)
    words += inst(OP_VARIABLE, 10, 15, STORAGE_OUTPUT)
    words += inst(OP_FUNCTION, 1, 11, 0, 2)
    words += inst(OP_LABEL, 22)
    words += inst(OP_LOAD, 4, 18, 12)
    words += inst(OP_LOAD, 5, 19, 13)
    words += inst(OP_COMPOSITE_CONSTRUCT, 6, 20, 18, 16, 17)
    words += inst(OP_STORE, 15, 20)
    words += inst(OP_STORE, 14, 19)
    words += inst(OP_RETURN)
    words += inst(OP_FUNCTION_END)
    return words


def fragment_module():
    """Build the fragment shader word stream."""
    words = [0x07230203, 0x00010000, 0, 15, 0]
    words += inst(OP_CAPABILITY, 0)
    words += inst(OP_MEMORY_MODEL, 0, 1)
    words += inst(OP_ENTRY_POINT, EXECUTION_FRAGMENT, 8,
                  *spv_string("main"), 9, 10)
    words += inst(OP_EXECUTION_MODE, 8, EXECUTION_ORIGIN_UPPER_LEFT)
    words += inst(OP_DECORATE, 9, DECORATION_LOCATION, 0)
    words += inst(OP_DECORATE, 10, DECORATION_LOCATION, 0)
    words += inst(OP_TYPE_VOID, 1)
    words += inst(OP_TYPE_FUNCTION, 2, 1)
    words += inst(OP_TYPE_FLOAT, 3, 32)
    words += inst(OP_TYPE_VECTOR, 4, 3, 3)
    words += inst(OP_TYPE_VECTOR, 5, 3, 4)
    words += inst(OP_TYPE_POINTER, 6, STORAGE_INPUT, 4)
    words += inst(OP_TYPE_POINTER, 7, STORAGE_OUTPUT, 5)
    words += inst(OP_CONSTANT, 3, 14, float_bits(1.0))
    words += inst(OP_VARIABLE, 6, 9, STORAGE_INPUT)
    words += inst(OP_VARIABLE, 7, 10, STORAGE_OUTPUT)
    words += inst(OP_FUNCTION, 1, 8, 0, 2)
    words += inst(OP_LABEL, 11)
    words += inst(OP_LOAD, 4, 12, 9)
    words += inst(OP_COMPOSITE_CONSTRUCT, 5, 13, 12, 14)
    words += inst(OP_STORE, 10, 13)
    words += inst(OP_RETURN)
    words += inst(OP_FUNCTION_END)
    return words


def emit_array(name, words):
    """Render one C array plus its word count."""
    body = []
    for start in range(0, len(words), 8):
        body.append("    " + ", ".join("0x%08xu" % word for word in words[start:start + 8]))
    return (
        "static const unsigned int %s[] = {\n%s\n};\n"
        "static const unsigned int %s_count = %d;\n"
        % (name, ",\n".join(body), name, len(words))
    )


def main():
    """Write the shader header and report the module sizes."""
    header = (
        "/* Generated by tools/spirv-test-gen.rb -- do not edit by hand. */\n"
        "#ifndef ZHAOXU_VULKAN_TEST_SHADERS_H\n"
        "#define ZHAOXU_VULKAN_TEST_SHADERS_H\n"
        "\n"
    )
    header += emit_array("vk_test_vertex_spirv", vertex_module())
    header += "\n"
    header += emit_array("vk_test_fragment_spirv", fragment_module())
    header += "\n#endif\n"
    with open(OUT, "w") as handle:
        handle.write(header)
    print("wrote %s (%d + %d words)" % (OUT, len(vertex_module()), len(fragment_module())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
