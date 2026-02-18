/*
 * stitch.c
 *
 * Reassembles parts using manifest.
 * Writes to temporary file.
 * Verifies SHA-256.
 * Deletes parts only after verification success.
 * Atomic rename replacement.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <openssl/sha.h>

#define BUF_SIZE 65536

/* Compute SHA256 of file (streamed, constant memory) */
void sha256_file(const char *path, unsigned char hash[SHA256_DIGEST_LENGTH]) {

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("hash open");
        exit(EXIT_FAILURE);
    }

    SHA256_CTX ctx;
    SHA256_Init(&ctx);

    unsigned char buffer[BUF_SIZE];
    ssize_t bytes;

    while ((bytes = read(fd, buffer, sizeof(buffer))) > 0)
        SHA256_Update(&ctx, buffer, bytes);

    if (bytes < 0) {
        perror("hash read");
        close(fd);
        exit(EXIT_FAILURE);
    }

    SHA256_Final(hash, &ctx);
    close(fd);
}

/* Convert hex string to binary hash */
void hex_to_bytes(const char *hex, unsigned char *out) {
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        sscanf(hex + (2 * i), "%2hhx", &out[i]);
}

int main(int argc, char *argv[]) {

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <manifest>\n", argv[0]);
        return EXIT_FAILURE;
    }

    printf("[*] checking for manifest file...\n");
    const char *manifest_path = argv[1];

    FILE *manifest = fopen(manifest_path, "r");
    if (!manifest) {
        perror("manifest open");
        return EXIT_FAILURE;
    }

    char original_file[512];
    uint64_t total_size;
    int parts;
    char hash_hex[65];

    if (fscanf(manifest, "original_file=%511s\n", original_file) != 1 ||
        fscanf(manifest, "total_size=%lu\n", &total_size) != 1 ||
        fscanf(manifest, "parts=%d\n", &parts) != 1 ||
        fscanf(manifest, "sha256=%64s\n", hash_hex) != 1) {

        fprintf(stderr, "[*] malformed manifest. closing file read...\n");
        fclose(manifest);
        return EXIT_FAILURE;
    }

    fclose(manifest);
    printf("[*] manifest file read success.\n");

    /* Temporary reconstruction file */
    char temp_file[600];
    snprintf(temp_file, sizeof(temp_file), "%s.tmp", original_file);

    int out_fd = open(temp_file, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (out_fd < 0) {
        perror("open temp output");
        return EXIT_FAILURE;
    }

    unsigned char buffer[BUF_SIZE];

    /* Reassemble parts */
    printf("[*] preparing to reassemble parts...\n");
    for (int i = 0; i < parts; i++) {

        char part_name[600];
        snprintf(part_name, sizeof(part_name), "%s.part%04d", original_file, i);

        int in_fd = open(part_name, O_RDONLY);
        if (in_fd < 0) {
            perror("open part");
            close(out_fd);
            unlink(temp_file);
            return EXIT_FAILURE;
        }

        ssize_t r;
        while ((r = read(in_fd, buffer, sizeof(buffer))) > 0) {

            ssize_t w = write(out_fd, buffer, r);
            if (w != r) {
                perror("write temp");
                close(in_fd);
                close(out_fd);
                unlink(temp_file);
                return EXIT_FAILURE;
            }
        }

        if (r < 0) {
            perror("read part");
            close(in_fd);
            close(out_fd);
            unlink(temp_file);
            return EXIT_FAILURE;
        }

        close(in_fd);
    }

    close(out_fd);

    /* Verify SHA256 */
    printf("[*] verifying SHA256 digests...\n");
    unsigned char expected[SHA256_DIGEST_LENGTH];
    unsigned char actual[SHA256_DIGEST_LENGTH];

    hex_to_bytes(hash_hex, expected);
    sha256_file(temp_file, actual);

    if (memcmp(expected, actual, SHA256_DIGEST_LENGTH) != 0) {
        fprintf(stderr, "[ERROR] hash mismatch. reconstruction aborted.\n");
        unlink(temp_file);  // Remove corrupt output
        return EXIT_FAILURE;
    }

    /* Backup existing file only after verification success */
    struct stat st;
    if (stat(original_file, &st) == 0) {
        char backup_name[600];
        snprintf(backup_name, sizeof(backup_name), "%s.bkp", original_file);
        if (rename(original_file, backup_name) != 0) {
            perror("backup rename");
            unlink(temp_file);
            return EXIT_FAILURE;
        }
    }

    /* Atomic replace */
    if (rename(temp_file, original_file) != 0) {
        perror("final rename");
        return EXIT_FAILURE;
    }

    /* Cleanup parts ONLY AFTER success */
    for (int i = 0; i < parts; i++) {
        char part_name[600];
        snprintf(part_name, sizeof(part_name), "%s.part%04d", original_file, i);
        unlink(part_name);
    }

    printf("[DONE] reassembly complete. artifacts cleaned.\n");
    return EXIT_SUCCESS;
}

