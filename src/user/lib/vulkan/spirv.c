/*
 * Zhaoxu OS - SPIR-V 1.0 interpreter used by the software Vulkan ICD.
 *
 * The interpreter executes straight-line float/vector shaders.  It resolves
 * location-decorated interface variables, gl_Position and a fragment colour
 * output.  Unsupported instructions return an error instead of producing a
 * plausible-looking wrong image.
 */
#include "spirv.h"

#include <string.h>

#ifdef SPV_DEBUG
#include <stdio.h>
#define SPV_TRACE(...) fprintf(stderr, __VA_ARGS__)
#else
#define SPV_TRACE(...)
#endif

#define SPV_MAGIC 0x07230203u

#define OP_NAME 5
#define OP_EXT_INST_IMPORT 11
#define OP_MEMORY_MODEL 14
#define OP_ENTRY_POINT 15
#define OP_EXECUTION_MODE 16
#define OP_CAPABILITY 17
#define OP_TYPE_VOID 19
#define OP_TYPE_FLOAT 22
#define OP_TYPE_VECTOR 23
#define OP_TYPE_POINTER 32
#define OP_TYPE_FUNCTION 33
#define OP_CONSTANT 43
#define OP_CONSTANT_COMPOSITE 44
#define OP_FUNCTION 54
#define OP_FUNCTION_PARAMETER 55
#define OP_FUNCTION_END 56
#define OP_VARIABLE 59
#define OP_LOAD 61
#define OP_STORE 62
#define OP_DECORATE 71
#define OP_COMPOSITE_CONSTRUCT 80
#define OP_COMPOSITE_EXTRACT 81
#define OP_UNDEF 1
#define OP_LABEL 248
#define OP_RETURN 253
#define OP_FADD 129
#define OP_FSUB 131
#define OP_FMUL 133
#define OP_FDIV 136
#define OP_DOT 148
#define OP_VECTOR_TIMES_SCALAR 142

#define TYPE_UNKNOWN 0
#define TYPE_VOID 1
#define TYPE_FLOAT 2
#define TYPE_VECTOR 3
#define TYPE_POINTER 4
#define TYPE_FUNCTION 5

#define STORAGE_INPUT 1
#define STORAGE_OUTPUT 3

#define DECORATION_BUILTIN 11
#define DECORATION_LOCATION 30

enum {
    EXEC_OK = 0,
    EXEC_UNSUPPORTED = -1
};

struct spv_exec {
    struct spv_module *module;
    struct spv_value values[SPV_MAX_IDS];
    int value_valid[SPV_MAX_IDS];
};

/* Copy a value and mark it defined. */
static void set_value(struct spv_exec *exec, unsigned int id,
                      const struct spv_value *value)
{
    if (id >= SPV_MAX_IDS)
        return;
    exec->values[id] = *value;
    exec->value_valid[id] = 1;
}

/* Fetch a result value or a module constant. */
static const struct spv_value *get_value(struct spv_exec *exec,
                                         unsigned int id)
{
    if (id < SPV_MAX_IDS && exec->value_valid[id])
        return &exec->values[id];
    if (id < SPV_MAX_IDS && exec->module->constant_valid[id])
        return &exec->module->constants[id];
    return 0;
}

/* Return the component count of a type id. */
static int type_components(struct spv_module *module, unsigned int type)
{
    if (type < SPV_MAX_IDS && module->types[type].kind == TYPE_VECTOR)
        return module->types[type].components;
    if (type < SPV_MAX_IDS && module->types[type].kind == TYPE_FLOAT)
        return 1;
    return 0;
}

/* Flatten one constituent into the destination vector. */
static int append_components(struct spv_value *destination, int at,
                             const struct spv_value *source)
{
    for (int i = 0; i < source->components && at < 4; i++)
        destination->f[at++] = source->f[i];
    return at;
}

