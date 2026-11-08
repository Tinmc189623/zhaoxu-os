/*
 * Zhaoxu OS - zcc, the in-OS compiler for the Zhaoxu C subset.
 *
 * Compiles a small C-like language straight to x86-64 machine code and
 * packages it as a .zxe v1 image, so applications can be written and run
 * on the machine itself with no external toolchain.
 *
 * Supported language:
 *   int main() { ... }
 *   int a = expr;  a = expr;
 *   if (expr) stmt [else stmt]
 *   while (expr) stmt
 *   print("literal"); printnum(expr);
 *   return expr;
 * Expressions: + - * / % comparisons, parentheses, int variables/literals.
 */

extern "C" {
#include <kprintf.h>
#include <string.h>
#include <types.h>
}

namespace {

constexpr u64 kTextBase = 0x10000000000ULL;   /* 1 TiB: above the identity map */
constexpr u64 kDataBase = 0x10000001000ULL;
constexpr u64 kHeaderSize = 112;   /* align16(32 + 40 * 2) */
constexpr int kMaxCode = 32768;
constexpr int kMaxData = 8192;
constexpr int kMaxVars = 96;
constexpr int kMaxImage = 49152;
constexpr int kFrameSize = 1024;
constexpr int kScratchDisp = -(kFrameSize - 15); /* rbp-1009 */

u8 g_code[kMaxCode];
u8 g_data[kMaxData];
int g_code_len;
int g_data_len;
u8 g_image[kMaxImage];
char g_error[160];
bool g_failed;

/* ---- lexer ---------------------------------------------------------- */

enum Tok {
    T_EOF,
    T_IDENT,
    T_NUM,
    T_STR,
    T_SYM,
};

struct Lexer {
    const char *p;
    int line;
    int tok;
    i32 num;
    char text[512];
    int text_len;
};

Lexer g_lx;

void fail(const char *message)
{
    if (g_failed)
        return;
    g_failed = true;
    ksnprintf(g_error, sizeof(g_error), "line %d: %s", g_lx.line, message);
}

void lex_next(void)
{
    for (;;) {
        while (*g_lx.p == ' ' || *g_lx.p == '\t' || *g_lx.p == '\r' ||
               *g_lx.p == '\n') {
            if (*g_lx.p == '\n')
                g_lx.line++;
            g_lx.p++;
        }
        if (g_lx.p[0] == '/' && g_lx.p[1] == '/') {
            while (*g_lx.p && *g_lx.p != '\n')
                g_lx.p++;
            continue;
        }
        if (g_lx.p[0] == '/' && g_lx.p[1] == '*') {
            g_lx.p += 2;
            while (*g_lx.p && !(g_lx.p[0] == '*' && g_lx.p[1] == '/')) {
                if (*g_lx.p == '\n')
                    g_lx.line++;
                g_lx.p++;
            }
            if (*g_lx.p)
                g_lx.p += 2;
            continue;
        }
        break;
    }

    g_lx.text_len = 0;
    char c = *g_lx.p;
    if (!c) {
        g_lx.tok = T_EOF;
        return;
    }

    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
        int n = 0;
        while ((*g_lx.p >= 'a' && *g_lx.p <= 'z') ||
               (*g_lx.p >= 'A' && *g_lx.p <= 'Z') ||
               (*g_lx.p >= '0' && *g_lx.p <= '9') || *g_lx.p == '_') {
            if (n < (int)sizeof(g_lx.text) - 1)
                g_lx.text[n++] = *g_lx.p;
            g_lx.p++;
        }
        g_lx.text[n] = 0;
        g_lx.text_len = n;
        g_lx.tok = T_IDENT;
        return;
    }

    if (c >= '0' && c <= '9') {
        i64 value = 0;
        while (*g_lx.p >= '0' && *g_lx.p <= '9') {
            value = value * 10 + (*g_lx.p - '0');
            if (value > 0x7FFFFFFFLL) {
                fail("integer literal out of range");
                g_lx.tok = T_EOF;
                return;
            }
            g_lx.p++;
        }
        g_lx.num = (i32)value;
        g_lx.tok = T_NUM;
        return;
    }

