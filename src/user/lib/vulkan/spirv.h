#ifndef ZHAOXU_SPIRV_H
#define ZHAOXU_SPIRV_H

/* Zhaoxu OS - a small SPIR-V 1.0 interpreter for the software Vulkan ICD.
 *
 * This is deliberately a subset: one entry point per module, straight-line
 * code, float/vector arithmetic, composite construction/extraction, and
 * location/builtin-decorated globals.  Branches, loops, images, samplers,
 * atomics and storage buffers are rejected, not silently mis-executed. */

#define SPV_MAX_IDS 256
#define SPV_MAX_LOCATIONS 8
#define SPV_MAX_INTERFACES 32

struct spv_type {
    int kind;
    int components;
    int element;
    int storage;
};

struct spv_value {
    int components;
    float f[4];
};

struct spv_var {
    int valid;
    int type;
    int storage;
    int location;
    int builtin;
    struct spv_value value;
};

struct spv_module {
    const unsigned int *words;
    unsigned int word_count;
    int stage;                 /* 0 = vertex, 1 = fragment */
    int bound;
    struct spv_type types[SPV_MAX_IDS];
    struct spv_value constants[SPV_MAX_IDS];
    int constant_valid[SPV_MAX_IDS];
    struct spv_var vars[SPV_MAX_IDS];
    int decorated_location[SPV_MAX_IDS];
    int decorated_builtin[SPV_MAX_IDS];
    int entry_id;
    int interfaces[SPV_MAX_INTERFACES];
    int interface_count;
    const unsigned int *function_start;
    const unsigned int *function_end;
};

struct spv_attribute {
    int location;
    int components;
    const float *data;
};

struct spv_vertex_result {
    float position[4];
    float varyings[SPV_MAX_LOCATIONS][4];
    int varying_components[SPV_MAX_LOCATIONS];
};

/* Parse and validate the subset used by the ICD; returns 0 on success. */
int spv_parse(struct spv_module *module, const unsigned int *words,
              unsigned int word_count, int stage);

/* Execute the vertex entry with the supplied location attributes. */
int spv_run_vertex(struct spv_module *module,
                   const struct spv_attribute *attributes, int attribute_count,
                   struct spv_vertex_result *result);

/* Execute the fragment entry with interpolated location varyings. */
int spv_run_fragment(struct spv_module *module,
                     const float varyings[SPV_MAX_LOCATIONS][4],
                     const int components[SPV_MAX_LOCATIONS],
                     float out_color[4]);

#endif
