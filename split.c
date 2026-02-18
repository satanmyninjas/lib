/*
 * split.c
 *
 * Splits a large file into fixed-size parts.
 * Generates SHA-256 hash of original file.
 * Writes metadata file (.manifest) for verification.
 *
 * POSIX compliant.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>      // read, write, close
#include <fcntl.h>       // open
#include <sys/stat.h>    // stat
#include <string.h>
#include <errno.h>
#include <openssl/sha.h>

#define DEFAULT_PART_SIZE (45ULL * 1024ULL * 1024ULL) // 45MB safe for GitHub
#define BUF_SIZE 65536

void sha256_file(const char *path, unsigned char hash[SHA256_DIGEST_LENGTH]) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("open for hashing");
        exit(EXIT_FAILURE);
    }

    SHA256_CTX ctx;
    SHA256_Init(&ctx);

    unsigned char buffer[BUF_SIZE];
    ssize_t bytes;

    while ((bytes = read(fd, buffer, sizeof(buffer))) > 0) {
        SHA256_Update(&ctx, buffer, bytes);
    }

    if (bytes < 0) {
        perror("read during hashing");
        close(fd);
        exit(EXIT_FAILURE);
    }

    SHA256_Final(hash, &ctx);
}

void print_hash(FILE *out, unsigned char hash[SHA256_DIGEST_LENGTH]) {
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        fprintf(out, "%02x", hash[i]);
}

int main(int argc, char *argv[]) {
    printf("[*] starting main()...\n");
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file> [part_size_MB]\n", argv[0]);
        return EXIT_FAILURE;
    }

    printf("[*] starting file read...\n");
    const char *input_file = argv[1];
    uint64_t part_size = DEFAULT_PART_SIZE;

    if (argc >= 3)
        part_size = strtoull(argv[2], NULL, 10) * 1024ULL * 1024ULL;

    struct stat st;
    if (stat(input_file, &st) != 0) {
        perror("stat");
        return EXIT_FAILURE;
    }

    uint64_t total_size = st.st_size;

    // Compute original hash
    unsigned char original_hash[SHA256_DIGEST_LENGTH];
    sha256_file(input_file, original_hash);
    printf("[*] computed original SHA256 hashes...\n");

    int in_fd = open(input_file, O_RDONLY);
    if (in_fd < 0) {
        perror("open input");
        return EXIT_FAILURE;
    }

    printf("[*] determining number of parts needed...\n");
    unsigned char buffer[BUF_SIZE];
    uint64_t bytes_written = 0;
    int part_num = 0;
    
    printf("[*] preparing file read and writes...\n");
    while (bytes_written < total_size) {

        char part_name[512];
        snprintf(part_name, sizeof(part_name), "%s.part%04d", input_file, part_num);

        int out_fd = open(part_name, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (out_fd < 0) {
            perror("open part");
            close(in_fd);
            return EXIT_FAILURE;
        }

        uint64_t part_bytes = 0;

        while (part_bytes < part_size && bytes_written < total_size) {
            ssize_t r = read(in_fd, buffer, sizeof(buffer));
            if (r < 0) {
                perror("read");
                exit(EXIT_FAILURE);
            }
            if (r == 0)
                break;

            ssize_t w = write(out_fd, buffer, r);
            if (w != r) {
                perror("write");
                exit(EXIT_FAILURE);
            }

            part_bytes += r;
            bytes_written += r;
        }

        close(out_fd);
        part_num++;
    }

    close(in_fd);
	
    // Write manifest
    char manifest_name[512];
    snprintf(manifest_name, sizeof(manifest_name), "%s.manifest", input_file);
    printf("[*] manifest file created...\n");

    FILE *manifest = fopen(manifest_name, "w");
    if (!manifest) {
        perror("manifest");
        return EXIT_FAILURE;
    }

    fprintf(manifest, "original_file=%s\n", input_file);
    fprintf(manifest, "total_size=%lu\n", total_size);
    fprintf(manifest, "parts=%d\n", part_num);
    fprintf(manifest, "sha256=");
    print_hash(manifest, original_hash);
    fprintf(manifest, "\n");

    fclose(manifest);

    printf("[DONE] split complete. %d parts created.\n", part_num);
    return EXIT_SUCCESS;
}