    if (c == '"') {
        g_lx.p++;
        int n = 0;
        while (*g_lx.p && *g_lx.p != '"') {
            char ch = *g_lx.p++;
            if (ch == '\\') {
                char e = *g_lx.p++;
                switch (e) {
                case 'n': ch = '\n'; break;
                case 't': ch = '\t'; break;
                case 'r': ch = '\r'; break;
                case '\\': ch = '\\'; break;
                case '"': ch = '"'; break;
                case '0': ch = 0; break;
                default:
                    fail("unknown escape in string literal");
                    g_lx.tok = T_EOF;
                    return;
                }
            }
            if (n < (int)sizeof(g_lx.text) - 1)
                g_lx.text[n++] = ch;
        }
        if (*g_lx.p != '"') {
            fail("unterminated string literal");
            g_lx.tok = T_EOF;
            return;
        }
        g_lx.p++;
        g_lx.text[n] = 0;
        g_lx.text_len = n;
        g_lx.tok = T_STR;
        return;
    }

    static const char *kTwo[] = { "==", "!=", "<=", ">=" };
    for (int i = 0; i < 4; i++) {
        if (g_lx.p[0] == kTwo[i][0] && g_lx.p[1] == kTwo[i][1]) {
            g_lx.text[0] = kTwo[i][0];
            g_lx.text[1] = kTwo[i][1];
            g_lx.text[2] = 0;
            g_lx.text_len = 2;
            g_lx.p += 2;
            g_lx.tok = T_SYM;
            return;
        }
    }
    if (c == '(' || c == ')' || c == '{' || c == '}' || c == ';' ||
        c == ',' || c == '=' || c == '+' || c == '-' || c == '*' ||
        c == '/' || c == '%' || c == '<' || c == '>') {
        g_lx.text[0] = c;
        g_lx.text[1] = 0;
        g_lx.text_len = 1;
        g_lx.p++;
        g_lx.tok = T_SYM;
        return;
    }

    fail("unexpected character");
    g_lx.tok = T_EOF;
}

bool sym_is(const char *s)
{
    return g_lx.tok == T_SYM && strcmp(g_lx.text, s) == 0;
}

bool ident_is(const char *s)
{
    return g_lx.tok == T_IDENT && strcmp(g_lx.text, s) == 0;
}

void expect_sym(const char *s)
{
    if (!sym_is(s)) {
        char message[96];
        ksnprintf(message, sizeof(message), "expected '%s'", s);
        fail(message);
        return;
    }
    lex_next();
}

/* ---- code emitter ---------------------------------------------------- */

void emit8(u32 value)
{
    if (g_failed)
        return;
    if (g_code_len >= kMaxCode) {
        fail("program too large");
        return;
    }
    g_code[g_code_len++] = (u8)value;
}

void emit32(u32 value)
{
    emit8(value & 0xFF);
    emit8((value >> 8) & 0xFF);
    emit8((value >> 16) & 0xFF);
    emit8((value >> 24) & 0xFF);
}

int emit_jcc(u8 condition)
{
    emit8(0x0F);
    emit8(condition);
    int at = g_code_len;
    emit32(0);
    return at;
}

int emit_jmp()
{
    emit8(0xE9);
    int at = g_code_len;
    emit32(0);
    return at;
}

void patch_jump(int at)
{
    if (g_failed)
        return;
    i32 distance = (i32)(g_code_len - (at + 4));
    g_code[at + 0] = (u8)(distance & 0xFF);
    g_code[at + 1] = (u8)((distance >> 8) & 0xFF);
    g_code[at + 2] = (u8)((distance >> 16) & 0xFF);
    g_code[at + 3] = (u8)((distance >> 24) & 0xFF);
}

void emit_mov_eax(u32 value)
{
    emit8(0xB8);
    emit32(value);
}

void emit_mov_rax_imm(i32 value)
{
    emit8(0x48);
    emit8(0xC7);
    emit8(0xC0);
    emit32((u32)value);
}

