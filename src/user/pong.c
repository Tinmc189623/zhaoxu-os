/* IPC self-test service: accepts one client and replies "pong". */
#include "userlib.h"

int user_main(long *stack)
{
    (void)stack;

    long port = sys_port_listen("test.echo");
    if (port <= 0) {
        u_puts("[pong] listen failed\n");
        return 1;
    }

    long channel = sys_port_accept(port);
    if (channel <= 0) {
        u_puts("[pong] accept failed\n");
        return 2;
    }

    unsigned long type = 0;
    char buffer[64];
    long length = sys_port_recv(channel, &type, buffer, sizeof(buffer));
    u_puts("[pong] request type ");
    u_putn((long)type);
    u_puts(": ");
    if (length > 0)
        sys_write(1, buffer, (u64)length);
    u_puts("\n");

    sys_port_send(channel, 2, "pong", 4);

    long shm = sys_shm_create(4096);
    if (shm > 0) {
        char *shared = (char *)sys_shm_map(shm);
        if (shared) {
            const char *text = "shared-from-pong";
            for (int i = 0; text[i]; i++)
                shared[i] = text[i];
            shared[16] = 0;

            sys_port_send(channel, 3, &shm, sizeof(shm));

            unsigned long ack = 0;
            char scratch[8];
            sys_port_recv(channel, &ack, scratch, sizeof(scratch));
            u_puts("[pong] shm acknowledged\n");
            sys_shm_unmap(shm);
        }
        sys_shm_close(shm);
    }

    sys_port_close(channel);
    sys_port_close(port);
    return 0;
}
