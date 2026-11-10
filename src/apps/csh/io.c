/* csh - console output helpers shared by every command. */
#include "csh.h"

void csh_out(const char *text)
{
    if (text && *text)
        sys_write(1, text, u_strlen(text));
}

void csh_outln(const char *text)
{
    csh_out(text);
    csh_out("\n");
}

void csh_number(long value)
{
    u_putn(value);
}

void csh_error(const char *text)
{
    csh_out("csh: ");
    csh_outln(text);
}