/* load or store a variable slot at [rbp + disp] */
void emit_load_slot(int disp)
{
    emit8(0x48);
    emit8(0x8B);
    emit8(0x85);
    emit32((u32)disp);
}

void emit_store_slot(int disp)
{
    emit8(0x48);
    emit8(0x89);
    emit8(0x85);
    emit32((u32)disp);
}

void emit_lea_rsi_rip(int data_offset)
{
    i32 disp = (i32)((i64)kDataBase + data_offset -
                     ((i64)kTextBase + g_code_len + 7));
    emit8(0x48);
    emit8(0x8D);
    emit8(0x35);
    emit32((u32)disp);
}

int add_string(const char *s, int len)
{
    if (g_data_len + len > kMaxData) {
        fail("too many string literals");
        return 0;
    }
    int offset = g_data_len;
    for (int i = 0; i < len; i++)
        g_data[g_data_len++] = (u8)s[i];
    return offset;
}

/* ---- parser ---------------------------------------------------------- */

struct Var {
    char name[32];
    int disp;
};

Var g_vars[kMaxVars];
int g_var_count;

int find_var(const char *name)
{
    for (int i = 0; i < g_var_count; i++) {
        if (strcmp(g_vars[i].name, name) == 0)
            return g_vars[i].disp;
    }
    return 0;
}

bool add_var(const char *name, int *disp)
{
    if (g_var_count >= kMaxVars) {
        fail("too many variables");
        return false;
    }
    int slot = (g_var_count + 1) * 8;
    if (slot > kFrameSize - 32) {
        fail("too many variables");
        return false;
    }
    strncpy(g_vars[g_var_count].name, name, 31);
    g_vars[g_var_count].name[31] = 0;
    g_vars[g_var_count].disp = -slot;
    *disp = -slot;
    g_var_count++;
    return true;
}

void parse_expr(void);

void parse_primary(void)
{
    if (g_failed)
        return;
    if (g_lx.tok == T_NUM) {
        emit_mov_rax_imm(g_lx.num);
        lex_next();
        return;
    }
    if (g_lx.tok == T_IDENT) {
        int disp = find_var(g_lx.text);
        if (disp == 0) {
            char message[96];
            ksnprintf(message, sizeof(message), "unknown variable '%s'",
                      g_lx.text);
            fail(message);
            return;
        }
        emit_load_slot(disp);
        lex_next();
        return;
    }
    if (sym_is("(")) {
        lex_next();
        parse_expr();
        expect_sym(")");
        return;
    }
    if (sym_is("-")) {
        lex_next();
        parse_primary();
        emit8(0x48);
        emit8(0xF7);
        emit8(0xD8); /* neg rax */
        return;
    }
    fail("expected an expression");
}

void parse_term(void)
{
    parse_primary();
    while (!g_failed && (sym_is("*") || sym_is("/") || sym_is("%"))) {
        char op = g_lx.text[0];
        lex_next();
        emit8(0x50); /* push rax */
        parse_primary();
        emit8(0x48);
        emit8(0x89);
        emit8(0xC1); /* mov rcx, rax */
        emit8(0x58); /* pop rax */
        if (op == '*') {
            emit8(0x48);
            emit8(0x0F);
            emit8(0xAF);
            emit8(0xC1); /* imul rax, rcx */
        } else {
            emit8(0x48);
            emit8(0x99); /* cqo */
            emit8(0x48);
            emit8(0xF7);
            emit8(0xF9); /* idiv rcx */
            if (op == '%') {
                emit8(0x48);
                emit8(0x89);
                emit8(0xD0); /* mov rax, rdx */
            }
        }
    }
}

void parse_additive(void)
{
    parse_term();
    while (!g_failed && (sym_is("+") || sym_is("-"))) {
        char op = g_lx.text[0];
        lex_next();
        emit8(0x50);
        parse_term();
        emit8(0x48);
        emit8(0x89);
        emit8(0xC1);
        emit8(0x58);
        emit8(0x48);
        if (op == '+')
            emit8(0x01); /* add rax, rcx */
        else
            emit8(0x29); /* sub rax, rcx */
        emit8(0xC8);
    }
}

