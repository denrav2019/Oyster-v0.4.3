// ============================================================
// main.c — Oyster Language Entry Point
// ============================================================
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vm.h"

void compile_to_bytecode(const char* source, uint8_t** out_code, int* out_size,
                         int source_mode, int extended_mode);
void free_string_pool(void);

static char* my_strdup(const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str) + 1;
    char* copy = malloc(len);
    if (copy) memcpy(copy, str, len);
    return copy;
}

static int is_osf_file(const char* filename) {
    const char* dot = strrchr(filename, '.');
    return dot && strcmp(dot, ".osf") == 0;
}

static int is_oce_file(const char* filename) {
    const char* dot = strrchr(filename, '.');
    return dot && strcmp(dot, ".oce") == 0;
}

static void print_usage(const char* prog) {
    printf("Oyster Language v0.4.0\n");
    printf("Usage:\n");
    printf("  %s <file.osf>              - compile and run\n", prog);
    printf("  %s <file.oce>              - run compiled bytecode\n", prog);
    printf("  %s -c <file.osf>           - compile only\n", prog);
    printf("  %s -c <file.osf> -o <out>  - compile to custom output\n", prog);
    printf("  %s -s <file.osf>           - compile with source comments\n", prog);
    printf("  %s -e <file.osf>           - compile with extended syntax\n", prog);
    printf("  %s -I <path>               - add path to @INC\n", prog);
    printf("  %s -h, --help              - show this help\n", prog);
    printf("\n");
    printf("Examples:\n");
    printf("  %s script.osf              - compile and run script.osf\n", prog);
    printf("  %s script.oce              - run compiled script.oce\n", prog);
    printf("  %s -c script.osf           - compile to script.oce\n", prog);
    printf("  %s -c script.osf -o out.oce - compile to out.oce\n", prog);
    printf("  %s -s script.osf           - compile with source comments\n", prog);
    printf("  %s -e script.osf           - compile with extended syntax\n");
    printf("  %s -I ./my_modules         - add ./my_modules to @INC\n", prog);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    int compile_only = 0;
    int source_mode = 0;
    int extended_mode = 0;
    const char* input_file = NULL;
    const char* output_file = NULL;
    const char* inc_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--compile") == 0) {
            compile_only = 1;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--source") == 0) {
            source_mode = 1;
        } else if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--extended") == 0) {
            extended_mode = 1;
        } else if (strcmp(argv[i], "-I") == 0 && i + 1 < argc) {
            inc_path = argv[++i];
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (argv[i][0] != '-') {
            input_file = argv[i];
        }
    }

    if (!input_file) {
        fprintf(stderr, "No input file specified\n");
        print_usage(argv[0]);
        return 1;
    }

    int is_osf = is_osf_file(input_file);
    int is_oce = is_oce_file(input_file);

    if (!is_osf && !is_oce) {
        fprintf(stderr, "Unknown file type: %s (expected .osf or .oce)\n", input_file);
        return 1;
    }

    if (is_osf) {
        FILE* f = fopen(input_file, "r");
        if (!f) {
            perror("Cannot open input file");
            return 1;
        }
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fseek(f, 0, SEEK_SET);
        char* source = malloc(len + 1);
        size_t read_len = fread(source, 1, len, f);
        source[read_len] = '\0';
        fclose(f);

        uint8_t* bytecode = NULL;
        int bytecode_size = 0;

        compile_to_bytecode(source, &bytecode, &bytecode_size, source_mode, extended_mode);

        if (!bytecode) {
            free(source);
            return 1;
        }

        const char* out_name = output_file;
        char* auto_name = NULL;
        if (!out_name) {
            auto_name = my_strdup(input_file);
            char* dot = strrchr(auto_name, '.');
            if (dot) strcpy(dot, ".oce");
            else strcat(auto_name, ".oce");
            out_name = auto_name;
        }

        FILE* out = fopen(out_name, "w");
        if (out) {
            size_t written = fwrite(bytecode, 1, bytecode_size, out);
            fflush(out);
            fclose(out);
            printf("Compiled to %s\n", out_name);
        } else {
            perror("Cannot write output file");
        }
        free(auto_name);

        if (!compile_only) {
            VM* vm = vm_new();
            if (inc_path) {
                vm_add_inc_path(vm, inc_path);
            }
            vm_execute(vm, bytecode, bytecode_size);
            vm_free(vm);
        }

        free(bytecode);
        free_string_pool();
        free(source);
        return 0;
    }

    if (is_oce) {
        FILE* f = fopen(input_file, "rb");
        if (!f) {
            perror("Cannot open .oce file");
            return 1;
        }
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t* bytecode = malloc(len + 1);
        size_t read_len = fread(bytecode, 1, len, f);
        bytecode[read_len] = '\0';
        fclose(f);

        VM* vm = vm_new();
        if (inc_path) {
            vm_add_inc_path(vm, inc_path);
        }
        vm_execute(vm, bytecode, (int)len);
        vm_free(vm);
        free(bytecode);
        return 0;
    }

    return 0;
}
