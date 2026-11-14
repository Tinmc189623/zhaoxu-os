/*
 * Zhaoxu OS - eCryptfs-style encrypted stacking filesystem.
 *
 * Files live in a lower writable filesystem (normally tmpfs) wrapped in a
 * 64-byte envelope:
 *
 *   magic "ZECR" | version | flags | original size | salt[16] | iv[16]
 *
 * The file key is SHA-256(master key || salt); contents are AES-256-CBC with
 * PKCS#7 padding.  The master key is SHA-256 of /etc/ecryptfs.key when that
 * file exists, otherwise a fixed development passphrase so the mount is
 * always available on a single-user image.  The VFS has no sidecar metadata
 * channel, so file names stay visible while contents are encrypted.
 */
#include <crypto.h>
#include <kprintf.h>
#include <mem.h>
#include <string.h>
#include <timer.h>
#include <types.h>
#include <vfs.h>

#define ECRYPT_HEADER 64
#define ECRYPT_MAGIC "ZECR"
#define ECRYPT_VERSION 1
#define ECRYPT_MAX_FILE (16u * 1024u * 1024u)

struct ecryptfs {
    char lower[VFS_PATH_MAX];
    u8 master[32];
};

static void ecrypt_key(const struct ecryptfs *fs, const u8 salt[16],
                       u8 key[32])
{
    u8 material[48];
    memcpy(material, fs->master, 32);
    memcpy(material + 32, salt, 16);
    sha256(material, sizeof(material), key);
}

static int ecrypt_wrap(const struct ecryptfs *fs, const u8 *data, u64 size,
                       u8 **out, u64 *out_size)
{
    if (size > ECRYPT_MAX_FILE)
        return -1;
    usize padded = (usize)((size / 16 + 1) * 16);
    u8 *blob = kmalloc(ECRYPT_HEADER + padded);
    if (!blob)
        return -1;

    memset(blob, 0, ECRYPT_HEADER);
    memcpy(blob, ECRYPT_MAGIC, 4);
    blob[4] = ECRYPT_VERSION;
    blob[5] = 0;
    for (int i = 0; i < 8; i++)
        blob[8 + i] = (u8)(size >> (i * 8));

    /* No hardware RNG is available, so salt and IV are derived from the
     * master key, a monotonic write counter and the timer; the same file
     * written twice never reuses an IV within a boot. */
    static u32 write_counter;
    u8 seed[48];
    u64 tick = timer_ticks();
    u32 counter = ++write_counter;
    memcpy(seed, fs->master, 32);
    memcpy(seed + 32, &size, 8);
    memcpy(seed + 40, &counter, 4);
    memcpy(seed + 44, &tick, 4);
    u8 digest[32];
    sha256(seed, sizeof(seed), digest);
    memcpy(blob + 16, digest, 16);
    sha256(digest, 32, digest);
    memcpy(blob + 32, digest, 16);

    u8 key[32];
    ecrypt_key(fs, blob + 16, key);
    struct aes_ctx ctx;
    if (aes_set_key(&ctx, key, sizeof(key)) != 0) {
        kfree(blob);
        return -1;
    }
    int written = aes_cbc_encrypt(&ctx, blob + 32, data, (usize)size,
                                  blob + ECRYPT_HEADER, padded);
    if (written < 0) {
        kfree(blob);
        return -1;
    }
    *out = blob;
    *out_size = ECRYPT_HEADER + (u64)written;
    return 0;
}

static int ecrypt_unwrap(const struct ecryptfs *fs, const u8 *blob, u64 size,
                         u8 **out, u64 *out_size)
{
    if (size < ECRYPT_HEADER || memcmp(blob, ECRYPT_MAGIC, 4) != 0)
        return -1;
    u8 version = blob[4];
    if (version != ECRYPT_VERSION)
        return -1;
    u64 original = 0;
    for (int i = 0; i < 8; i++)
        original |= (u64)blob[8 + i] << (i * 8);
    u64 cipher_len = size - ECRYPT_HEADER;
    if (!cipher_len || (cipher_len % 16) || original > cipher_len)
        return -1;

    u8 key[32];
    ecrypt_key(fs, blob + 16, key);
    struct aes_ctx ctx;
    if (aes_set_key(&ctx, key, sizeof(key)) != 0)
        return -1;

    u8 *plain = kmalloc((usize)cipher_len);
    if (!plain)
        return -1;
    int produced = aes_cbc_decrypt(&ctx, blob + 32, blob + ECRYPT_HEADER,
                                   (usize)cipher_len, plain,
                                   (usize)cipher_len);
    if (produced < 0 || (u64)produced < original) {
        kfree(plain);
        return -1;
    }
    *out = plain;
    *out_size = original;
    return 0;
}

static int ecrypt_lower(const struct ecryptfs *fs, const char *rel, char *out,
                        usize capacity)
{
    usize base = strlen(fs->lower);
    usize length = strlen(rel ? rel : "");
    if (base + length + 2 > capacity)
        return -1;
    memcpy(out, fs->lower, base);
    if (length) {
        out[base] = '/';
        memcpy(out + base + 1, rel, length + 1);
    } else {
        out[base] = 0;
    }
    return 0;
}

static int ecrypt_read_raw(const char *path, u8 **data, u64 *size)
{
    int is_dir = 0;
    u64 length = 0;
    if (vfs_stat(path, &is_dir, &length) != 0 || is_dir)
        return -1;
    if (length > ECRYPT_MAX_FILE + ECRYPT_HEADER)
        return -1;
    u8 *buffer = kmalloc((usize)(length ? length : 1));
    if (!buffer)
        return -1;
    u64 read = 0;
    if (length && vfs_read_into(path, buffer, length, &read) != 0) {
        kfree(buffer);
        return -1;
    }
    *data = buffer;
    *size = length;
    return 0;
}