void emit_setcc(u8 cc)
{
    emit8(0x0F);
    emit8(cc);
    emit8(0xC0); /* setcc al */
    emit8(0x48);
    emit8(0x0F);
    emit8(0xB6);
    emit8(0xC0); /* movzx rax, al */
}

void parse_compare(void)
{
    parse_additive();
    if (g_failed || g_lx.tok != T_SYM)
        return;

    u8 cc = 0;
    if (strcmp(g_lx.text, "==") == 0)
        cc = 0x94;
    else if (strcmp(g_lx.text, "!=") == 0)
        cc = 0x95;
    else if (strcmp(g_lx.text, "<") == 0)
        cc = 0x9C;
    else if (strcmp(g_lx.text, ">") == 0)
        cc = 0x9F;
    else if (strcmp(g_lx.text, "<=") == 0)
        cc = 0x9E;
    else if (strcmp(g_lx.text, ">=") == 0)
        cc = 0x9D;
    else
        return;

    lex_next();
    emit8(0x50);
    parse_additive();
    emit8(0x48);
    emit8(0x89);
    emit8(0xC1); /* mov rcx, rax */
    emit8(0x58); /* pop rax */
    emit8(0x48);
    emit8(0x39);
    emit8(0xC8); /* cmp rax, rcx */
    emit_setcc(cc);
}

void parse_expr(void)
{
    parse_compare();
}

void emit_test_rax(void)
{
    emit8(0x48);
    emit8(0x85);
    emit8(0xC0);
}

void emit_exit(void)
{
    emit8(0x48);
    emit8(0x89);
    emit8(0xC7); /* mov rdi, rax */
    emit_mov_eax(0);
    emit8(0xCD);
    emit8(0x80);
    emit8(0xF4); /* hlt, in case exit returns */
    emit8(0xEB);
    emit8(0xFE); /* jmp $ */
}

void parse_statement(void);

void parse_block(void)
{
    expect_sym("{");
    while (!g_failed && !sym_is("}") && g_lx.tok != T_EOF)
        parse_statement();
    expect_sym("}");
}

void parse_print(void)
{
    expect_sym("(");
    if (g_lx.tok != T_STR) {
        fail("print() needs a string literal");
        return;
    }
    int offset = add_string(g_lx.text, g_lx.text_len);
    int len = g_lx.text_len;
    lex_next();
    expect_sym(")");
    expect_sym(";");
    if (g_failed)
        return;

    emit8(0xBF);
    emit32(1);            /* mov edi, 1 */
    emit_lea_rsi_rip(offset);
    emit8(0xBA);
    emit32((u32)len);
    emit_mov_eax(1);
    emit8(0xCD);
    emit8(0x80);
}

