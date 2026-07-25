// ============================================================
// vm.h — FINAL
// ============================================================
#ifndef VM_H
#define VM_H

#include <stdint.h>

#define STRING_POOL_SIZE 256
#define FUNC_TABLE_MAX 1024

typedef enum {
    V_UNDEF = 0,
    V_NUMBER = 1,
    V_STRING = 2,
    V_ARRAY = 3,
    V_HASH = 4,
    V_EDECIMAL = 5,
    V_FLOAT = 6
} ValueType;

typedef struct {
    int64_t hi;      // целая часть (число) или указатель на ByteArray
    uint32_t lo;     // дробная часть (число)
    uint8_t pad[3];  // выравнивание
    uint8_t tag;     // тип значения
} Value;  // ровно 16 байт

typedef struct ByteArray {
    uint8_t* data;
    int length;
} ByteArray;

typedef struct CallFrame {
    struct CallFrame* prev;
    uint8_t* return_ip;
    const uint8_t* code;
    int code_size;
    Value* locals;
    int local_count;
    int param_count;
    char** string_pool;
    int string_pool_count;
} CallFrame;

typedef struct VM VM;
typedef void (*InstrHandler)(VM* vm, uint32_t param);

struct VM {
    Value stack[4096];
    Value* sp;

    CallFrame* frame;
    uint8_t* ip;
    const uint8_t* code;
    int code_size;

    Value* globals;
    int global_count;

    char* string_pool[STRING_POOL_SIZE];
    int string_pool_lengths[STRING_POOL_SIZE];  // ← ДОБАВИТЬ
    int string_pool_count;

    InstrHandler handlers[256];
    // Файловые дескрипторы
    FILE* open_files[256];
    int open_file_count;
};


Value val_number(int64_t hi, uint32_t lo);
Value val_string(ByteArray* ba);
Value val_array(ByteArray* ba);
Value val_undef(void);

VM* vm_new(void);
void vm_free(VM* vm);
void vm_execute(VM* vm, const uint8_t* code, int code_size);

void vm_add_inc_path(VM* vm, const char* path);

#endif
