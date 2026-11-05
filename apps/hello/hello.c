#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "ecore/elf.h"

// Embedded application manifest
EOS_APP_MANIFEST_ATTR = {
    EOS_BIN_INITIALIZER,
    .filename = "hello.elf",
    .name = "Hello App",
    .group = "demo",
    .description = "Standalone dynamic ELF demo application",
    .flags = EOS_BIN_FLAG_NONE,
};

int main(int argc, char **argv) {
    printf("Hello from EOS dynamic ELF app: %s!\n", argv[0]);
    for (int i = 1; i < argc; i++) {
        printf("  argv[%d] = %s\n", i, argv[i]);
    }
    return 0;
}