void parse_printnum(void)
{
    expect_sym("(");
    parse_expr();
    expect_sym(")");
    expect_sym(";");
    if (g_failed)
        return;

    /* sign */
    emit_test_rax();
    int jns_at = emit_jcc(0x89); /* jns */
    emit8(0x50);                  /* push rax */
    emit8(0xC6);
    emit8(0x85);
    emit32(0xFFFFFFFF);           /* mov byte [rbp-1], '-' */
    emit8('-');
    emit8(0xBF);
    emit32(1);
    emit8(0x48);
    emit8(0x8D);
    emit8(0xB5);
    emit32(0xFFFFFFFF);           /* lea rsi, [rbp-1] */
    emit8(0xBA);
    emit32(1);
    emit_mov_eax(1);
    emit8(0xCD);
    emit8(0x80);
    emit8(0x58);                  /* pop rax */
    emit8(0x48);
    emit8(0xF7);
    emit8(0xD8);                  /* neg rax */
    patch_jump(jns_at);

    /* decimal conversion over a scratch buffer at [rbp-1009..rbp-1000] */
    emit8(0x48);
    emit8(0xC7);
    emit8(0xC1);
    emit32(10);                   /* mov rcx, 10 */
    emit8(0x48);
    emit8(0x8D);
    emit8(0xB5);
    emit32((u32)kScratchDisp);    /* lea rsi, [rbp-1009] */
    int loop_at = g_code_len;
    emit8(0x31);
    emit8(0xD2);                  /* xor edx, edx */
    emit8(0x48);
    emit8(0xF7);
    emit8(0xF1);                  /* div rcx */
    emit8(0x80);
    emit8(0xC2);
    emit8('0');                   /* add dl, '0' */
    emit8(0x48);
    emit8(0xFF);
    emit8(0xCE);                  /* dec rsi */
    emit8(0x88);
    emit8(0x16);                  /* mov [rsi], dl */
    emit_test_rax();
    int jnz_at = emit_jcc(0x85); /* jnz loop */
    i32 back = (i32)(loop_at - (jnz_at + 4));
    g_code[jnz_at + 0] = (u8)(back & 0xFF);
    g_code[jnz_at + 1] = (u8)((back >> 8) & 0xFF);
    g_code[jnz_at + 2] = (u8)((back >> 16) & 0xFF);
    g_code[jnz_at + 3] = (u8)((back >> 24) & 0xFF);

    emit8(0x48);
    emit8(0x8D);
    emit8(0x85);
    emit32((u32)kScratchDisp);    /* lea rax, [rbp-1009] */
    emit8(0x48);
    emit8(0x29);
    emit8(0xF0);                  /* sub rax, rsi */
    emit8(0x48);
    emit8(0x89);
    emit8(0xC2);                  /* mov rdx, rax */
    emit8(0xBF);
    emit32(1);
    emit_mov_eax(1);
    emit8(0xCD);
    emit8(0x80);
}

void parse_if(void)
{
    expect_sym("(");
    parse_expr();
    expect_sym(")");
    emit_test_rax();
    int jz_at = emit_jcc(0x84);
    parse_statement();
    if (ident_is("else")) {
        lex_next();
        int jmp_at = emit_jmp();
        patch_jump(jz_at);
        parse_statement();
        patch_jump(jmp_at);
    } else {
        patch_jump(jz_at);
    }
}

void parse_while(void)
{
    int start = g_code_len;
    expect_sym("(");
    parse_expr();
    expect_sym(")");
    emit_test_rax();
    int jz_at = emit_jcc(0x84);
    parse_statement();
    emit8(0xE9);
    i32 back = (i32)(start - (g_code_len + 4));
    emit32((u32)back);
    patch_jump(jz_at);
}

void parse_return(void)
{
    if (sym_is(";")) {
        emit8(0x31);
        emit8(0xC0); /* xor eax, eax */
    } else {
        parse_expr();
    }
    expect_sym(";");
    emit_exit();
}

void parse_declaration(void)
{
    /* 'int' already consumed */
    if (g_lx.tok != T_IDENT) {
        fail("expected a variable name");
        return;
    }
    char name[32];
    strncpy(name, g_lx.text, 31);
    name[31] = 0;
    lex_next();

    int disp = 0;
    if (!add_var(name, &disp))
        return;

    if (sym_is("=")) {
        lex_next();
        parse_expr();
        emit_store_slot(disp);
    } else {
        emit8(0x31);
        emit8(0xC0); /* xor eax, eax */
        emit_store_slot(disp);
    }
    expect_sym(";");
}

void parse_assignment(void)
{
    int disp = find_var(g_lx.text);
    if (disp == 0) {
        char message[96];
        ksnprintf(message, sizeof(message), "unknown variable '%s'",
                  g_lx.text);
        fail(message);
        return;
    }
    lex_next();
    expect_sym("=");
    parse_expr();
    emit_store_slot(disp);
    expect_sym(";");
}

void parse_statement(void)
{
    if (g_failed)
        return;

    if (sym_is("{")) {
        parse_block();
        return;
    }
    if (sym_is(";")) {
        lex_next();
        return;
    }
    if (ident_is("int")) {
        lex_next();
        parse_declaration();
        return;
    }
    if (ident_is("if")) {
        lex_next();
        parse_if();
        return;
    }
    if (ident_is("while")) {
        lex_next();
        parse_while();
        return;
    }
    if (ident_is("return")) {
        lex_next();
        parse_return();
        return;
    }
    if (ident_is("print")) {
        lex_next();
        parse_print();
        return;
    }
    if (ident_is("printnum")) {
        lex_next();
        parse_printnum();
        return;
    }
    if (g_lx.tok == T_IDENT) {
        parse_assignment();
        return;
    }
    fail("expected a statement");
}

