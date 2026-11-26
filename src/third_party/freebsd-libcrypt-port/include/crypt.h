/* Public crypt(3) surface for the FreeBSD libcrypt port. */
#ifndef ZHAOXU_CRYPT_PORT_H
#define ZHAOXU_CRYPT_PORT_H

#ifndef ZHAOXU_CRYPT_DATA_DEFINED
#define ZHAOXU_CRYPT_DATA_DEFINED
struct crypt_data {
    int initialized;
    char __buf[256];
};
#endif

char *crypt(const char *password, const char *salt);
char *crypt_r(const char *password, const char *salt,
              struct crypt_data *data);
const char *crypt_get_format(void);
int crypt_set_format(const char *format);

#endif
