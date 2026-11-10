/*
 * Zhaoxu OS - keyboard decoder running inside the Ring3 driver service.
 * Polls the 8042 output buffer and pushes decoded characters to the kernel
 * input ring.
 */
#include <types.h>

extern u8 ps2_status(void);
extern u8 ps2_read_data(void);
void driver_push_key(char c);
int toupper(int c);

static int shift_down;
static int caps_lock;
static int ctrl_down;
static int extended;

static const char normal_map[128] = {
    [0x01]='\x1b',
    [0x02]='1', [0x03]='2', [0x04]='3', [0x05]='4', [0x06]='5',
    [0x07]='6', [0x08]='7', [0x09]='8', [0x0A]='9', [0x0B]='0',
    [0x0C]='-', [0x0D]='=', [0x0E]='\b', [0x0F]='\t',
    [0x10]='q', [0x11]='w', [0x12]='e', [0x13]='r', [0x14]='t',
    [0x15]='y', [0x16]='u', [0x17]='i', [0x18]='o', [0x19]='p',
    [0x1A]='[', [0x1B]=']', [0x1C]='\n',
    [0x1E]='a', [0x1F]='s', [0x20]='d', [0x21]='f', [0x22]='g',
    [0x23]='h', [0x24]='j', [0x25]='k', [0x26]='l', [0x27]=';',
    [0x28]='\'', [0x29]='`', [0x2B]='\\',
    [0x2C]='z', [0x2D]='x', [0x2E]='c', [0x2F]='v', [0x30]='b',
    [0x31]='n', [0x32]='m', [0x33]=',', [0x34]='.', [0x35]='/',
    [0x39]=' '
};

static const char shift_map[128] = {
    [0x02]='!', [0x03]='@', [0x04]='#', [0x05]='$', [0x06]='%',
    [0x07]='^', [0x08]='&', [0x09]='*', [0x0A]='(', [0x0B]=')',
    [0x0C]='_', [0x0D]='+', [0x0E]='\b', [0x0F]='\t',
    [0x10]='Q', [0x11]='W', [0x12]='E', [0x13]='R', [0x14]='T',
    [0x15]='Y', [0x16]='U', [0x17]='I', [0x18]='O', [0x19]='P',
    [0x1A]='{', [0x1B]='}', [0x1C]='\n',
    [0x1E]='A', [0x1F]='S', [0x20]='D', [0x21]='F', [0x22]='G',
    [0x23]='H', [0x24]='J', [0x25]='K', [0x26]='L', [0x27]=':',
    [0x28]='"', [0x29]='~', [0x2B]='|',
    [0x2C]='Z', [0x2D]='X', [0x2E]='C', [0x2F]='V', [0x30]='B',
    [0x31]='N', [0x32]='M', [0x33]='<', [0x34]='>', [0x35]='?',
    [0x39]=' '
};

static void handle_scancode(u8 scancode)
{
    if (scancode == 0xE0) {
        extended = 1;
        return;
    }
    if (extended) {
        extended = 0;
        return;
    }
    if (scancode == 0x2A || scancode == 0x36) {
        shift_down = 1;
        return;
    }
    if (scancode == 0xAA || scancode == 0xB6) {
        shift_down = 0;
        return;
    }
    if (scancode == 0x1D) {
        ctrl_down = 1;
        return;
    }
    if (scancode == 0x9D) {
        ctrl_down = 0;
        return;
    }
    if (scancode == 0x3A) {
        caps_lock = !caps_lock;
        return;
    }
    if (scancode & 0x80)
        return;

    char c = 0;
    int is_letter = (scancode >= 0x10 && scancode <= 0x19) ||
                    (scancode >= 0x1E && scancode <= 0x26) ||
                    (scancode >= 0x2C && scancode <= 0x32);
    if (is_letter) {
        c = normal_map[scancode];
        if (shift_down ^ caps_lock)
            c = (char)toupper((int)c);
    } else {
        c = shift_down ? shift_map[scancode] : normal_map[scancode];
    }

    if (ctrl_down && (c == 'l' || c == 'L'))
        c = 12;
    else if (ctrl_down && (c == 'c' || c == 'C'))
        c = 3;

    if (c)
        driver_push_key(c);
}

void keyboard_init(void)
{
    shift_down = 0;
    caps_lock = 0;
    ctrl_down = 0;
    extended = 0;
}

void keyboard_poll(void)
{
    u8 status = ps2_status();
    if ((status & 1) == 0 || (status & 0x20))
        return;
    handle_scancode(ps2_read_data());
}