/* ---- image builder --------------------------------------------------- */

u64 put16(u8 *p, u32 value)
{
    p[0] = (u8)(value & 0xFF);
    p[1] = (u8)((value >> 8) & 0xFF);
    return 2;
}

u64 put32(u8 *p, u32 value)
{
    p[0] = (u8)(value & 0xFF);
    p[1] = (u8)((value >> 8) & 0xFF);
    p[2] = (u8)((value >> 16) & 0xFF);
    p[3] = (u8)((value >> 24) & 0xFF);
    return 4;
}

u64 put64(u8 *p, u64 value)
{
    put32(p, (u32)value);
    put32(p + 4, (u32)(value >> 32));
    return 8;
}

void put_segment(u8 *p, u64 vaddr, u64 file_off, u64 file_size, u64 flags)
{
    put64(p + 0, vaddr);
    put64(p + 8, file_off);
    put64(p + 16, file_size);
    put64(p + 24, file_size);
    put64(p + 32, flags);
}

int build_image(u64 *size_out)
{
    int text_pad = (g_code_len + 15) & ~15;
    u64 data_off = kHeaderSize + text_pad;
    u64 file_size = data_off + (u64)g_data_len;
    if (file_size > kMaxImage) {
        fail("image too large");
        return -1;
    }

    memset(g_image, 0, kHeaderSize);
    g_image[0] = 'Z';
    g_image[1] = 'X';
    g_image[2] = 'E';
    g_image[3] = 1;
    put16(g_image + 4, 1);            /* version */
    put16(g_image + 6, 62);           /* machine x86-64 */
    put16(g_image + 8, 2);            /* segment count */
    put16(g_image + 10, (u32)kHeaderSize);
    put32(g_image + 12, 0);
    put64(g_image + 16, kTextBase);   /* entry */
    put64(g_image + 24, file_size);
    put_segment(g_image + 32, kTextBase, kHeaderSize, (u64)g_code_len, 5);
    put_segment(g_image + 72, kDataBase, data_off, (u64)g_data_len, 1);

    memcpy(g_image + kHeaderSize, g_code, (usize)g_code_len);
    memcpy(g_image + data_off, g_data, (usize)g_data_len);
    *size_out = file_size;
    return 0;
}

} // namespace

extern "C" int zcc_compile(const char *source, const u8 **image, u64 *size,
                           const char **error)
{
    g_failed = false;
    g_code_len = 0;
    g_data_len = 0;
    g_var_count = 0;
    g_error[0] = 0;

    g_lx.p = source;
    g_lx.line = 1;
    lex_next();

    if (!ident_is("int")) {
        fail("program must start with 'int main()'");
    } else {
        lex_next();
        if (!ident_is("main")) {
            fail("program must start with 'int main()'");
        } else {
            lex_next();
            expect_sym("(");
            expect_sym(")");
        }
    }

    if (!g_failed) {
        /* prologue: push rbp; mov rbp, rsp; sub rsp, frame */
        emit8(0x55);
        emit8(0x48);
        emit8(0x89);
        emit8(0xE5);
        emit8(0x48);
        emit8(0x81);
        emit8(0xEC);
        emit32(kFrameSize);
        parse_block();

        /* implicit exit(0) */
        emit8(0x31);
        emit8(0xC0);
        emit_exit();

        if (g_lx.tok != T_EOF)
            fail("trailing input after main()");
    }

    if (g_failed) {
        *error = g_error;
        return -1;
    }

    if (g_data_len == 0) {
        g_data[0] = 0;
        g_data_len = 1;
    }

    u64 image_size = 0;
    if (build_image(&image_size) != 0) {
        *error = g_error;
        return -1;
    }
    *image = g_image;
    *size = image_size;
    return 0;
}
