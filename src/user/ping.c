/* IPC self-test client: connects to test.echo and checks the reply. */
#include "userlib.h"

int user_main(long *stack)
{
    (void)stack;

    long channel = 0;
    for (int i = 0; i < 2000 && channel <= 0; i++)
        channel = sys_port_connect("test.echo");

    if (channel <= 0) {
        u_puts("[ping] connect failed\n");
        return 1;
    }

    if (sys_port_send(channel, 1, "ping", 4) < 0) {
        u_puts("[ping] send failed\n");
        return 2;
    }

    unsigned long type = 0;
    char buffer[64];
    long length = sys_port_recv(channel, &type, buffer, sizeof(buffer));
    u_puts("[ping] reply type ");
    u_putn((long)type);
    u_puts(": ");
    if (length > 0)
        sys_write(1, buffer, (u64)length);
    u_puts("\n");

    long shm = 0;
    length = sys_port_recv(channel, &type, &shm, sizeof(shm));
    if (length == (long)sizeof(shm) && shm > 0) {
        char *shared = (char *)sys_shm_map(shm);
        if (shared) {
            u_puts("[ping] shm: ");
            sys_write(1, shared, 16);
            u_puts("\n");
            sys_shm_unmap(shm);
        }
        sys_port_send(channel, 4, "ack", 3);
    }

    sys_port_close(channel);
    return 0;
}