/* Decode the module's types, constants, variables and entry point. */
int spv_parse(struct spv_module *module, const unsigned int *words,
              unsigned int word_count, int stage)
{
    if (!module || !words || word_count < 5 || words[0] != SPV_MAGIC)
        return -1;
    if ((words[1] >> 16) != 1)
        return -1;
    memset(module, 0, sizeof(*module));
    for (int i = 0; i < SPV_MAX_IDS; i++) {
        module->decorated_location[i] = -1;
        module->decorated_builtin[i] = -1;
    }
    module->words = words;
    module->word_count = word_count;
    module->stage = stage;
    module->bound = (int)words[3];
    if (module->bound <= 0 || module->bound > SPV_MAX_IDS)
        return -1;

    unsigned int offset = 5;
    while (offset < word_count) {
        unsigned int first = words[offset];
        unsigned int count = first >> 16;
        unsigned int opcode = first & 0xFFFF;
        if (count == 0 || offset + count > word_count)
            return -1;
        const unsigned int *operands = &words[offset + 1];

        switch (opcode) {
        case OP_ENTRY_POINT: {
            unsigned int model = operands[0];
            module->entry_id = (int)operands[1];
            int at = 2;
            /* The name is a NUL-terminated word string. */
            while (at < (int)count - 1) {
                unsigned int word = operands[at++];
                if ((word & 0xFF) == 0 || (word & 0xFF00) == 0 ||
                    (word & 0xFF0000) == 0 || (word & 0xFF000000) == 0)
                    break;
            }
            module->interface_count = 0;
            for (int i = at; i < (int)count - 1; i++) {
                if (module->interface_count < SPV_MAX_INTERFACES)
                    module->interfaces[module->interface_count++] =
                        (int)operands[i];
            }
            if ((stage == 0 && model != 0) || (stage == 1 && model != 4))
                return -1;
            break;
        }
        case OP_DECORATE: {
            unsigned int id = operands[0];
            unsigned int decoration = operands[1];
            if (id >= SPV_MAX_IDS)
                break;
            if (decoration == DECORATION_LOCATION)
                module->decorated_location[id] = (int)operands[2];
            else if (decoration == DECORATION_BUILTIN)
                module->decorated_builtin[id] = (int)operands[2];
            break;
        }
        case OP_TYPE_VOID:
            if (operands[0] < SPV_MAX_IDS)
                module->types[operands[0]].kind = TYPE_VOID;
            break;
        case OP_TYPE_FLOAT:
            if (operands[0] < SPV_MAX_IDS &&
                operands[0] < SPV_MAX_IDS) {
                module->types[operands[0]].kind = TYPE_FLOAT;
                module->types[operands[0]].components = 1;
            }
            break;
        case OP_TYPE_VECTOR:
            if (operands[0] < SPV_MAX_IDS) {
                module->types[operands[0]].kind = TYPE_VECTOR;
                module->types[operands[0]].components = (int)operands[2];
                module->types[operands[0]].element = (int)operands[1];
            }
            break;
        case OP_TYPE_POINTER:
            if (operands[0] < SPV_MAX_IDS) {
                module->types[operands[0]].kind = TYPE_POINTER;
                module->types[operands[0]].storage = (int)operands[1];
                module->types[operands[0]].element = (int)operands[2];
            }
            break;
        case OP_TYPE_FUNCTION:
            if (operands[0] < SPV_MAX_IDS)
                module->types[operands[0]].kind = TYPE_FUNCTION;
            break;
        case OP_CONSTANT: {
            unsigned int type = operands[0];
            unsigned int id = operands[1];
            if (id >= SPV_MAX_IDS)
                break;
            struct spv_value *value = &module->constants[id];
            value->components = type_components(module, type);
            if (value->components < 1)
                value->components = 1;
            if (value->components == 1) {
                union {
                    unsigned int bits;
                    float number;
                } convert;
                convert.bits = operands[2];
                value->f[0] = convert.number;
            }
            module->constant_valid[id] = 1;
            break;
        }
        case OP_CONSTANT_COMPOSITE: {
            unsigned int type = operands[0];
            unsigned int id = operands[1];
            if (id >= SPV_MAX_IDS)
                break;
            struct spv_value *value = &module->constants[id];
            memset(value, 0, sizeof(*value));
            value->components = type_components(module, type);
            int at = 0;
            for (unsigned int i = 2; i < count - 1; i++) {
                if (operands[i] < SPV_MAX_IDS &&
                    module->constant_valid[operands[i]])
                    at = append_components(
                        value, at, &module->constants[operands[i]]);
            }
            if (value->components == 0)
                value->components = at;
            module->constant_valid[id] = 1;
            break;
        }
        case OP_VARIABLE: {
            unsigned int id = operands[1];
            if (id >= SPV_MAX_IDS)
                return -1;
            module->vars[id].valid = 1;
            module->vars[id].type = (int)operands[0];
            module->vars[id].storage = (int)operands[2];
            break;
        }
        case OP_FUNCTION: {
            unsigned int function_id = operands[1];
            if ((int)function_id != module->entry_id)
                break;
            const unsigned int *cursor = &words[offset + count];
            while (cursor < words + word_count) {
                unsigned int inner = *cursor++;
                unsigned int inner_count = inner >> 16;
                unsigned int inner_op = inner & 0xFFFF;
                if (inner_op == OP_LABEL) {
                    module->function_start = cursor + (inner_count - 1);
                    break;
                }
                if (inner_op == OP_FUNCTION_END || inner_count == 0)
                    break;
                cursor += inner_count - 1;
            }
            break;
        }
        case OP_FUNCTION_END:
            if (module->function_start && !module->function_end)
                module->function_end = &words[offset];
            break;
        default:
            break;
        }
        offset += count;
    }

    if (!module->function_start)
        return -1;
    if (!module->function_end)
        module->function_end = words + word_count;

    /* Variables are declared after decorations in some producers; apply the
     * decorations once both passes are complete. */
    for (int i = 0; i < SPV_MAX_IDS; i++) {
        if (module->vars[i].valid) {
            module->vars[i].location = module->decorated_location[i];
            module->vars[i].builtin = module->decorated_builtin[i];
        }
    }
    return 0;
}