struct ecrypt_list_state {
    vfs_list_cb callback;
    void *context;
};

/* Hide the placeholder that keeps the lower directory present. */
static void ecrypt_list_filter(const char *name, u64 size, int is_dir,
                               void *context)
{
    struct ecrypt_list_state *state = context;
    if (name && strcmp(name, ".keep") == 0)
        return;
    state->callback(name, size, is_dir, state->context);
}

static int ecryptfs_list(void *private, const char *rel, vfs_list_cb callback,
                         void *context)
{
    struct ecryptfs *fs = private;
    char path[VFS_PATH_MAX];
    if (ecrypt_lower(fs, rel, path, sizeof(path)) != 0)
        return -1;
    struct ecrypt_list_state state = {callback, context};
    return vfs_list(path, ecrypt_list_filter, &state);
}

static int ecryptfs_stat(void *private, const char *rel, int *is_dir,
                         u64 *size)
{
    struct ecryptfs *fs = private;
    char path[VFS_PATH_MAX];
    if (ecrypt_lower(fs, rel, path, sizeof(path)) != 0)
        return -1;
    int dir = 0;
    u64 raw = 0;
    if (vfs_stat(path, &dir, &raw) != 0)
        return -1;
    if (dir) {
        if (is_dir)
            *is_dir = 1;
        if (size)
            *size = 0;
        return 0;
    }
    if (is_dir)
        *is_dir = 0;
    if (size) {
        u8 header[ECRYPT_HEADER];
        u64 got = 0;
        if (raw >= ECRYPT_HEADER &&
            vfs_read_at(path, 0, header, sizeof(header), &got) == 0 &&
            got == sizeof(header) &&
            memcmp(header, ECRYPT_MAGIC, 4) == 0) {
            u64 original = 0;
            for (int i = 0; i < 8; i++)
                original |= (u64)header[8 + i] << (i * 8);
            *size = original;
        } else {
            *size = raw;
        }
    }
    return 0;
}

static int ecryptfs_read(void *private, const char *rel, u64 offset,
                         void *buffer, u64 capacity, u64 *read_out)
{
    struct ecryptfs *fs = private;
    char path[VFS_PATH_MAX];
    if (ecrypt_lower(fs, rel, path, sizeof(path)) != 0)
        return -1;

    u8 *raw = NULL;
    u64 raw_size = 0;
    if (ecrypt_read_raw(path, &raw, &raw_size) != 0)
        return -1;

    u8 *plain = raw;
    u64 plain_size = raw_size;
    u8 *allocated = NULL;
    if (raw_size >= ECRYPT_HEADER && memcmp(raw, ECRYPT_MAGIC, 4) == 0) {
        if (ecrypt_unwrap(fs, raw, raw_size, &allocated, &plain_size) != 0) {
            kfree(raw);
            return -1;
        }
        plain = allocated;
    }

    if (offset >= plain_size) {
        if (read_out)
            *read_out = 0;
        kfree(allocated);
        kfree(raw);
        return 0;
    }
    u64 count = plain_size - offset;
    if (count > capacity)
        count = capacity;
    memcpy(buffer, plain + offset, (usize)count);
    if (read_out)
        *read_out = count;
    kfree(allocated);
    kfree(raw);
    return 0;
}

static int ecryptfs_write(void *private, const char *rel, const u8 *data,
                          u64 size)
{
    struct ecryptfs *fs = private;
    char path[VFS_PATH_MAX];
    if (ecrypt_lower(fs, rel, path, sizeof(path)) != 0)
        return -1;
    u8 *blob = NULL;
    u64 blob_size = 0;
    if (ecrypt_wrap(fs, data, size, &blob, &blob_size) != 0)
        return -1;
    int result = vfs_write_file(path, blob, blob_size);
    kfree(blob);
    return result;
}

static int ecryptfs_unlink(void *private, const char *rel)
{
    struct ecryptfs *fs = private;
    char path[VFS_PATH_MAX];
    if (ecrypt_lower(fs, rel, path, sizeof(path)) != 0)
        return -1;
    return vfs_unlink(path);
}

static const struct vfs_fs_ops g_ecryptfs_ops = {
    .name = "ecryptfs",
    .list = ecryptfs_list,
    .stat = ecryptfs_stat,
    .read = ecryptfs_read,
    .write = ecryptfs_write,
    .unlink = ecryptfs_unlink,
};

/* Mount the encrypted view at /secure with tmpfs as its backing store. */
int ecryptfs_mount(void)
{
    struct ecryptfs *fs = kzalloc(sizeof(*fs));
    if (!fs)
        return -1;
    strcpy(fs->lower, "/tmp/.ecryptfs");

    u8 passphrase[256];
    memset(passphrase, 0, sizeof(passphrase));
    u64 got = 0;
    static const char *fallback = "zhaoxu-ecryptfs-single-user";
    if (vfs_read_into("/etc/ecryptfs.key", passphrase,
                      sizeof(passphrase), &got) != 0 || !got) {
        memcpy(passphrase, fallback, strlen(fallback));
        got = strlen(fallback);
        kprintf("[ecrypt] no /etc/ecryptfs.key; using the image default "
                "passphrase\n");
    }
    sha256(passphrase, (usize)got, fs->master);
    memset(passphrase, 0, sizeof(passphrase));

    /* tmpfs has no mkdir entry point; writing an empty placeholder creates
     * the lower directory through the normal VFS write path. */
    static const u8 placeholder = 0;
    vfs_write_file("/tmp/.ecryptfs/.keep", &placeholder, 0);

    if (vfs_mount("/secure", "ecryptfs", &g_ecryptfs_ops, fs) != 0) {
        kfree(fs);
        return -1;
    }
    return 0;
}