/* Elementwise binary float operation with scalar broadcast. */
static int float_binary(struct spv_exec *exec, unsigned int result_id,
                        unsigned int type, unsigned int left_id,
                        unsigned int right_id, int opcode)
{
    const struct spv_value *left = get_value(exec, left_id);
    const struct spv_value *right = get_value(exec, right_id);
    if (!left || !right)
        return EXEC_UNSUPPORTED;
    int components = type_components(exec->module, type);
    if (components <= 0)
        return EXEC_UNSUPPORTED;
    struct spv_value result;
    memset(&result, 0, sizeof(result));
    result.components = components;
    for (int i = 0; i < components; i++) {
        float a = left->f[i < left->components ? i : 0];
        float b = right->f[i < right->components ? i : 0];
        switch (opcode) {
        case OP_FADD:
            result.f[i] = a + b;
            break;
        case OP_FSUB:
            result.f[i] = a - b;
            break;
        case OP_FMUL:
            result.f[i] = a * b;
            break;
        case OP_FDIV:
            result.f[i] = b == 0 ? 0 : a / b;
            break;
        default:
            return EXEC_UNSUPPORTED;
        }
    }
    set_value(exec, result_id, &result);
    return EXEC_OK;
}

/* Run the module's main function over the prepared global variables. */
static int execute(struct spv_exec *exec)
{
    SPV_TRACE("execute enter start=%p end=%p\n",
              (const void *)exec->module->function_start,
              (const void *)exec->module->function_end);
    struct spv_module *module = exec->module;
    const unsigned int *cursor = module->function_start;
    SPV_TRACE("delta %ld first %08x\n",
              (long)(module->function_end - module->function_start),
              module->function_start ? *module->function_start : 0);
    while (cursor < module->function_end) {
        unsigned int first = *cursor++;
        unsigned int count = first >> 16;
        unsigned int opcode = first & 0xFFFF;
        if (count == 0)
            return EXEC_UNSUPPORTED;
        const unsigned int *operands = cursor;
        cursor += count - 1;
        SPV_TRACE("spv op %u count %u\n", opcode, count);

        switch (opcode) {
        case OP_LABEL:
            break;
        case OP_RETURN:
            return EXEC_OK;
        case OP_UNDEF: {
            struct spv_value value;
            memset(&value, 0, sizeof(value));
            value.components = type_components(module, operands[0]);
            set_value(exec, operands[1], &value);
            break;
        }
        case OP_LOAD: {
            unsigned int pointer = operands[2];
            if (pointer < SPV_MAX_IDS && module->vars[pointer].valid) {
                const struct spv_var *var = &module->vars[pointer];
                const struct spv_value *value =
                    exec->value_valid[pointer]
                        ? &exec->values[pointer]
                        : &var->value;
                set_value(exec, operands[1], value);
            } else {
                const struct spv_value *value = get_value(exec, pointer);
                if (!value)
                    return EXEC_UNSUPPORTED;
                set_value(exec, operands[1], value);
            }
            break;
        }
        case OP_STORE: {
            unsigned int pointer = operands[0];
            const struct spv_value *value = get_value(exec, operands[1]);
            if (!value || pointer >= SPV_MAX_IDS ||
                !module->vars[pointer].valid)
                return EXEC_UNSUPPORTED;
            module->vars[pointer].value = *value;
            exec->values[pointer] = *value;
            exec->value_valid[pointer] = 1;
            break;
        }
        case OP_COMPOSITE_CONSTRUCT: {
            struct spv_value result;
            memset(&result, 0, sizeof(result));
            result.components = type_components(module, operands[0]);
            int at = 0;
            for (unsigned int i = 2; i < count - 1; i++) {
                const struct spv_value *value = get_value(exec, operands[i]);
                if (!value)
                    return EXEC_UNSUPPORTED;
                at = append_components(&result, at, value);
            }
            if (result.components == 0)
                result.components = at;
            set_value(exec, operands[1], &result);
            break;
        }
        case OP_COMPOSITE_EXTRACT: {
            const struct spv_value *source = get_value(exec, operands[2]);
            if (!source || count < 5)
                return EXEC_UNSUPPORTED;
            unsigned int index = operands[3];
            struct spv_value result;
            memset(&result, 0, sizeof(result));
            result.components = type_components(module, operands[0]);
            if (result.components < 1)
                result.components = 1;
            if (index < (unsigned int)source->components)
                result.f[0] = source->f[index];
            set_value(exec, operands[1], &result);
            break;
        }
        case OP_FADD:
        case OP_FSUB:
        case OP_FMUL:
        case OP_FDIV:
            if (float_binary(exec, operands[1], operands[0], operands[2],
                             operands[3], (int)opcode) != EXEC_OK)
                return EXEC_UNSUPPORTED;
            break;
        case OP_VECTOR_TIMES_SCALAR: {
            const struct spv_value *vector = get_value(exec, operands[2]);
            const struct spv_value *scalar = get_value(exec, operands[3]);
            if (!vector || !scalar)
                return EXEC_UNSUPPORTED;
            struct spv_value result = *vector;
            for (int i = 0; i < result.components; i++)
                result.f[i] *= scalar->f[0];
            set_value(exec, operands[1], &result);
            break;
        }
        case OP_DOT: {
            const struct spv_value *left = get_value(exec, operands[2]);
            const struct spv_value *right = get_value(exec, operands[3]);
            if (!left || !right)
                return EXEC_UNSUPPORTED;
            float sum = 0;
            for (int i = 0; i < left->components; i++)
                sum += left->f[i] * right->f[i];
            struct spv_value result;
            memset(&result, 0, sizeof(result));
            result.components = 1;
            result.f[0] = sum;
            set_value(exec, operands[1], &result);
            break;
        }
        default:
            SPV_TRACE("spv unsupported op %u\n", opcode);
            return EXEC_UNSUPPORTED;
        }
    }
    return EXEC_UNSUPPORTED;
}

/* Return the component count behind a variable's pointer type. */
static int variable_components(struct spv_module *module,
                               const struct spv_var *var)
{
    int type = var->type;
    if (type >= 0 && type < SPV_MAX_IDS &&
        module->types[type].kind == TYPE_POINTER)
        type = module->types[type].element;
    int components = type_components(module, (unsigned int)type);
    return components > 0 ? components : 1;
}

/* Fill a variable from a location attribute. */
static void fill_input(struct spv_exec *exec, unsigned int id,
                       const struct spv_attribute *attribute)
{
    struct spv_var *var = &exec->module->vars[id];
    memset(&var->value, 0, sizeof(var->value));
    var->value.components = variable_components(exec->module, var);
    for (int i = 0; i < var->value.components && i < attribute->components;
         i++)
        var->value.f[i] = attribute->data[i];
    exec->values[id] = var->value;
    exec->value_valid[id] = 1;
}

/* Execute the vertex entry and read back gl_Position / varyings. */
int spv_run_vertex(struct spv_module *module,
                   const struct spv_attribute *attributes, int attribute_count,
                   struct spv_vertex_result *result)
{
    if (!module || module->stage != 0 || !result)
        return -1;
    struct spv_exec exec;
    memset(&exec, 0, sizeof(exec));
    exec.module = module;

    for (int i = 0; i < module->interface_count; i++) {
        int id = module->interfaces[i];
        if (id < 0 || id >= SPV_MAX_IDS)
            continue;
        struct spv_var *var = &module->vars[id];
        if (!var->valid || var->storage != STORAGE_INPUT)
            continue;
        for (int a = 0; a < attribute_count; a++) {
            if (attributes[a].location == var->location) {
                fill_input(&exec, (unsigned int)id, &attributes[a]);
                break;
            }
        }
    }

    if (execute(&exec) != EXEC_OK)
        return -1;

    memset(result, 0, sizeof(*result));
    for (int i = 0; i < module->interface_count; i++) {
        int id = module->interfaces[i];
        if (id < 0 || id >= SPV_MAX_IDS)
            continue;
        struct spv_var *var = &module->vars[id];
        if (!var->valid || var->storage != STORAGE_OUTPUT)
            continue;
        const struct spv_value *value =
            exec.value_valid[id] ? &exec.values[id] : &var->value;
        if (var->builtin == 0) {
            for (int k = 0; k < 4; k++)
                result->position[k] =
                    k < value->components ? value->f[k] : 0;
        } else if (var->location >= 0 &&
                   var->location < SPV_MAX_LOCATIONS) {
            for (int k = 0; k < 4; k++)
                result->varyings[var->location][k] =
                    k < value->components ? value->f[k] : 0;
            result->varying_components[var->location] = value->components;
        }
    }
    return 0;
}

/* Execute the fragment entry and return its location-0 output colour. */
int spv_run_fragment(struct spv_module *module,
                     const float varyings[SPV_MAX_LOCATIONS][4],
                     const int components[SPV_MAX_LOCATIONS],
                     float out_color[4])
{
    if (!module || module->stage != 1 || !out_color)
        return -1;
    struct spv_exec exec;
    memset(&exec, 0, sizeof(exec));
    exec.module = module;

    for (int i = 0; i < module->interface_count; i++) {
        int id = module->interfaces[i];
        if (id < 0 || id >= SPV_MAX_IDS)
            continue;
        struct spv_var *var = &module->vars[id];
        if (!var->valid || var->storage != STORAGE_INPUT ||
            var->location < 0 || var->location >= SPV_MAX_LOCATIONS)
            continue;
        memset(&var->value, 0, sizeof(var->value));
        var->value.components = variable_components(module, var);
        int source_components = components[var->location];
        for (int k = 0; k < var->value.components; k++)
            var->value.f[k] = k < source_components
                                  ? varyings[var->location][k]
                                  : 0;
        exec.values[id] = var->value;
        exec.value_valid[id] = 1;
    }

    if (execute(&exec) != EXEC_OK)
        return -1;

    int found = 0;
    for (int i = 0; i < module->interface_count; i++) {
        int id = module->interfaces[i];
        if (id < 0 || id >= SPV_MAX_IDS)
            continue;
        struct spv_var *var = &module->vars[id];
        if (!var->valid || var->storage != STORAGE_OUTPUT ||
            var->location != 0)
            continue;
        const struct spv_value *value =
            exec.value_valid[id] ? &exec.values[id] : &var->value;
        for (int k = 0; k < 4; k++)
            out_color[k] = k < value->components ? value->f[k] : 0;
        found = 1;
    }
    return found ? 0 : -1;
}
