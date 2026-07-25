// ============================================================
// vm.c — Oyster VM v0.4.2 (полная поддержка внешних модулей)
// ============================================================
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "vm.h"
#include <math.h>

#define UNICODE_MARKER "\x5C\x75\x75\x00"
#define UNICODE_MARKER_LEN 4
#define STACK_SIZE 4096
#define MAX_MODULES 64
#define _GNU_SOURCE

// === Кэш загруженных модулей ===
typedef struct {
    char name[64];
    uint8_t* bytecode;
    int bytecode_size;
    const uint8_t* code_start;
    int code_size;
    char* string_pool[256];
    int string_pool_count;
    Value* globals;
    int global_count;

    struct {
        char name[128];
        char module_name[64];  // имя модуля (пусто для локальных)
        int code_offset;
        int param_count;
        int local_count;
    } functions[256];
    int function_count;
} LoadedModule;

static LoadedModule loaded_modules[MAX_MODULES];
static int loaded_module_count = 0;

// ТДФ основного модуля (для handle_call)
static LoadedModule main_module;

// === Конструкторы ===
Value val_number(int64_t hi, uint32_t lo) {
    Value v;
    v.hi = hi;
    v.lo = lo;
    v.pad[0] = v.pad[1] = v.pad[2] = 0;
    v.tag = V_NUMBER;
    return v;
}

Value val_string(ByteArray* ba) {
    Value v;
    v.hi = (int64_t)ba;
    v.lo = 0;
    v.pad[0] = v.pad[1] = v.pad[2] = 0;
    v.tag = V_STRING;
    return v;
}

Value val_array(ByteArray* ba) {
    Value v;
    v.hi = (int64_t)ba;
    v.lo = 0;
    v.pad[0] = v.pad[1] = v.pad[2] = 0;
    v.tag = V_ARRAY;
    return v;
}

Value val_undef(void) {
    Value v;
    v.hi = 0;
    v.lo = 0;
    v.pad[0] = v.pad[1] = v.pad[2] = 0;
    v.tag = V_UNDEF;
    return v;
}

Value val_hash(ByteArray* ba) {
    Value v;
    v.hi = (int64_t)ba;
    v.lo = 0;
    v.pad[0] = v.pad[1] = v.pad[2] = 0;
    v.tag = V_HASH;
    return v;
}





// === Стек ===
static void vm_push(VM* vm, Value v) {
    if (vm->sp >= vm->stack + STACK_SIZE) {
        fprintf(stderr, "Stack overflow\n");
        exit(1);
    }
    *vm->sp = v;
    vm->sp++;
}

static Value vm_pop(VM* vm) {
    if (vm->sp <= vm->stack) {
        fprintf(stderr, "Stack underflow\n");
        exit(1);
    }
    vm->sp--;
    Value v = *vm->sp;

    return v;
}
static uint32_t read_hex_param(const uint8_t** ip) {
    char hex[9] = {0};
    for (int i = 0; i < 8; i++) hex[i] = (char)(*ip)[i];
    *ip += 8;
    return (uint32_t)strtoul(hex, NULL, 16);
}

// === Вспомогательные ===
static int is_truthy(Value v) {
    if (v.tag == V_UNDEF) return 0;
    if (v.tag == V_NUMBER) return !(v.hi == 0 && v.lo == 0);
    if (v.tag == V_STRING) {
        ByteArray* ba = (ByteArray*)v.hi;
        return ba != NULL && ba->length > 0;
    }
    if (v.tag == V_ARRAY) {
        ByteArray* ba = (ByteArray*)v.hi;
        return ba != NULL && ba->length > 0;
    }
    return 1;
}

// === Загрузка модуля ===
static LoadedModule* vm_find_module(const char* name) {
    for (int i = 0; i < loaded_module_count; i++) {
        if (strcmp(loaded_modules[i].name, name) == 0) {
            return &loaded_modules[i];
        }
    }
    return NULL;
}

static LoadedModule* vm_load_module(VM* vm, const char* module_name) {
    (void)vm;

    LoadedModule* mod = vm_find_module(module_name);
    if (mod) return mod;

    char filename[256];
    FILE* f = NULL;
    snprintf(filename, sizeof(filename), "%s.ocm", module_name);
    f = fopen(filename, "rb");
    if (!f) {
        snprintf(filename, sizeof(filename), "./modules/%s.ocm", module_name);
        f = fopen(filename, "rb");
    }
    if (!f) {
        fprintf(stderr, "VM: Module '%s' not found\n", module_name);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = malloc(size);
    fread(data, 1, size, f);
    fclose(f);

    if (size < 6 || memcmp(data, "<OYCE;", 6) != 0) {
        fprintf(stderr, "VM: Invalid module format\n");
        free(data);
        return NULL;
    }

    const char* ptr = (char*)data + 6;
    const char* he = strchr(ptr, '>');
    if (!he) { free(data); return NULL; }

    size_t hl = he - ptr;
    char* hb = malloc(hl + 1);
    memcpy(hb, ptr, hl);
    hb[hl] = '\0';


    uint32_t str_off = 0, tdf_off = 0, code_off = 0, const_off = 0;
    uint32_t global_count = 0;
    char* tok = strtok(hb, ";");
    while (tok) {
        if (tok[0] == 'S') str_off = strtoul(tok + 2, NULL, 16);
        if (tok[0] == 'F') tdf_off = strtoul(tok + 2, NULL, 16);
        if (tok[0] == 'C') code_off = strtoul(tok + 2, NULL, 16);
        if (tok[0] == 'N') const_off = strtoul(tok + 2, NULL, 16);
        if (tok[0] == 'G') global_count = strtoul(tok + 2, NULL, 16);
        tok = strtok(NULL, ";");
    }
    free(hb);

    if (!str_off || !tdf_off || !code_off || !const_off) {
        fprintf(stderr, "VM: Invalid module header\n");
        free(data);
        return NULL;
    }

    if (loaded_module_count >= MAX_MODULES) {
        fprintf(stderr, "VM: Too many modules\n");
        free(data);
        return NULL;
    }

    LoadedModule* lm = &loaded_modules[loaded_module_count++];
    strcpy(lm->name, module_name);
    lm->bytecode = data;
    lm->bytecode_size = size;

    // Парсим пул строк
    // Парсим пул строк
    const uint8_t* sp = data + str_off;
    lm->string_pool_count = 0;

    while (sp < data + const_off && lm->string_pool_count < 256) {
        if (sp[0] == '<' && sp[1] == 'l' && sp[2] == ':') {
            sp += 3;
            int len = strtoul((char*)sp, NULL, 16);
            sp += 4;
            if (*sp != '>') break;
            sp++;
            char* s = malloc(len + 1);
            memcpy(s, sp, len);
            s[len] = '\0';
            sp += len;
            lm->string_pool[lm->string_pool_count++] = s;
        } else break;
    }

    // Парсим ТДФ
    // Парсим ТДФ (новый формат: <f:name:module:pc:lc:offset:export>)
    const uint8_t* tp = data + tdf_off;
    lm->function_count = 0;

    while (tp < data + size && lm->function_count < 256) {
        if (tp[0] == '<' && tp[1] == 'f' && tp[2] == ':') {
            tp += 3;

            // Читаем имя функции до ':'
            char fname[128]; int i = 0;
            while (tp < data + size && *tp != ':') fname[i++] = *tp++;
            fname[i] = '\0';
            if (*tp == ':') tp++;

            // Читаем имя модуля до ':'
            char mname[64]; i = 0;
            while (tp < data + size && *tp != ':') mname[i++] = *tp++;
            mname[i] = '\0';
            if (*tp == ':') tp++;

            // PARAM_COUNT (2 hex)
            char tmp[3]; memcpy(tmp, tp, 2); tmp[2] = '\0';
            int param_count = strtoul(tmp, NULL, 16);
            tp += 2;
            if (*tp == ':') tp++;

            // LOCAL_COUNT (2 hex)
            memcpy(tmp, tp, 2); tmp[2] = '\0';
            int local_count = strtoul(tmp, NULL, 16);
            tp += 2;
            if (*tp == ':') tp++;

            // CODE_OFFSET (8 hex)
            char otmp[9]; memcpy(otmp, tp, 8); otmp[8] = '\0';
            int code_offset = strtoul(otmp, NULL, 16);
            tp += 8;
            if (*tp == ':') tp++;

            // EXPORT_FLAG (пропускаем, все функции в ТДФ валидны)
            while (tp < data + size && *tp != '>') tp++;
            if (*tp == '>') tp++;

            // Сохраняем функцию
            strcpy(lm->functions[lm->function_count].name, fname);
            strcpy(lm->functions[lm->function_count].module_name, mname);
            lm->functions[lm->function_count].code_offset = code_offset;
            lm->functions[lm->function_count].param_count = param_count;
            lm->functions[lm->function_count].local_count = local_count;
            lm->function_count++;
        } else {
            tp++;
        }
    }
    const uint8_t* cp = data + code_off;
    while (*cp == ' ') cp++;
    lm->code_start = cp;
    lm->code_size = size - (cp - data);

    lm->global_count = global_count;
    lm->globals = calloc(global_count, sizeof(Value));
    for (uint32_t i = 0; i < global_count; i++) {
        lm->globals[i] = val_undef();
    }

    return lm;
}

// === Создание CallFrame для функции ===
static CallFrame* vm_create_frame(VM* vm, LoadedModule* mod, int func_idx) {


    CallFrame* frame = malloc(sizeof(CallFrame));
    frame->prev = vm->frame;
    frame->return_ip = vm->ip;

    int offset = mod->functions[func_idx].code_offset;
    frame->code = mod->code_start + offset;
    frame->code_size = mod->code_size - offset;

    int local_count = mod->functions[func_idx].local_count;
    int param_count = mod->functions[func_idx].param_count;

    frame->locals = calloc(local_count, sizeof(Value));
    frame->local_count = local_count;
    frame->param_count = param_count;



    for (int i = param_count - 1; i >= 0; i--) {
        if (vm->sp > vm->stack) {
            Value v = vm_pop(vm);
            frame->locals[i] = v;
        } else {
            frame->locals[i] = val_undef();
        }
    }

    frame->string_pool = mod->string_pool;
    frame->string_pool_count = mod->string_pool_count;

    return frame;
}

// === Обработчики опкодов ===

static void handle_integer(VM* vm, uint32_t param) {
    vm_push(vm, val_number((int32_t)param, 0));
}

static void handle_number_new(VM* vm, uint32_t param) {
    (void)param;
    Value lo_val = vm_pop(vm), hi_low = vm_pop(vm), hi_high = vm_pop(vm);
    int64_t hi = ((int64_t)(uint32_t)hi_high.hi << 32) | (uint32_t)hi_low.hi;
    uint32_t lo = (uint32_t)lo_val.hi;
    vm_push(vm, val_number(hi, lo));
}

static void handle_string(VM* vm, uint32_t param) {
    int idx = (int)param;
    CallFrame* frame = vm->frame;
    char** pool = frame ? frame->string_pool : vm->string_pool;
    int count = frame ? frame->string_pool_count : vm->string_pool_count;

    if (idx < count && pool[idx]) {
        char* str = pool[idx];
        int len = vm->string_pool_lengths[idx];  // ← ИСПОЛЬЗУЕМ СОХРАНЁННУЮ ДЛИНУ

        //int len = strlen(str);
        ByteArray* ba = malloc(sizeof(ByteArray));
        ba->data = malloc(len + 1);
        memcpy(ba->data, str, len + 1);
        ba->length = len;
        vm_push(vm, val_string(ba));
    } else {
        vm_push(vm, val_undef());
    }
}

static void handle_load_global(VM* vm, uint32_t param) {
    int idx = (int)param;
    if (vm->frame && idx < vm->frame->local_count) {
        vm_push(vm, vm->frame->locals[idx]);
    } else if (idx < vm->global_count) {
        vm_push(vm, vm->globals[idx]);
    } else {
        vm_push(vm, val_undef());
    }
}

static void handle_store_global(VM* vm, uint32_t param) {
    int idx = (int)param;
    Value v = vm_pop(vm);
    if (vm->frame && idx < vm->frame->local_count) {
        vm->frame->locals[idx] = v;
    } else if (idx < vm->global_count) {
        vm->globals[idx] = v;
    }
}

static void handle_print(VM* vm, uint32_t param) {
    (void)param;
    Value v = vm_pop(vm);


    if (v.tag == V_NUMBER) {

        if (v.lo == 0) {
            printf("%lld", (long long)v.hi);
        } else {
            __int128 frac128 = (__int128)v.lo * 10000000000ULL;
            uint64_t frac = (uint64_t)(frac128 >> 32);
            char frac_str[11];
            snprintf(frac_str, sizeof(frac_str), "%010llu", (unsigned long long)frac);
            char* end = frac_str + 9;
            while (end > frac_str && *end == '0') end--;
            *(end + 1) = '\0';
            printf("%lld.%s", (long long)v.hi, frac_str);
        }
    } else if (v.tag == V_STRING) {
        ByteArray* ba = (ByteArray*)v.hi;
        if (ba && ba->data && ba->length > 0) {
            char* s = (char*)ba->data;
            if (ba->length >= 4 && memcmp(s, UNICODE_MARKER, 4) == 0) {
                 // Unicode-строка: UTF-32 → UTF-8
                int count = (ba->length - 4) / 4;
                uint32_t* utf32 = (uint32_t*)(s + 4);
                for (int i = 0; i < count; i++) {
                    uint32_t cp = utf32[i];
                    if (cp < 0x80) putchar(cp);
                    else if (cp < 0x800) {
                        putchar(0xC0 | (cp >> 6));
                        putchar(0x80 | (cp & 0x3F));
                    } else if (cp < 0x10000) {
                        putchar(0xE0 | (cp >> 12));
                        putchar(0x80 | ((cp >> 6) & 0x3F));
                        putchar(0x80 | (cp & 0x3F));
                    } else {
                        putchar(0xF0 | (cp >> 18));
                        putchar(0x80 | ((cp >> 12) & 0x3F));
                        putchar(0x80 | ((cp >> 6) & 0x3F));
                        putchar(0x80 | (cp & 0x3F));
                    }
                }
            } else {
                printf("%s", s);
            }
        }
    } else if (v.tag == V_ARRAY) {
        ByteArray* ba = (ByteArray*)v.hi;
        if (ba && ba->data) {
            int count = ba->length / sizeof(Value);
            Value* elements = (Value*)ba->data;
            printf("[");
            for (int i = 0; i < count; i++) {
                if (i > 0) printf(", ");
                if (elements[i].tag == V_NUMBER) {
                    if (elements[i].lo == 0) {
                        printf("%lld", (long long)elements[i].hi);
                    } else {
                        __int128 frac128 = (__int128)v.lo * 10000000000ULL;
                        uint64_t frac = (uint64_t)(frac128 >> 32);
                        char frac_str[11];
                        snprintf(frac_str, sizeof(frac_str), "%010llu", (unsigned long long)frac);
                        char* end = frac_str + 9;
                        while (end > frac_str && *end == '0') end--;
                        *(end + 1) = '\0';
                        printf("%lld.%s", (long long)elements[i].hi, frac_str);
                    }
                } else if (elements[i].tag == V_STRING) {
                    ByteArray* s = (ByteArray*)elements[i].hi;
                    if (s && s->data) printf("%s", (char*)s->data);
                } else if (elements[i].tag == V_ARRAY) {
                    printf("[array]");
                } else {
                    printf("undef");
                }
            }
            printf("]");
        } else {
            printf("undef");
        }
    } else if (v.tag == V_HASH) {
        ByteArray* base = (ByteArray*)v.hi;
        if (base && base->data) {
            Value* parts = (Value*)base->data;
            int count = (int)parts[0].hi;

            if (count == 0) {
                printf("{}");
            } else {
                ByteArray* keys_ba = (parts[1].tag == V_ARRAY) ? (ByteArray*)parts[1].hi : NULL;
                ByteArray* vals_ba = (parts[2].tag == V_ARRAY) ? (ByteArray*)parts[2].hi : NULL;
                Value* keys = keys_ba ? (Value*)keys_ba->data : NULL;
                Value* vals = vals_ba ? (Value*)vals_ba->data : NULL;

                int has_keys = 0;
                if (keys) {
                    for (int i = 0; i < count; i++) {
                        if (keys[1 + i].tag != V_UNDEF) {
                            has_keys = 1;
                            break;
                        }
                    }
                }

                if (has_keys) {
                    printf("{");
                    for (int i = 0; i < count; i++) {
                        if (i > 0) printf(", ");
                        // Ключ
                        if (keys && keys[1 + i].tag == V_STRING) {
                            ByteArray* kba = (ByteArray*)keys[1 + i].hi;
                            if (kba && kba->data) printf("%s: ", (char*)kba->data);
                        }
                        // Значение
                        if (vals && vals[1 + i].tag == V_NUMBER) {
                            if (vals[1 + i].lo == 0) printf("%lld", (long long)vals[1 + i].hi);
                            else {
                                __int128 frac128 = (__int128)v.lo * 10000000000ULL;
                                uint64_t frac = (uint64_t)(frac128 >> 32);
                                printf("%lld.%08X", (long long)vals[1 + i].hi, vals[1 + i].lo);
                            }
                        } else if (vals && vals[1 + i].tag == V_STRING) {
                            ByteArray* s = (ByteArray*)vals[1 + i].hi;
                            if (s && s->data) printf("%s", (char*)s->data);
                        } else {
                            printf("undef");
                        }
                    }
                    printf("}");
                } else {
                    printf("[");
                    for (int i = 0; i < count; i++) {
                        if (i > 0) printf(", ");
                        if (vals && vals[1 + i].tag == V_NUMBER) {
                            if (vals[1 + i].lo == 0) printf("%lld", (long long)vals[1 + i].hi);
                            else printf("%lld.%08X", (long long)vals[1 + i].hi, vals[1 + i].lo);
                        } else if (vals && vals[1 + i].tag == V_STRING) {
                            ByteArray* s = (ByteArray*)vals[1 + i].hi;
                            if (s && s->data) printf("%s", (char*)s->data);
                        } else {
                            printf("undef");
                        }
                    }
                    printf("]");
                }
            }
        } else {
            printf("undef");
        }
    } else {
        printf("undef");
    }
    printf("\n");
    fflush(stdout);
}

// c — вызов функции (ПОЛНАЯ РЕАЛИЗАЦИЯ)
static void handle_call(VM* vm, uint32_t param) {
    int func_idx = (int)param;

    if (func_idx >= main_module.function_count) {
        fprintf(stderr, "VM: Function index %d out of range (max=%d)\n", func_idx, main_module.function_count);
        return;
    }

    char* func_name = main_module.functions[func_idx].name;
    char* module_name = main_module.functions[func_idx].module_name;
    LoadedModule* target_mod = NULL;
    int target_func_idx = -1;

    if (module_name[0] == '\0') {
        target_mod = &main_module;
        target_func_idx = func_idx;
    } else {
        target_mod = vm_load_module(vm, module_name);
        if (!target_mod) {
            fprintf(stderr, "VM: Cannot load module '%s'\n", module_name);
            return;
        }


        for (int i = 0; i < target_mod->function_count; i++) {
            if (strcmp(target_mod->functions[i].name, func_name) == 0) {
                target_func_idx = i;
                break;
            }
        }

        if (target_func_idx < 0) {
            fprintf(stderr, "VM: Function '%s' not found in module '%s'\n", func_name, module_name);
            return;
        }
    }



    CallFrame* new_frame = vm_create_frame(vm, target_mod, target_func_idx);

    vm->frame = new_frame;
    vm->code = new_frame->code;
    vm->code_size = new_frame->code_size;
    vm->ip = (uint8_t*)new_frame->code;
}

// y — return из функции
static void handle_return(VM* vm, uint32_t param) {
    (void)param;
    if (!vm->frame) {
        // Возврат из основного модуля — просто завершаем текущую функцию?
        // Основной модуль не должен иметь return. Просто выходим.
        vm->ip = (uint8_t*)(vm->code + vm->code_size);
        return;
    }

    Value ret_val = val_undef();
    if (vm->sp > vm->stack) {
        ret_val = vm_pop(vm);
    }

    CallFrame* old_frame = vm->frame;
    vm->frame = old_frame->prev;
    vm->ip = old_frame->return_ip;

    if (vm->frame) {
        // Возврат во внешнюю функцию
        vm->code = vm->frame->code;
        vm->code_size = vm->frame->code_size;
    } else {
        // Возврат в основной модуль
        vm->code = main_module.code_start;
        vm->code_size = main_module.code_size;
    }


    free(old_frame->locals);
    free(old_frame);

    vm_push(vm, ret_val);
}

// Арифметика
static void handle_add(VM* vm, uint32_t param) {
    (void)param;
    Value b = vm_pop(vm), a = vm_pop(vm);

    // Если обе строки — конкатенация
    if (a.tag == V_STRING && b.tag == V_STRING) {
        ByteArray* ba = (ByteArray*)a.hi;
        ByteArray* bb = (ByteArray*)b.hi;

        char* sa = (ba && ba->data) ? (char*)ba->data : "";
        char* sb = (bb && bb->data) ? (char*)bb->data : "";

        int len_a = strlen(sa);
        int len_b = strlen(sb);

        char* result = malloc(len_a + len_b + 1);
        memcpy(result, sa, len_a);
        memcpy(result + len_a, sb, len_b + 1);

        ByteArray* result_ba = malloc(sizeof(ByteArray));
        result_ba->data = (uint8_t*)result;
        result_ba->length = len_a + len_b;

        vm_push(vm, val_string(result_ba));
        return;
    }

    // Иначе — числовое сложение
    if (a.tag == V_NUMBER && b.tag == V_NUMBER) {
        uint64_t sum_lo = (uint64_t)a.lo + b.lo;
        vm_push(vm, val_number(a.hi + b.hi + (sum_lo >> 32), (uint32_t)(sum_lo & 0xFFFFFFFF)));
    } else {
        vm_push(vm, val_undef());
    }
}

static void handle_sub(VM* vm, uint32_t param) {
    (void)param;
    Value b = vm_pop(vm), a = vm_pop(vm);
    if (a.tag == V_NUMBER && b.tag == V_NUMBER) {
        uint64_t diff_lo = (uint64_t)a.lo - b.lo;
        vm_push(vm, val_number(a.hi - b.hi - ((diff_lo >> 32) & 1), (uint32_t)(diff_lo & 0xFFFFFFFF)));
    } else {
        vm_push(vm, val_undef());
    }
}

static void handle_mul(VM* vm, uint32_t param) {
    (void)param;
    Value b = vm_pop(vm), a = vm_pop(vm);
    if (a.tag == V_NUMBER && b.tag == V_NUMBER) {
        if (a.lo == 0 && b.lo == 0) {
            vm_push(vm, val_number(a.hi * b.hi, 0));
        } else {
            __int128 af = ((__int128)a.hi << 32) | a.lo;
            __int128 bf = ((__int128)b.hi << 32) | b.lo;
            __int128 prod = af * bf;
            vm_push(vm, val_number((int64_t)(prod >> 64), (uint32_t)((prod >> 32) & 0xFFFFFFFF)));
        }
    } else {
        vm_push(vm, val_undef());
    }
}

static void handle_div(VM* vm, uint32_t param) {
    (void)param;
    Value b = vm_pop(vm), a = vm_pop(vm);
    if (a.tag == V_NUMBER && b.tag == V_NUMBER) {
        if (b.hi == 0 && b.lo == 0) { vm_push(vm, val_undef()); return; }
        __int128 af = ((__int128)a.hi << 32) | a.lo;
        __int128 bf = ((__int128)b.hi << 32) | b.lo;
        __int128 scaled = af << 32;
        __int128 quot = scaled / bf;
        vm_push(vm, val_number((int64_t)(quot >> 32), (uint32_t)(quot & 0xFFFFFFFF)));
    } else {
        vm_push(vm, val_undef());
    }
}

static void handle_mod(VM* vm, uint32_t param) {
    (void)param;
    Value b = vm_pop(vm), a = vm_pop(vm);
    if (a.tag == V_NUMBER && b.tag == V_NUMBER) {
        if (b.hi == 0) { vm_push(vm, val_undef()); return; }
        vm_push(vm, val_number(a.hi % b.hi, 0));
    } else {
        vm_push(vm, val_undef());
    }
}

// Сравнения
static void handle_equal(VM* vm, uint32_t param) {
    (void)param;
    Value b = vm_pop(vm), a = vm_pop(vm);
    if (a.tag == V_NUMBER && b.tag == V_NUMBER)
        vm_push(vm, val_number(a.hi == b.hi && a.lo == b.lo ? 1 : 0, 0));
    else if (a.tag == V_STRING && b.tag == V_STRING) {
        ByteArray* sa = (ByteArray*)a.hi, *sb = (ByteArray*)b.hi;
        vm_push(vm, val_number(strcmp((char*)sa->data, (char*)sb->data) == 0 ? 1 : 0, 0));
    } else vm_push(vm, val_undef());
}

static void handle_not_equal(VM* vm, uint32_t param) {
    (void)param;
    Value b = vm_pop(vm), a = vm_pop(vm);
    if (a.tag == V_NUMBER && b.tag == V_NUMBER)
        vm_push(vm, val_number(a.hi != b.hi || a.lo != b.lo ? 1 : 0, 0));
    else if (a.tag == V_STRING && b.tag == V_STRING) {
        ByteArray* sa = (ByteArray*)a.hi, *sb = (ByteArray*)b.hi;
        vm_push(vm, val_number(strcmp((char*)sa->data, (char*)sb->data) != 0 ? 1 : 0, 0));
    } else vm_push(vm, val_undef());
}

static void handle_less(VM* vm, uint32_t param) {
    (void)param;
    Value b = vm_pop(vm), a = vm_pop(vm);
    if (a.tag == V_NUMBER && b.tag == V_NUMBER)
        vm_push(vm, val_number(a.hi < b.hi || (a.hi == b.hi && a.lo < b.lo) ? 1 : 0, 0));
    else if (a.tag == V_STRING && b.tag == V_STRING) {
        ByteArray* sa = (ByteArray*)a.hi, *sb = (ByteArray*)b.hi;
        vm_push(vm, val_number(strcmp((char*)sa->data, (char*)sb->data) < 0 ? 1 : 0, 0));
    } else vm_push(vm, val_undef());
}

static void handle_greater(VM* vm, uint32_t param) {
    (void)param;
    Value b = vm_pop(vm), a = vm_pop(vm);
    if (a.tag == V_NUMBER && b.tag == V_NUMBER)
        vm_push(vm, val_number(a.hi > b.hi || (a.hi == b.hi && a.lo > b.lo) ? 1 : 0, 0));
    else if (a.tag == V_STRING && b.tag == V_STRING) {
        ByteArray* sa = (ByteArray*)a.hi, *sb = (ByteArray*)b.hi;
        vm_push(vm, val_number(strcmp((char*)sa->data, (char*)sb->data) > 0 ? 1 : 0, 0));
    } else vm_push(vm, val_undef());
}

static void handle_less_equal(VM* vm, uint32_t param) {
    (void)param;
    Value b = vm_pop(vm), a = vm_pop(vm);
    if (a.tag == V_NUMBER && b.tag == V_NUMBER) {
        int result = (a.hi < b.hi || (a.hi == b.hi && a.lo <= b.lo)) ? 1 : 0;

        vm_push(vm, val_number(result, 0));
    } else if (a.tag == V_STRING && b.tag == V_STRING) {
        ByteArray* sa = (ByteArray*)a.hi, *sb = (ByteArray*)b.hi;
        vm_push(vm, val_number(strcmp((char*)sa->data, (char*)sb->data) <= 0 ? 1 : 0, 0));
    } else vm_push(vm, val_undef());
}

static void handle_greater_equal(VM* vm, uint32_t param) {
    (void)param;
    Value b = vm_pop(vm), a = vm_pop(vm);
    if (a.tag == V_NUMBER && b.tag == V_NUMBER)
        vm_push(vm, val_number(a.hi > b.hi || (a.hi == b.hi && a.lo >= b.lo) ? 1 : 0, 0));
    else if (a.tag == V_STRING && b.tag == V_STRING) {
        ByteArray* sa = (ByteArray*)a.hi, *sb = (ByteArray*)b.hi;
        vm_push(vm, val_number(strcmp((char*)sa->data, (char*)sb->data) >= 0 ? 1 : 0, 0));
    } else vm_push(vm, val_undef());
}

// Переходы
static void handle_jump(VM* vm, uint32_t param) {
    vm->ip = (uint8_t*)(vm->code + (int)param);
}

static void handle_jump_if_false(VM* vm, uint32_t param) {
    Value v = vm_pop(vm);
    if (!is_truthy(v)) vm->ip = (uint8_t*)(vm->code + (int)param);
}


// S — создание массива из элементов на стеке
static void handle_array_create(VM* vm, uint32_t param) {
    int count = (int)param;

    if (count <= 0) {
        vm_push(vm, val_undef());
        return;
    }

    // Собираем элементы со стека (в обратном порядке)
    Value* elements = malloc(count * sizeof(Value));
    for (int i = count - 1; i >= 0; i--) {
        elements[i] = vm_pop(vm);
    }

    // Создаём ByteArray
    ByteArray* ba = malloc(sizeof(ByteArray));
    ba->length = count * sizeof(Value);
    ba->data = malloc(ba->length);
    memcpy(ba->data, elements, ba->length);

    free(elements);

    vm_push(vm, val_array(ba));
}

// A — доступ к элементу массива по индексу @arr[i]
static void handle_array_index(VM* vm, uint32_t param) {
    (void)param;

    Value index_val = vm_pop(vm);
    Value arr_val = vm_pop(vm);

    if (arr_val.tag != V_ARRAY || index_val.tag != V_NUMBER) {
        vm_push(vm, val_undef());
        return;
    }

    ByteArray* ba = (ByteArray*)arr_val.hi;
    int index = (int)index_val.hi;
    int count = ba->length / sizeof(Value);

    if (index < 0 || index >= count) {
        vm_push(vm, val_undef());
        return;
    }

    Value* elements = (Value*)ba->data;
    vm_push(vm, elements[index]);
}

// L — длина массива len(@arr)
// L — длина массива len(@arr) или строки len($s)
static void handle_length(VM* vm, uint32_t param) {
    (void)param;

    Value v = vm_pop(vm);

    if (v.tag == V_ARRAY) {
        ByteArray* ba = (ByteArray*)v.hi;
        int count = ba->length / sizeof(Value);
        vm_push(vm, val_number(count, 0));
    } else if (v.tag == V_STRING) {
        ByteArray* ba = (ByteArray*)v.hi;
        if (ba && ba->data) {
            char* s = (char*)ba->data;
            if (ba->length >= 4 && memcmp(s, UNICODE_MARKER, 4) == 0) {
                int count = (ba->length - 4) / 4;
                vm_push(vm, val_number(count, 0));
            } else {
                vm_push(vm, val_number(ba->length, 0));
            }
        } else {
            vm_push(vm, val_number(0, 0));
        }
    } else if (v.tag == V_HASH) {
        ByteArray* base = (ByteArray*)v.hi;
        if (base && base->data) {
            Value* parts = (Value*)base->data;
            vm_push(vm, val_number(parts[0].hi, 0));
        } else {
            vm_push(vm, val_number(0, 0));
        }
    } else {
        vm_push(vm, val_undef());
    }
}

// M — присваивание элементу массива @arr[$i] = value
static void handle_array_store(VM* vm, uint32_t param) {
    (void)param;

    Value val = vm_pop(vm);       // значение
    Value index_val = vm_pop(vm); // индекс
    Value arr_val = vm_pop(vm);   // массив

    if (arr_val.tag != V_ARRAY || index_val.tag != V_NUMBER) {
        return;
    }

    ByteArray* ba = (ByteArray*)arr_val.hi;
    int index = (int)index_val.hi;
    int count = ba->length / sizeof(Value);

    if (index < 0 || index >= count) {
        return;  // выход за границы — игнорируем
    }

    Value* elements = (Value*)ba->data;
    elements[index] = val;
}

// [ — создание массива заданной длины array($n)
static void handle_array_new(VM* vm, uint32_t param) {
    (void)param;

    Value count_val = vm_pop(vm);

    if (count_val.tag != V_NUMBER || count_val.hi < 0) {
        vm_push(vm, val_undef());
        return;
    }

    int count = (int)count_val.hi;

    if (count <= 0) {
        vm_push(vm, val_undef());
        return;
    }

    ByteArray* ba = malloc(sizeof(ByteArray));
    ba->length = count * sizeof(Value);
    ba->data = calloc(count, sizeof(Value));

    Value* elements = (Value*)ba->data;
    for (int i = 0; i < count; i++) {
        elements[i] = val_undef();
    }

    vm_push(vm, val_array(ba));
}

// N — clone(x) — клонирование массива или хеша
static void handle_clone(VM* vm, uint32_t param) {
    (void)param;

    Value v = vm_pop(vm);

    if (v.tag == V_ARRAY) {
        ByteArray* src = (ByteArray*)v.hi;
        ByteArray* dst = malloc(sizeof(ByteArray));
        dst->length = src->length;
        dst->data = malloc(src->length);
        memcpy(dst->data, src->data, src->length);
        vm_push(vm, val_array(dst));
    } else if (v.tag == V_HASH) {
        ByteArray* src_base = (ByteArray*)v.hi;
        if (!src_base || !src_base->data) {
            vm_push(vm, val_undef());
            return;
        }

        Value* src_parts = (Value*)src_base->data;
        int count = (int)src_parts[0].hi;

        // Копируем ключи
        ByteArray* src_keys = (ByteArray*)src_parts[1].hi;
        ByteArray* dst_keys = malloc(sizeof(ByteArray));
        dst_keys->length = src_keys->length;
        dst_keys->data = malloc(src_keys->length);
        memcpy(dst_keys->data, src_keys->data, src_keys->length);

        // Копируем значения
        ByteArray* src_vals = (ByteArray*)src_parts[2].hi;
        ByteArray* dst_vals = malloc(sizeof(ByteArray));
        dst_vals->length = src_vals->length;
        dst_vals->data = malloc(src_vals->length);
        memcpy(dst_vals->data, src_vals->data, src_vals->length);

        // Создаём новый хеш
        ByteArray* dst_base = malloc(sizeof(ByteArray));
        dst_base->length = 3 * sizeof(Value);
        dst_base->data = malloc(3 * sizeof(Value));
        Value* dst_parts = (Value*)dst_base->data;
        dst_parts[0] = val_number(count, 0);
        dst_parts[1] = val_array(dst_keys);
        dst_parts[2] = val_array(dst_vals);
        Value result = val_hash(dst_base);
        vm_push(vm, result);
        return;

    } else if (v.tag == V_STRING) {
        ByteArray* ba = (ByteArray*)v.hi;
        if (ba && ba->data && ba->length > 0) {
            char* s = (char*)ba->data;

            // Проверяем маркер Unicode
            if (ba->length >= 4 && memcmp(s, UNICODE_MARKER, 4) == 0) {
                // UTF-32 → UTF-8 для вывода
                int char_count = (ba->length - 4) / 4;
                uint32_t* utf32 = (uint32_t*)(s + 4);
                for (int i = 0; i < char_count; i++) {
                    uint32_t cp = utf32[i];

                    if (cp < 0x80) {
                        putchar(cp);
                    } else if (cp < 0x800) {
                        putchar(0xC0 | (cp >> 6));
                        putchar(0x80 | (cp & 0x3F));
                    } else if (cp < 0x10000) {
                        putchar(0xE0 | (cp >> 12));
                        putchar(0x80 | ((cp >> 6) & 0x3F));
                        putchar(0x80 | (cp & 0x3F));
                    } else {
                        putchar(0xF0 | (cp >> 18));
                        putchar(0x80 | ((cp >> 12) & 0x3F));
                        putchar(0x80 | ((cp >> 6) & 0x3F));
                        putchar(0x80 | (cp & 0x3F));
                    }
                }
            } else {
                printf("%s", s);
            }
        }
    }
}

// x — deallocate(x) — освобождение памяти, возвращает undef
static void handle_deallocate(VM* vm, uint32_t param) {
    (void)param;

    Value v = vm_pop(vm);

    if (v.tag == V_ARRAY) {
        ByteArray* ba = (ByteArray*)v.hi;
        if (ba) {
            if (ba->data) free(ba->data);
            free(ba);
        }
    } else if (v.tag == V_STRING) {
        ByteArray* ba = (ByteArray*)v.hi;
        if (ba) {
            if (ba->data) free(ba->data);
            free(ba);
        }
    } else if (v.tag == V_HASH) {
        ByteArray* base = (ByteArray*)v.hi;
        if (base) {
            Value* parts = (Value*)base->data;
            ByteArray* keys = (ByteArray*)parts[1].hi;
            ByteArray* vals = (ByteArray*)parts[2].hi;
            if (keys) { free(keys->data); free(keys); }
            if (vals) { free(vals->data); free(vals); }
            free(base->data);
            free(base);
        }
    }

    vm_push(vm, val_undef());
}


// R — reverse(@arr) — переворот массива
static void handle_reverse(VM* vm, uint32_t param) {
    (void)param;

    Value v = vm_pop(vm);

    if (v.tag != V_ARRAY) {
        vm_push(vm, val_undef());
        return;
    }

    ByteArray* src = (ByteArray*)v.hi;
    int count = src->length / sizeof(Value);

    ByteArray* dst = malloc(sizeof(ByteArray));
    dst->length = src->length;
    dst->data = malloc(src->length);

    Value* sv = (Value*)src->data;
    Value* dv = (Value*)dst->data;

    for (int i = 0; i < count; i++) {
        dv[i] = sv[count - 1 - i];
    }

    vm_push(vm, val_array(dst));
}


// Сравнение двух значений для сортировки
static int value_less_than(Value a, Value b) {
    // UNDEF меньше всего
    if (a.tag == V_UNDEF && b.tag != V_UNDEF) return 1;
    if (b.tag == V_UNDEF && a.tag != V_UNDEF) return 0;
    if (a.tag == V_UNDEF && b.tag == V_UNDEF) return 0;

    // Числа идут перед строками
    if (a.tag == V_NUMBER && b.tag != V_NUMBER) return 1;
    if (b.tag == V_NUMBER && a.tag != V_NUMBER) return 0;

    // Строки идут перед массивами
    if (a.tag == V_STRING && b.tag != V_STRING) return 1;
    if (b.tag == V_STRING && a.tag != V_STRING) return 0;

    // Сравнение чисел
    if (a.tag == V_NUMBER && b.tag == V_NUMBER) {
        return (a.hi < b.hi) || (a.hi == b.hi && a.lo < b.lo);
    }

    // Сравнение строк
    if (a.tag == V_STRING && b.tag == V_STRING) {
        ByteArray* sa = (ByteArray*)a.hi;
        ByteArray* sb = (ByteArray*)b.hi;
        if (!sa || !sa->data || !sb || !sb->data) return 0;
        return strcmp((char*)sa->data, (char*)sb->data) < 0;
    }

    // Массивы сравниваем по длине
    if (a.tag == V_ARRAY && b.tag == V_ARRAY) {
        ByteArray* ba = (ByteArray*)a.hi;
        ByteArray* bb = (ByteArray*)b.hi;
        if (!ba || !bb) return 0;
        return ba->length < bb->length;
    }

    return 0;
}

// O — sort(@arr) — сортировка массива
static void handle_sort(VM* vm, uint32_t param) {

    (void)param;

    Value v = vm_pop(vm);
    //fprintf(stderr, "DEBUG sort: tag=%d\n", v.tag);
    if (v.tag != V_ARRAY) {
        vm_push(vm, val_undef());
        return;
    }

    ByteArray* ba = (ByteArray*)v.hi;
    int count = ba->length / sizeof(Value);

    // Копируем массив
    ByteArray* dst = malloc(sizeof(ByteArray));
    dst->length = ba->length;
    dst->data = malloc(ba->length);
    memcpy(dst->data, ba->data, ba->length);

    Value* elements = (Value*)dst->data;

    // Пузырьковая сортировка
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (!value_less_than(elements[j], elements[j + 1])) {
                // Меняем местами, если не меньше (т.е. больше или равно)
                if (value_less_than(elements[j + 1], elements[j])) {
                    Value tmp = elements[j];
                    elements[j] = elements[j + 1];
                    elements[j + 1] = tmp;
                }
            }
        }
    }

    vm_push(vm, val_array(dst));
}

// U — загрузить undef
static void handle_undef(VM* vm, uint32_t param) {
    (void)param;
    vm_push(vm, val_undef());
}

// z — lc(s) — строка в нижний регистр
static void handle_lc(VM* vm, uint32_t param) {
    (void)param;

    Value v = vm_pop(vm);

    if (v.tag != V_STRING) {
        vm_push(vm, val_undef());
        return;
    }

    ByteArray* src = (ByteArray*)v.hi;
    if (!src || !src->data) {
        vm_push(vm, val_undef());
        return;
    }

    char* orig = (char*)src->data;
    int slen = strlen(orig);
    char* s = malloc(slen + 1);
    memcpy(s, orig, slen + 1);
    for (int i = 0; s[i]; i++) s[i] = tolower((unsigned char)s[i]);

    ByteArray* ba = malloc(sizeof(ByteArray));
    ba->data = (uint8_t*)s;
    ba->length = strlen(s);

    vm_push(vm, val_string(ba));
}

// Z — uc(s) — строка в верхний регистр
static void handle_uc(VM* vm, uint32_t param) {
    (void)param;

    Value v = vm_pop(vm);

    if (v.tag != V_STRING) {
        vm_push(vm, val_undef());
        return;
    }

    ByteArray* src = (ByteArray*)v.hi;
    if (!src || !src->data) {
        vm_push(vm, val_undef());
        return;
    }

    char* orig = (char*)src->data;
    int slen = strlen(orig);
    char* s = malloc(slen + 1);
    memcpy(s, orig, slen + 1);
    for (int i = 0; s[i]; i++) s[i] = toupper((unsigned char)s[i]);

    ByteArray* ba = malloc(sizeof(ByteArray));
    ba->data = (uint8_t*)s;
    ba->length = strlen(s);

    vm_push(vm, val_string(ba));
}

// 5 — chr(n) — код в символ
static void handle_chr(VM* vm, uint32_t param) {
    (void)param;

    Value v = vm_pop(vm);

    if (v.tag != V_NUMBER) {
        vm_push(vm, val_undef());
        return;
    }

    char* s = malloc(2);
    s[0] = (char)(int)v.hi;
    s[1] = '\0';

    ByteArray* ba = malloc(sizeof(ByteArray));
    ba->data = (uint8_t*)s;
    ba->length = 1;

    vm_push(vm, val_string(ba));
}

// 6 — ord(c) — символ в код
static void handle_ord(VM* vm, uint32_t param) {
    (void)param;

    Value v = vm_pop(vm);

    if (v.tag != V_STRING) {
        vm_push(vm, val_undef());
        return;
    }

    ByteArray* ba = (ByteArray*)v.hi;
    if (!ba || !ba->data || ba->length == 0) {
        vm_push(vm, val_undef());
        return;
    }

    vm_push(vm, val_number((unsigned char)((char*)ba->data)[0], 0));
}


// K — chomp(s) — убрать \n в конце
static void handle_chomp(VM* vm, uint32_t param) {
    (void)param;

    Value v = vm_pop(vm);

    if (v.tag != V_STRING) {
        vm_push(vm, val_undef());
        return;
    }

    ByteArray* src = (ByteArray*)v.hi;
    if (!src || !src->data) {
        vm_push(vm, val_undef());
        return;
    }

    int is_unicode = (src->length >= 4 && memcmp(src->data, UNICODE_MARKER, 4) == 0);

    if (is_unicode) {
        int char_count = (src->length - 4) / 4;
        uint32_t* str32 = (uint32_t*)(src->data + 4);

        if (char_count > 0 && str32[char_count - 1] == '\n') {
            int new_len = 4 + (char_count - 1) * 4;
            char* s = malloc(new_len + 1);
            memcpy(s, src->data, new_len);
            s[new_len] = '\0';
            ByteArray* ba = malloc(sizeof(ByteArray));
            ba->data = (uint8_t*)s;
            ba->length = new_len;
            vm_push(vm, val_string(ba));
        } else {
            char* s = malloc(src->length + 1);
            memcpy(s, src->data, src->length);
            s[src->length] = '\0';
            ByteArray* ba = malloc(sizeof(ByteArray));
            ba->data = (uint8_t*)s;
            ba->length = src->length;
            vm_push(vm, val_string(ba));
        }
        return;
    }

    // Обычный ASCII
    char* orig = (char*)src->data;
    int slen = strlen(orig);
    char* s = malloc(slen + 1);
    memcpy(s, orig, slen + 1);

    if (slen > 0 && s[slen - 1] == '\n') {
        s[slen - 1] = '\0';
        slen--;
    }

    ByteArray* ba = malloc(sizeof(ByteArray));
    ba->data = (uint8_t*)s;
    ba->length = slen;
    vm_push(vm, val_string(ba));

}

// t — chop(s) — убрать последний символ
static void handle_chop(VM* vm, uint32_t param) {
    (void)param;
    Value v = vm_pop(vm);
    if (v.tag != V_STRING) { vm_push(vm, val_undef()); return; }

    ByteArray* src = (ByteArray*)v.hi;
    if (!src || !src->data) { vm_push(vm, val_undef()); return; }

    int is_unicode = (src->length >= 4 && memcmp(src->data, UNICODE_MARKER, 4) == 0);

    if (is_unicode) {
        int char_count = (src->length - 4) / 4;
        if (char_count <= 1) {
            // Пустая строка или 1 символ — возвращаем пустую Unicode-строку
            char* result = malloc(5);
            memcpy(result, UNICODE_MARKER, 4);
            result[4] = '\0';
            ByteArray* ba = malloc(sizeof(ByteArray));
            ba->data = (uint8_t*)result;
            ba->length = 4;
            vm_push(vm, val_string(ba));
        } else {
            int new_len = 4 + (char_count - 1) * 4;
            char* result = malloc(new_len + 1);
            memcpy(result, src->data, new_len);
            result[new_len] = '\0';
            ByteArray* ba = malloc(sizeof(ByteArray));
            ba->data = (uint8_t*)result;
            ba->length = new_len;
            vm_push(vm, val_string(ba));
        }
    } else {
        // ASCII
        char* orig = (char*)src->data;
        int slen = strlen(orig);
        char* s = malloc(slen + 1);
        memcpy(s, orig, slen + 1);
        if (slen > 0) s[slen - 1] = '\0';
        ByteArray* ba = malloc(sizeof(ByteArray));
        ba->data = (uint8_t*)s;
        ba->length = strlen(s);
        vm_push(vm, val_string(ba));
    }
}

// 1 — lcfirst(s) — первый символ в нижний регистр
static void handle_lcfirst(VM* vm, uint32_t param) {
    (void)param;

    Value v = vm_pop(vm);

    if (v.tag != V_STRING) {
        vm_push(vm, val_undef());
        return;
    }

    ByteArray* src = (ByteArray*)v.hi;
    if (!src || !src->data) {
        vm_push(vm, val_undef());
        return;
    }

    char* orig = (char*)src->data;
    int slen = strlen(orig);
    char* s = malloc(slen + 1);
    memcpy(s, orig, slen + 1);

    if (s[0]) s[0] = tolower((unsigned char)s[0]);

    ByteArray* ba = malloc(sizeof(ByteArray));
    ba->data = (uint8_t*)s;
    ba->length = slen;

    vm_push(vm, val_string(ba));
}

// 2 — ucfirst(s) — первый символ в верхний регистр
static void handle_ucfirst(VM* vm, uint32_t param) {
    (void)param;

    Value v = vm_pop(vm);

    if (v.tag != V_STRING) {
        vm_push(vm, val_undef());
        return;
    }

    ByteArray* src = (ByteArray*)v.hi;
    if (!src || !src->data) {
        vm_push(vm, val_undef());
        return;
    }

    char* orig = (char*)src->data;
    int slen = strlen(orig);
    char* s = malloc(slen + 1);
    memcpy(s, orig, slen + 1);

    if (s[0]) s[0] = toupper((unsigned char)s[0]);

    ByteArray* ba = malloc(sizeof(ByteArray));
    ba->data = (uint8_t*)s;
    ba->length = slen;

    vm_push(vm, val_string(ba));
}
/*
// w — index(s, sub, pos?) — поиск подстроки
static void handle_index(VM* vm, uint32_t param) {
    int arg_count = (int)param;

    int pos = 0;
    if (arg_count == 3) {
        Value pos_val = vm_pop(vm);
        if (pos_val.tag == V_NUMBER) pos = (int)pos_val.hi;
    }

    Value sub_val = vm_pop(vm);
    Value str_val = vm_pop(vm);

    if (str_val.tag != V_STRING || sub_val.tag != V_STRING) {
        vm_push(vm, val_number(-1, 0));
        return;
    }

    ByteArray* str_ba = (ByteArray*)str_val.hi;
    ByteArray* sub_ba = (ByteArray*)sub_val.hi;

    if (!str_ba || !str_ba->data || !sub_ba || !sub_ba->data) {
        vm_push(vm, val_number(-1, 0));
        return;
    }

    char* s = (char*)str_ba->data;
    char* sub = (char*)sub_ba->data;
    int slen = strlen(s);

    if (pos < 0) pos = 0;
    if (pos > slen) pos = slen;

    char* found = strstr(s + pos, sub);

    if (found) {
        vm_push(vm, val_number(found - s, 0));
    } else {
        vm_push(vm, val_number(-1, 0));
    }
}*/

// w — index(s, sub, pos?) — поиск подстроки
static void handle_str_index(VM* vm, uint32_t param) {
    int arg_count = (int)param;

    int pos = 0;
    if (arg_count == 3) {
        Value pos_val = vm_pop(vm);
        if (pos_val.tag == V_NUMBER) pos = (int)pos_val.hi;
    }

    Value sub_val = vm_pop(vm);
    Value str_val = vm_pop(vm);

    if (str_val.tag != V_STRING || sub_val.tag != V_STRING) {
        vm_push(vm, val_number(-1, 0));
        return;
    }

    ByteArray* str_ba = (ByteArray*)str_val.hi;
    ByteArray* sub_ba = (ByteArray*)sub_val.hi;

    if (!str_ba || !str_ba->data || !sub_ba || !sub_ba->data) {
        vm_push(vm, val_number(-1, 0));
        return;
    }

    // Проверяем Unicode-маркер
    int str_is_unicode = (str_ba->length >= 4 && memcmp(str_ba->data, UNICODE_MARKER, 4) == 0);
    int sub_is_unicode = (sub_ba->length >= 4 && memcmp(sub_ba->data, UNICODE_MARKER, 4) == 0);

    if (str_is_unicode && sub_is_unicode) {
        // UTF-32 поиск
        int str_count = (str_ba->length - 4) / 4;
        int sub_count = (sub_ba->length - 4) / 4;
        uint32_t* str32 = (uint32_t*)(str_ba->data + 4);
        uint32_t* sub32 = (uint32_t*)(sub_ba->data + 4);

        if (pos < 0) pos = 0;
        if (pos > str_count) pos = str_count;

        for (int i = pos; i <= str_count - sub_count; i++) {
            int match = 1;
            for (int j = 0; j < sub_count; j++) {
                if (str32[i + j] != sub32[j]) {
                    match = 0;
                    break;
                }
            }
            if (match) {
                vm_push(vm, val_number(i, 0));
                return;
            }
        }
        vm_push(vm, val_number(-1, 0));
    } else if (!str_is_unicode && !sub_is_unicode) {
        // Обычный ASCII поиск
        char* s = (char*)str_ba->data;
        char* sub = (char*)sub_ba->data;
        int slen = strlen(s);

        if (pos < 0) pos = 0;
        if (pos > slen) pos = slen;

        char* found = strstr(s + pos, sub);
        if (found) {
            vm_push(vm, val_number(found - s, 0));
        } else {
            vm_push(vm, val_number(-1, 0));
        }
    } else {
        // Смешанный поиск — не поддерживается
        vm_push(vm, val_number(-1, 0));
    }
}

// W — rindex(s, sub, pos?) — поиск подстроки справа
static void handle_str_rindex(VM* vm, uint32_t param) {
    int arg_count = (int)param;

    int pos = -1;
    if (arg_count == 3) {
        Value pos_val = vm_pop(vm);
        if (pos_val.tag == V_NUMBER) pos = (int)pos_val.hi;
    }

    Value sub_val = vm_pop(vm);
    Value str_val = vm_pop(vm);

    if (str_val.tag != V_STRING || sub_val.tag != V_STRING) {
        vm_push(vm, val_number(-1, 0));
        return;
    }

    ByteArray* str_ba = (ByteArray*)str_val.hi;
    ByteArray* sub_ba = (ByteArray*)sub_val.hi;

    if (!str_ba || !str_ba->data || !sub_ba || !sub_ba->data) {
        vm_push(vm, val_number(-1, 0));
        return;
    }

    int str_is_unicode = (str_ba->length >= 4 && memcmp(str_ba->data, UNICODE_MARKER, 4) == 0);
    int sub_is_unicode = (sub_ba->length >= 4 && memcmp(sub_ba->data, UNICODE_MARKER, 4) == 0);

    if (str_is_unicode && sub_is_unicode) {
        int str_count = (str_ba->length - 4) / 4;
        int sub_count = (sub_ba->length - 4) / 4;
        uint32_t* str32 = (uint32_t*)(str_ba->data + 4);
        uint32_t* sub32 = (uint32_t*)(sub_ba->data + 4);

        if (pos < 0 || pos > str_count) pos = str_count;

        int found = -1;
        for (int i = 0; i <= pos - sub_count; i++) {
            int match = 1;
            for (int j = 0; j < sub_count; j++) {
                if (str32[i + j] != sub32[j]) { match = 0; break; }
            }
            if (match) found = i;
        }
        vm_push(vm, val_number(found, 0));
    } else if (!str_is_unicode && !sub_is_unicode) {
        char* s = (char*)str_ba->data;
        char* sub = (char*)sub_ba->data;
        int slen = strlen(s);
        int sublen = strlen(sub);

        if (pos < 0 || pos > slen) pos = slen;

        int found = -1;
        for (int i = 0; i <= pos - sublen; i++) {
            if (strncmp(s + i, sub, sublen) == 0) found = i;
        }
        vm_push(vm, val_number(found, 0));
    } else {
        vm_push(vm, val_number(-1, 0));
    }
}

// k — substr(s, off, len, repl?) — извлечение/замена подстроки
static void handle_substr(VM* vm, uint32_t param) {
    int arg_count = (int)param;

    Value repl_val;
    int has_repl = (arg_count == 4);

    if (has_repl) {
        repl_val = vm_pop(vm);
    }

    Value len_val = vm_pop(vm);
    Value off_val = vm_pop(vm);
    Value str_val = vm_pop(vm);

    if (str_val.tag != V_STRING || off_val.tag != V_NUMBER || len_val.tag != V_NUMBER) {
        vm_push(vm, val_undef());
        return;
    }

    ByteArray* str_ba = (ByteArray*)str_val.hi;
    if (!str_ba || !str_ba->data) {
        vm_push(vm, val_undef());
        return;
    }
    // Проверяем Unicode-маркер
    int is_unicode = (str_ba->length >= 4 && memcmp(str_ba->data, UNICODE_MARKER, 4) == 0);

    if (is_unicode) {
        int char_count = (str_ba->length - 4) / 4;
        uint32_t* str32 = (uint32_t*)(str_ba->data + 4);

        int off = (int)off_val.hi;
        int len = (int)len_val.hi;

        if (off < 0) off = 0;
        if (off > char_count) off = char_count;
        if (len < 0) len = 0;
        if (off + len > char_count) len = char_count - off;

        if (has_repl) {
            if (repl_val.tag != V_STRING) { vm_push(vm, val_undef()); return; }
            ByteArray* repl_ba = (ByteArray*)repl_val.hi;
            if (!repl_ba || !repl_ba->data) { vm_push(vm, val_undef()); return; }

            int repl_is_unicode = (repl_ba->length >= 4 && memcmp(repl_ba->data, UNICODE_MARKER, 4) == 0);
            int repl_count = repl_is_unicode ? (repl_ba->length - 4) / 4 : repl_ba->length;

            int new_count = char_count - len + repl_count;
            char* result = malloc(4 + new_count * 4 + 1);
            memcpy(result, UNICODE_MARKER, 4);
            uint32_t* res32 = (uint32_t*)(result + 4);

            memcpy(res32, str32, off * 4);
            if (repl_is_unicode) {
                memcpy(res32 + off, repl_ba->data + 4, repl_count * 4);
            } else {
                for (int i = 0; i < repl_count; i++) {
                    res32[off + i] = (unsigned char)repl_ba->data[i];
                }
            }
            memcpy(res32 + off + repl_count, str32 + off + len, (char_count - off - len) * 4);

            ByteArray* ba = malloc(sizeof(ByteArray));
            ba->data = (uint8_t*)result;
            ba->length = 4 + new_count * 4;
            vm_push(vm, val_string(ba));
        } else {
            char* result = malloc(4 + len * 4 + 1);
            memcpy(result, UNICODE_MARKER, 4);
            memcpy(result + 4, str32 + off, len * 4);

            ByteArray* ba = malloc(sizeof(ByteArray));
            ba->data = (uint8_t*)result;
            ba->length = 4 + len * 4;
            vm_push(vm, val_string(ba));
        }
        return;
    }
    // Обычный ASCII
    char* s = (char*)str_ba->data;
    int slen = strlen(s);
    int off = (int)off_val.hi;
    int len = (int)len_val.hi;

    if (off < 0) off = 0;
    if (off > slen) off = slen;
    if (len < 0) len = 0;
    if (off + len > slen) len = slen - off;

    if (has_repl) {
        // Замена: возвращаем новую строку с заменённой подстрокой
        if (repl_val.tag != V_STRING) {
            vm_push(vm, val_undef());
            return;
        }
        ByteArray* repl_ba = (ByteArray*)repl_val.hi;
        char* repl = repl_ba && repl_ba->data ? (char*)repl_ba->data : "";
        int replen = strlen(repl);

        int newlen = slen - len + replen;
        char* result = malloc(newlen + 1);
        memcpy(result, s, off);
        memcpy(result + off, repl, replen);
        memcpy(result + off + replen, s + off + len, slen - off - len);
        result[newlen] = '\0';

        ByteArray* ba = malloc(sizeof(ByteArray));
        ba->data = (uint8_t*)result;
        ba->length = newlen;
        vm_push(vm, val_string(ba));
    } else {
        // Извлечение подстроки
        char* result = malloc(len + 1);
        memcpy(result, s + off, len);
        result[len] = '\0';

        ByteArray* ba = malloc(sizeof(ByteArray));
        ba->data = (uint8_t*)result;
        ba->length = len;
        vm_push(vm, val_string(ba));
    }
}

// 3 — split(sep, s) — разбить строку в массив
static void handle_split(VM* vm, uint32_t param) {
    (void)param;

    Value str_val = vm_pop(vm);
    Value sep_val = vm_pop(vm);

    if (str_val.tag != V_STRING || sep_val.tag != V_STRING) {
        vm_push(vm, val_undef());
        return;
    }

    ByteArray* str_ba = (ByteArray*)str_val.hi;
    ByteArray* sep_ba = (ByteArray*)sep_val.hi;

    if (!str_ba || !str_ba->data || !sep_ba || !sep_ba->data) {
        vm_push(vm, val_undef());
        return;
    }

    char* s = (char*)str_ba->data;
    char* sep = (char*)sep_ba->data;
    int seplen = strlen(sep);

    // Считаем количество частей
    int count = 1;
    for (char* p = s; *p; p++) {
        if (strncmp(p, sep, seplen) == 0) {
            count++;
            p += seplen - 1;
        }
    }

    // Создаём массив
    ByteArray* arr = malloc(sizeof(ByteArray));
    arr->length = count * sizeof(Value);
    arr->data = calloc(count, sizeof(Value));
    Value* elements = (Value*)arr->data;

    // Разбиваем строку
    int idx = 0;
    char* start = s;
    for (char* p = s; ; p++) {
        if (*p == '\0' || strncmp(p, sep, seplen) == 0) {
            int len = p - start;
            char* part = malloc(len + 1);
            memcpy(part, start, len);
            part[len] = '\0';

            ByteArray* ba = malloc(sizeof(ByteArray));
            ba->data = (uint8_t*)part;
            ba->length = len;

            elements[idx++] = val_string(ba);

            if (*p == '\0') break;
            start = p + seplen;
            p += seplen - 1;
        }
    }

    vm_push(vm, val_array(arr));
}

// 4 — join(sep, arr) — собрать массив в строку
static void handle_join(VM* vm, uint32_t param) {
    (void)param;

    Value arr_val = vm_pop(vm);
    Value sep_val = vm_pop(vm);

    if (arr_val.tag != V_ARRAY || sep_val.tag != V_STRING) {
        vm_push(vm, val_undef());
        return;
    }

    ByteArray* arr_ba = (ByteArray*)arr_val.hi;
    ByteArray* sep_ba = (ByteArray*)sep_val.hi;

    if (!arr_ba || !arr_ba->data || !sep_ba || !sep_ba->data) {
        vm_push(vm, val_undef());
        return;
    }

    char* sep = (char*)sep_ba->data;
    int count = arr_ba->length / sizeof(Value);
    Value* elements = (Value*)arr_ba->data;

    // Считаем общую длину
    int total = 0;
    for (int i = 0; i < count; i++) {
        if (elements[i].tag == V_STRING) {
            ByteArray* ba = (ByteArray*)elements[i].hi;
            if (ba && ba->data) {
                total += strlen((char*)ba->data);
            }
        }
    }
    total += strlen(sep) * (count > 0 ? count - 1 : 0);

    // Собираем строку
    char* result = malloc(total + 1);
    result[0] = '\0';

    for (int i = 0; i < count; i++) {
        if (elements[i].tag == V_STRING) {
            ByteArray* ba = (ByteArray*)elements[i].hi;
            if (ba && ba->data) {
                if (i > 0) strcat(result, sep);
                strcat(result, (char*)ba->data);
            }
        }
    }

    ByteArray* result_ba = malloc(sizeof(ByteArray));
    result_ba->data = (uint8_t*)result;
    result_ba->length = strlen(result);

    vm_push(vm, val_string(result_ba));
}

// . — конкатенация строк
static void handle_concat(VM* vm, uint32_t param) {
    (void)param;

    Value b = vm_pop(vm);
    Value a = vm_pop(vm);

    if (a.tag == V_STRING && b.tag == V_STRING) {
        ByteArray* ba = (ByteArray*)a.hi;
        ByteArray* bb = (ByteArray*)b.hi;

        if (!ba || !ba->data) ba = NULL;
        if (!bb || !bb->data) bb = NULL;

        char* sa = ba ? (char*)ba->data : "";
        char* sb = bb ? (char*)bb->data : "";

        int len_a = strlen(sa);
        int len_b = strlen(sb);

        char* result = malloc(len_a + len_b + 1);
        memcpy(result, sa, len_a);
        memcpy(result + len_a, sb, len_b + 1);

        ByteArray* result_ba = malloc(sizeof(ByteArray));
        result_ba->data = (uint8_t*)result;
        result_ba->length = len_a + len_b;

        vm_push(vm, val_string(result_ba));
    } else {
        // Если не строки — пробуем числовое сложение?
        vm_push(vm, val_undef());
    }
}

// 7 — strcmp(s1, s2) — сравнение строк
static void handle_strcmp(VM* vm, uint32_t param) {
    (void)param;

    Value b = vm_pop(vm);
    Value a = vm_pop(vm);

    if (a.tag != V_STRING || b.tag != V_STRING) {
        vm_push(vm, val_undef());
        return;
    }

    ByteArray* ba = (ByteArray*)a.hi;
    ByteArray* bb = (ByteArray*)b.hi;

    if (!ba || !ba->data || !bb || !bb->data) {
        vm_push(vm, val_undef());
        return;
    }

    int cmp = strcmp((char*)ba->data, (char*)bb->data);

    if (cmp < 0) cmp = -1;
    else if (cmp > 0) cmp = 1;

    vm_push(vm, val_number(cmp, 0));
}

// H — создание хеша %h = array(N) или %h = (elements)
// Берёт со стека: массив элементов ИЛИ число N
static void handle_hash_new(VM* vm, uint32_t param) {
    int idx = (int)param;

    Value top = vm_pop(vm);
    int count = 0;
    Value* elements = NULL;
    ByteArray* temp_arr = NULL;

    if (top.tag == V_HASH) {
        // Уже хеш — просто сохраняем
        if (idx < vm->global_count) {
            vm->globals[idx] = top;
        }
        return;
    }

    if (top.tag == V_ARRAY) {
        // Передан массив элементов
        ByteArray* ba = (ByteArray*)top.hi;
        count = ba->length / sizeof(Value);
        elements = (Value*)ba->data;
        temp_arr = ba;  // сохраняем для освобождения
    } else if (top.tag == V_NUMBER) {
        // Передано число — создаём пустой хеш
        count = (int)top.hi;
        if (count < 0) count = 0;
    } else if (top.tag == V_UNDEF) {
        count = 0;
        // продолжить создание пустого хеша
    }else {
        vm_push(vm, val_undef());
        return;
    }

    // Вычисляем размер головного сегмента: 1 указатель + N элементов, N кратно 8
    int segments = (count + 7) / 8;
    if (segments < 1) segments = 1;
    int seg_size = 1 + segments * 8;

    // Создаём сегмент ключей
    ByteArray* keys = malloc(sizeof(ByteArray));
    keys->data = calloc(seg_size * sizeof(Value), 1);
    keys->length = seg_size * sizeof(Value);
    Value* k = (Value*)keys->data;
    k[0] = val_undef();  // нет следующего сегмента

    // Создаём сегмент значений
    ByteArray* vals = malloc(sizeof(ByteArray));
    vals->data = calloc(seg_size * sizeof(Value), 1);
    vals->length = seg_size * sizeof(Value);
    Value* v = (Value*)vals->data;
    v[0] = val_undef();  // нет следующего сегмента

    // Заполняем: ключи = undef (лёгкий хеш), значения = undef или копируем элементы
    for (int i = 0; i < count && i < segments * 8; i++) {
        k[1 + i] = val_undef();  // лёгкий хеш — ключей нет
        if (elements) {
            v[1 + i] = elements[i];  // копируем элемент из массива
        } else {
            v[1 + i] = val_undef();  // пустой элемент
        }
    }
    // Остальные слоты — undef
    for (int i = count; i < segments * 8; i++) {
        k[1 + i] = val_undef();
        v[1 + i] = val_undef();
    }

    // Создаём базовую структуру хеша
    ByteArray* base = malloc(sizeof(ByteArray));
    base->data = calloc(3 * sizeof(Value), 1);
    base->length = 3 * sizeof(Value);

    Value* parts = (Value*)base->data;
    parts[0] = val_number(count, 0);  // количество элементов
    parts[1] = val_array(keys);       // сегмент ключей
    parts[2] = val_array(vals);       // сегмент значений

    // Сохраняем в переменную
    if (idx >= 0 && idx < vm->global_count) {
        vm->globals[idx] = val_hash(base);
    }

    // Освобождаем временный массив если был
    if (temp_arr) {
        free(temp_arr->data);
        free(temp_arr);
    }
}

// 8 — %h[key] чтение элемента хеша (числовой индекс или строковой ключ)
static void handle_hash_index_get(VM* vm, uint32_t param) {
    (void)param;

    Value key_val = vm_pop(vm);
    Value hash_val = vm_pop(vm);

    if (hash_val.tag != V_HASH) {
        vm_push(vm, val_undef());
        return;
    }

    ByteArray* base = (ByteArray*)hash_val.hi;
    if (!base || !base->data) {
        vm_push(vm, val_undef());
        return;
    }

    Value* parts = (Value*)base->data;
    int count = (int)parts[0].hi;
    ByteArray* keys_ba = (ByteArray*)parts[1].hi;
    ByteArray* vals_ba = (ByteArray*)parts[2].hi;
    Value* keys = keys_ba ? (Value*)keys_ba->data : NULL;
    Value* vals = vals_ba ? (Value*)vals_ba->data : NULL;

    if (key_val.tag == V_NUMBER) {
        // Доступ по индексу
        int idx = (int)key_val.hi;
        if (idx >= 0 && idx < count && vals) {
            vm_push(vm, vals[1 + idx]);
        } else {
            vm_push(vm, val_undef());
        }
    } else if (key_val.tag == V_STRING) {
        // Доступ по ключу
        char* key_str = (char*)((ByteArray*)key_val.hi)->data;
        for (int i = 0; i < count; i++) {
            if (keys && keys[1 + i].tag == V_STRING) {
                char* existing = (char*)((ByteArray*)keys[1 + i].hi)->data;
                if (existing && strcmp(existing, key_str) == 0) {
                    vm_push(vm, vals[1 + i]);
                    return;
                }
            }
        }
        vm_push(vm, val_undef());
    } else {
        vm_push(vm, val_undef());
    }
}

// 9 — %h[key] = value запись элемента хеша
static void handle_hash_index_set(VM* vm, uint32_t param) {
    (void)param;

    Value val = vm_pop(vm);
    Value key_val = vm_pop(vm);
    Value hash_val = vm_pop(vm);

    if (hash_val.tag != V_HASH) return;

    ByteArray* base = (ByteArray*)hash_val.hi;
    if (!base || !base->data) return;

    Value* parts = (Value*)base->data;
    int count = (int)parts[0].hi;
    ByteArray* keys_ba = (ByteArray*)parts[1].hi;
    ByteArray* vals_ba = (ByteArray*)parts[2].hi;
    Value* keys = keys_ba ? (Value*)keys_ba->data : NULL;
    Value* vals = vals_ba ? (Value*)vals_ba->data : NULL;

    if (key_val.tag == V_NUMBER) {
        int idx = (int)key_val.hi;
        if (idx >= 0 && idx < count && vals) {
            vals[1 + idx] = val;
        }
    } else if (key_val.tag == V_STRING) {
        char* key_str = (char*)((ByteArray*)key_val.hi)->data;

        // Ищем существующий ключ
        for (int i = 0; i < count; i++) {
            if (keys && keys[1 + i].tag == V_STRING) {
                char* existing = (char*)((ByteArray*)keys[1 + i].hi)->data;
                if (existing && strcmp(existing, key_str) == 0) {
                    vals[1 + i] = val;
                    return;
                }
            }
        }

        // Новый ключ — добавляем
        // Ищем свободный слот или расширяем
        int free_slot = -1;
        for (int i = 0; i < keys_ba->length / sizeof(Value) - 1; i++) {
            if (keys[1 + i].tag == V_UNDEF) {
                free_slot = i;
                break;
            }
        }

        if (free_slot < 0) {
            // Расширяем сегмент
            int old_size = keys_ba->length / sizeof(Value);
            int new_size = old_size + 8;

            ByteArray* new_keys = malloc(sizeof(ByteArray));
            new_keys->data = calloc(new_size * sizeof(Value), 1);
            new_keys->length = new_size * sizeof(Value);
            memcpy(new_keys->data, keys_ba->data, keys_ba->length);

            ByteArray* new_vals = malloc(sizeof(ByteArray));
            new_vals->data = calloc(new_size * sizeof(Value), 1);
            new_vals->length = new_size * sizeof(Value);
            memcpy(new_vals->data, vals_ba->data, vals_ba->length);

            free(keys_ba->data); free(keys_ba);
            free(vals_ba->data); free(vals_ba);

            keys_ba = new_keys;
            vals_ba = new_vals;
            keys = (Value*)keys_ba->data;
            vals = (Value*)vals_ba->data;
            parts[1] = val_array(keys_ba);
            parts[2] = val_array(vals_ba);

            free_slot = count;
        }

        keys[1 + free_slot] = key_val;
        vals[1 + free_slot] = val;
        count++;
        parts[0] = val_number(count, 0);
    }
}

// h — вставка ключ-значения в хеш %h[key] = value (или для полного хеша)
static void handle_hash_insert(VM* vm, uint32_t param) {
    (void)param;

    Value val = vm_pop(vm);
    Value key = vm_pop(vm);
    Value hash_val = vm_pop(vm);

    if (hash_val.tag != V_HASH || key.tag != V_STRING) {
        return;
    }

    ByteArray* base = (ByteArray*)hash_val.hi;
    if (!base || !base->data) return;

    Value* parts = (Value*)base->data;
    int count = (int)parts[0].hi;

    ByteArray* keys_ba = (ByteArray*)parts[1].hi;
    ByteArray* vals_ba = (ByteArray*)parts[2].hi;

    if (!keys_ba || !vals_ba) return;

    Value* keys = (Value*)keys_ba->data;
    Value* vals = (Value*)vals_ba->data;

    // Проверяем, есть ли уже такой ключ
    int slot = -1;
    for (int i = 0; i < count; i++) {
        if (keys[1 + i].tag == V_STRING) {
            ByteArray* existing = (ByteArray*)keys[1 + i].hi;
            ByteArray* new_key = (ByteArray*)key.hi;
            if (existing && new_key && existing->data && new_key->data &&
                strcmp((char*)existing->data, (char*)new_key->data) == 0) {
                slot = i;
            break;
                }
        }
    }

    if (slot >= 0) {
        // Заменяем существующий ключ
        vals[1 + slot] = val;
    } else {
        // Добавляем новый ключ
        if (count >= keys_ba->length / sizeof(Value) - 1) {
            // Нужно расширить сегмент
            int old_seg_size = keys_ba->length / sizeof(Value);
            int new_seg_size = old_seg_size + 8;  // добавляем 8 слотов

            ByteArray* new_keys = malloc(sizeof(ByteArray));
            new_keys->data = calloc(new_seg_size * sizeof(Value), 1);
            new_keys->length = new_seg_size * sizeof(Value);
            memcpy(new_keys->data, keys_ba->data, keys_ba->length);
            Value* nk = (Value*)new_keys->data;
            nk[0] = val_undef();  // нет следующего сегмента
            for (int i = old_seg_size; i < new_seg_size; i++) nk[i] = val_undef();

            ByteArray* new_vals = malloc(sizeof(ByteArray));
            new_vals->data = calloc(new_seg_size * sizeof(Value), 1);
            new_vals->length = new_seg_size * sizeof(Value);
            memcpy(new_vals->data, vals_ba->data, vals_ba->length);
            Value* nv = (Value*)new_vals->data;
            nv[0] = val_undef();
            for (int i = old_seg_size; i < new_seg_size; i++) nv[i] = val_undef();

            free(keys_ba->data);
            free(keys_ba);
            free(vals_ba->data);
            free(vals_ba);

            keys_ba = new_keys;
            vals_ba = new_vals;
            keys = nk;
            vals = nv;
            parts[1] = val_array(keys_ba);
            parts[2] = val_array(vals_ba);
        }

        // Ищем первый свободный слот
        for (int i = 0; i < keys_ba->length / sizeof(Value) - 1; i++) {
            if (keys[1 + i].tag == V_UNDEF) {
                keys[1 + i] = key;
                vals[1 + i] = val;
                count++;
                parts[0] = val_number(count, 0);
                break;
            }
        }
    }
}


// ? — haskeys(%h) — проверка наличия ключей
static void handle_haskeys(VM* vm, uint32_t param) {
    (void)param;

    Value hash_val = vm_pop(vm);

    if (hash_val.tag != V_HASH) {
        vm_push(vm, val_number(0, 0));
        return;
    }

    ByteArray* base = (ByteArray*)hash_val.hi;
    if (!base || !base->data) {
        vm_push(vm, val_number(0, 0));
        return;
    }

    Value* parts = (Value*)base->data;
    ByteArray* keys_ba = (ByteArray*)parts[1].hi;
    if (!keys_ba) {
        vm_push(vm, val_number(0, 0));
        return;
    }

    Value* keys = (Value*)keys_ba->data;
    int count = (int)parts[0].hi;

    for (int i = 0; i < count; i++) {
        if (keys[1 + i].tag != V_UNDEF) {
            vm_push(vm, val_number(1, 0));
            return;
        }
    }

    vm_push(vm, val_number(0, 0));
}


// $ — getkey(%h, index) — получить ключ по индексу
static void handle_getkey(VM* vm, uint32_t param) {
    (void)param;
    Value idx_val = vm_pop(vm);
    Value hash_val = vm_pop(vm);

    if (hash_val.tag != V_HASH || idx_val.tag != V_NUMBER) {
        vm_push(vm, val_undef());
        return;
    }

    ByteArray* base = (ByteArray*)hash_val.hi;
    Value* parts = (Value*)base->data;
    int count = (int)parts[0].hi;
    int idx = (int)idx_val.hi;

    if (idx < 0 || idx >= count) {
        vm_push(vm, val_undef());
        return;
    }

    ByteArray* keys_ba = (ByteArray*)parts[1].hi;
    Value* keys = (Value*)keys_ba->data;

    if (keys[1 + idx].tag == V_STRING) {
        vm_push(vm, keys[1 + idx]);
    } else {
        vm_push(vm, val_undef());
    }
}

// + — add(%h, value, key?) — добавить элемент
static void handle_hash_add(VM* vm, uint32_t param) {

    int arg_count = (int)param;
    Value key_val;
    if (arg_count == 3) {
        key_val = vm_pop(vm);
    }
    Value val = vm_pop(vm);
    Value hash_val = vm_pop(vm);

    if (hash_val.tag != V_HASH) return;

    ByteArray* base = (ByteArray*)hash_val.hi;
    Value* parts = (Value*)base->data;
    int count = (int)parts[0].hi;
    ByteArray* keys_ba = (ByteArray*)parts[1].hi;
    ByteArray* vals_ba = (ByteArray*)parts[2].hi;
    Value* keys = (Value*)keys_ba->data;
    Value* vals = (Value*)vals_ba->data;

    if (arg_count == 2) {
        // Лёгкий хеш — добавляем в конец
        if (count >= keys_ba->length / sizeof(Value) - 1) {
            // Расширяем
            int old_size = keys_ba->length / sizeof(Value);
            int new_size = old_size + 8;
            ByteArray* new_keys = malloc(sizeof(ByteArray));
            new_keys->data = calloc(new_size * sizeof(Value), 1);
            new_keys->length = new_size * sizeof(Value);
            memcpy(new_keys->data, keys_ba->data, keys_ba->length);
            ByteArray* new_vals = malloc(sizeof(ByteArray));
            new_vals->data = calloc(new_size * sizeof(Value), 1);
            new_vals->length = new_size * sizeof(Value);
            memcpy(new_vals->data, vals_ba->data, vals_ba->length);
            free(keys_ba->data); free(keys_ba);
            free(vals_ba->data); free(vals_ba);
            keys_ba = new_keys; vals_ba = new_vals;
            keys = (Value*)keys_ba->data; vals = (Value*)vals_ba->data;
            parts[1] = val_array(keys_ba); parts[2] = val_array(vals_ba);
        }
        keys[1 + count] = val_undef();  // ключ = undef для лёгкого хеша
        vals[1 + count] = val;
        parts[0] = val_number(count + 1, 0);
    } else if (key_val.tag == V_STRING) {
        // Полный хеш — добавляем ключ-значение
        char* key_str = (char*)((ByteArray*)key_val.hi)->data;
        for (int i = 0; i < count; i++) {
            if (keys[1 + i].tag == V_STRING) {
                char* existing = (char*)((ByteArray*)keys[1 + i].hi)->data;
                if (existing && strcmp(existing, key_str) == 0) {
                    vals[1 + i] = val;  // Заменяем
                    return;
                }
            }
        }
        // Новый ключ
        if (count >= keys_ba->length / sizeof(Value) - 1) {
            int old_size = keys_ba->length / sizeof(Value);
            int new_size = old_size + 8;
            ByteArray* new_keys = malloc(sizeof(ByteArray));
            new_keys->data = calloc(new_size * sizeof(Value), 1);
            new_keys->length = new_size * sizeof(Value);
            memcpy(new_keys->data, keys_ba->data, keys_ba->length);
            ByteArray* new_vals = malloc(sizeof(ByteArray));
            new_vals->data = calloc(new_size * sizeof(Value), 1);
            new_vals->length = new_size * sizeof(Value);
            memcpy(new_vals->data, vals_ba->data, vals_ba->length);
            free(keys_ba->data); free(keys_ba);
            free(vals_ba->data); free(vals_ba);
            keys_ba = new_keys; vals_ba = new_vals;
            keys = (Value*)keys_ba->data; vals = (Value*)vals_ba->data;
            parts[1] = val_array(keys_ba); parts[2] = val_array(vals_ba);
        }
        keys[1 + count] = key_val;
        vals[1 + count] = val;
        parts[0] = val_number(count + 1, 0);
    }
    vm_push(vm, val_undef());  // возвращаем undef
}

// D — hdel(%h) — удалить последний элемент
static void handle_hash_delete(VM* vm, uint32_t param) {
    (void)param;
    Value hash_val = vm_pop(vm);

    if (hash_val.tag != V_HASH) return;

    ByteArray* base = (ByteArray*)hash_val.hi;
    Value* parts = (Value*)base->data;
    int count = (int)parts[0].hi;

    if (count <= 0) return;

    ByteArray* keys_ba = (ByteArray*)parts[1].hi;
    ByteArray* vals_ba = (ByteArray*)parts[2].hi;
    Value* keys = (Value*)keys_ba->data;
    Value* vals = (Value*)vals_ba->data;

    // Находим последний не-undef элемент
    for (int i = count - 1; i >= 0; i--) {
        if (vals[1 + i].tag != V_UNDEF || keys[1 + i].tag != V_UNDEF) {
            keys[1 + i] = val_undef();
            vals[1 + i] = val_undef();
            parts[0] = val_number(count - 1, 0);
            return;
        }
    }
}

// B — abs(x) — абсолютное значение
static void handle_abs(VM* vm, uint32_t param) {
    (void)param;
    Value v = vm_pop(vm);

    if (v.tag == V_NUMBER) {
        if (v.hi < 0) {
            vm_push(vm, val_number(-v.hi, v.lo ? 0x100000000 - v.lo : 0));
        } else {
            vm_push(vm, v);
        }
    } else {
        vm_push(vm, val_undef());
    }
}

// C — sign(x) — знак числа
static void handle_sign(VM* vm, uint32_t param) {
    (void)param;
    Value v = vm_pop(vm);

    if (v.tag == V_NUMBER) {
        if (v.hi > 0 || (v.hi == 0 && v.lo > 0)) {
            vm_push(vm, val_number(1, 0));
        } else if (v.hi < 0) {
            vm_push(vm, val_number(-1, 0));
        } else {
            vm_push(vm, val_number(0, 0));
        }
    } else {
        vm_push(vm, val_undef());
    }
}

// I — int(x) — целая часть
static void handle_int(VM* vm, uint32_t param) {
    (void)param;
    Value v = vm_pop(vm);

    if (v.tag == V_NUMBER) {
        vm_push(vm, val_number(v.hi, 0));
    } else {
        vm_push(vm, val_undef());
    }
}


// u — унарный минус
static void handle_neg(VM* vm, uint32_t param) {
    (void)param;
    Value v = vm_pop(vm);
    if (v.tag == V_NUMBER) {
        if (v.hi == 0 && v.lo == 0) {
            vm_push(vm, v);
        } else {
            vm_push(vm, val_number(-v.hi, v.lo ? 0x100000000 - v.lo : 0));
        }
    } else {
        vm_push(vm, val_undef());
    }
}


// F — frac(x) — дробная часть
static void handle_frac(VM* vm, uint32_t param) {
    (void)param;
    Value v = vm_pop(vm);

    if (v.tag == V_NUMBER) {
        vm_push(vm, val_number(0, v.lo));
    } else {
        vm_push(vm, val_undef());
    }
}

// V — inv(x) — инверсия знака
static void handle_inv(VM* vm, uint32_t param) {
    (void)param;
    Value v = vm_pop(vm);

    if (v.tag == V_NUMBER) {
        if (v.hi == 0 && v.lo == 0) {
            vm_push(vm, v);
        } else {
            vm_push(vm, val_number(-v.hi, v.lo ? 0x100000000 - v.lo : 0));
        }
    } else {
        vm_push(vm, val_undef());
    }
}

// 0 — inc($x) — инкремент переменной
static void handle_inc_var(VM* vm, uint32_t param) {
    int idx = (int)param;
    if (idx < vm->global_count && vm->globals[idx].tag == V_NUMBER) {
        vm->globals[idx].hi++;
    }
}


// @ — dec($x) — декремент переменной
static void handle_dec_var(VM* vm, uint32_t param) {
    int idx = (int)param;
    if (idx < vm->global_count && vm->globals[idx].tag == V_NUMBER) {
        vm->globals[idx].hi--;
    }
}


// J — sqrt(x) — квадратный корень через idouble
static void handle_sqrt(VM* vm, uint32_t param) {
    (void)param;
    Value v = vm_pop(vm);
    //fprintf(stderr, "DEBUG sqrt called, hi=%lld, lo=%u\n", (long long)v.hi, v.lo);

    if (v.tag != V_NUMBER) {
        vm_push(vm, val_undef());
        return;
    }

    if (v.hi < 0) {
        vm_push(vm, val_undef());
        return;
    }

    if (v.hi == 0 && v.lo == 0) {
        vm_push(vm, val_number(0, 0));
        return;
    }

    // val = X * 2^32 (без сдвига)
    __int128 val = ((__int128)v.hi << 32) | v.lo;

    // Целочисленный sqrt
    __int128 x = val;
    __int128 y = (x + 1) >> 1;
    while (y < x) {
        x = y;
        y = (x + val / x) >> 1;
    }
    // Масштабируем результат: sqrt(X * 2^32) = sqrt(X) * 2^16
    x <<= 16;
    int64_t hi = (int64_t)(x >> 32);
    uint32_t lo = (uint32_t)(x & 0xFFFFFFFF);
    vm_push(vm, val_number(hi, lo));
}

/*
// J — sqrt(x) — квадратный корень через double
static void handle_sqrt(VM* vm, uint32_t param) {
    (void)param;
    Value v = vm_pop(vm);
    if (v.tag != V_NUMBER || v.hi < 0) {
        vm_push(vm, val_undef());
        return;
    }

    double d = (double)v.hi + (double)v.lo / 4294967296.0;

    double root = sqrt(d);

    int64_t hi = (int64_t)root;
    uint32_t lo = (uint32_t)((root - hi) * 4294967296.0);

    vm_push(vm, val_number(hi, lo));
}*/


// V — undef(x) — проверка на undef
static void handle_is_undef(VM* vm, uint32_t param) {
    (void)param;
    Value v = vm_pop(vm);
    vm_push(vm, val_number(v.tag == V_UNDEF ? 1 : 0, 0));
}

// Y — ifundef(x, default) — замена если undef
static void handle_ifundef(VM* vm, uint32_t param) {
    (void)param;
    Value def_val = vm_pop(vm);
    Value val = vm_pop(vm);
    vm_push(vm, val.tag == V_UNDEF ? def_val : val);
}

// X — exists(%h, key) — проверка существования
static void handle_exists(VM* vm, uint32_t param) {
    (void)param;
    Value key_val = vm_pop(vm);
    Value hash_val = vm_pop(vm);

    if (hash_val.tag != V_HASH) {
        vm_push(vm, val_number(0, 0));
        return;
    }

    ByteArray* base = (ByteArray*)hash_val.hi;
    Value* parts = (Value*)base->data;
    int count = (int)parts[0].hi;
    ByteArray* keys_ba = (ByteArray*)parts[1].hi;
    Value* keys = (Value*)keys_ba->data;

    if (key_val.tag == V_NUMBER) {
        int idx = (int)key_val.hi;
        if (idx >= 0 && idx < count) {
            vm_push(vm, val_number(1, 0));
        } else {
            vm_push(vm, val_number(0, 0));
        }
    } else if (key_val.tag == V_STRING) {
        char* key_str = (char*)((ByteArray*)key_val.hi)->data;

        for (int i = 0; i < count; i++) {
            fprintf(stderr, "  keys[%d].tag=%d", i, keys[1 + i].tag);
            if (keys[1 + i].tag == V_STRING) {
                char* existing = (char*)((ByteArray*)keys[1 + i].hi)->data;
                fprintf(stderr, " ='%s'", existing);
                if (existing && strcmp(existing, key_str) == 0) {
                    fprintf(stderr, " — MATCH!\n");
                    vm_push(vm, val_number(1, 0));
                    return;
                }
            }
            fprintf(stderr, "\n");
        }
        vm_push(vm, val_number(0, 0));
    } else {
        vm_push(vm, val_number(0, 0));
    }
}


// ^ — возведение в степень
static void handle_pow(VM* vm, uint32_t param) {
    (void)param;
    Value b = vm_pop(vm), a = vm_pop(vm);

    if (a.tag == V_NUMBER && b.tag == V_NUMBER) {
        double d = pow((double)a.hi + (double)a.lo / 4294967296.0,
                       (double)b.hi + (double)b.lo / 4294967296.0);
        int64_t hi = (int64_t)d;
        uint32_t lo = (uint32_t)((d - hi) * 4294967296.0);
        vm_push(vm, val_number(hi, lo));
    } else {
        vm_push(vm, val_undef());
    }
}


// # — setkey(%h, old_key, new_key) — изменить ключ
static void handle_setkey(VM* vm, uint32_t param) {
    (void)param;
    Value new_key = vm_pop(vm);
    Value old_key = vm_pop(vm);
    Value hash_val = vm_pop(vm);

    if (hash_val.tag != V_HASH || old_key.tag != V_STRING || new_key.tag != V_STRING) {
        vm_push(vm, val_undef());
        return;
    }

    ByteArray* base = (ByteArray*)hash_val.hi;
    Value* parts = (Value*)base->data;
    int count = (int)parts[0].hi;
    ByteArray* keys_ba = (ByteArray*)parts[1].hi;
    Value* keys = (Value*)keys_ba->data;

    char* old_str = (char*)((ByteArray*)old_key.hi)->data;
    for (int i = 0; i < count; i++) {
        if (keys[1 + i].tag == V_STRING) {
            char* existing = (char*)((ByteArray*)keys[1 + i].hi)->data;
            if (existing && strcmp(existing, old_str) == 0) {
                keys[1 + i] = new_key;
                vm_push(vm, val_undef());
                return;
            }
        }
    }
    vm_push(vm, val_undef());
}

// G — exp(x) — экспонента
static void handle_exp(VM* vm, uint32_t param) {
    (void)param;
    Value v = vm_pop(vm);
    //fprintf(stderr, "DEBUG exp called, hi=%lld, lo=%u\n", (long long)v.hi, v.lo);
    if (v.tag != V_NUMBER) { vm_push(vm, val_undef()); return; }
    double d = (double)v.hi + (double)v.lo / 4294967296.0;
    double r = exp(d);
    int64_t hi = (int64_t)r;
    uint32_t lo = (uint32_t)((r - hi) * 4294967296.0);
    vm_push(vm, val_number(hi, lo));
}

// Q — ln(x) — натуральный логарифм
static void handle_ln(VM* vm, uint32_t param) {
    (void)param;
    Value v = vm_pop(vm);
    //fprintf(stderr, "DEBUG exp called, hi=%lld, lo=%u\n", (long long)v.hi, v.lo);
    if (v.tag != V_NUMBER || v.hi <= 0) { vm_push(vm, val_undef()); return; }
    double d = (double)v.hi + (double)v.lo / 4294967296.0;
    double r = log(d);
    int64_t hi = (int64_t)r;
    uint32_t lo = (uint32_t)((r - hi) * 4294967296.0);
    vm_push(vm, val_number(hi, lo));
}

// T — log(x) — десятичный логарифм
static void handle_log10(VM* vm, uint32_t param) {
    (void)param;
    Value v = vm_pop(vm);
    //fprintf(stderr, "DEBUG exp called, hi=%lld, lo=%u\n", (long long)v.hi, v.lo);
    if (v.tag != V_NUMBER || v.hi <= 0) { vm_push(vm, val_undef()); return; }
    double d = (double)v.hi + (double)v.lo / 4294967296.0;
    double r = log10(d);
    int64_t hi = (int64_t)r;
    uint32_t lo = (uint32_t)((r - hi) * 4294967296.0);
    vm_push(vm, val_number(hi, lo));
}

// { — get8(var, byte_index) — прочитать 1 байт
static void handle_get8(VM* vm, uint32_t param) {
    (void)param;
    Value idx_val = vm_pop(vm);   // первым снимаем индекс (вершина стека)
    Value var_val = vm_pop(vm);   // вторым — переменную

    if (idx_val.tag != V_NUMBER) { vm_push(vm, val_undef()); return; }

    ByteArray* ba = NULL;
    if (var_val.tag == V_STRING || var_val.tag == V_ARRAY) {
        ba = (ByteArray*)var_val.hi;
    } else if (var_val.tag == V_HASH) {
        // Хеш — не поддерживается напрямую, нужен hvalues
        vm_push(vm, val_undef());
        return;
    } else {
        vm_push(vm, val_undef());
        return;
    }

    if (!ba || !ba->data) { vm_push(vm, val_undef()); return; }

    int idx = (int)idx_val.hi;
    if (idx < 0 || idx >= ba->length) { vm_push(vm, val_undef()); return; }

    uint8_t byte = ba->data[idx];
    vm_push(vm, val_number(byte, 0));
}


// } — get16(var, byte_index) — прочитать 2 байта
static void handle_get16(VM* vm, uint32_t param) {
    (void)param;
    Value idx_val = vm_pop(vm);
    Value var_val = vm_pop(vm);
    if (idx_val.tag != V_NUMBER) { vm_push(vm, val_undef()); return; }
    ByteArray* ba = NULL;
    if (var_val.tag == V_STRING || var_val.tag == V_ARRAY) {
        ba = (ByteArray*)var_val.hi;
    } else { vm_push(vm, val_undef()); return; }
    if (!ba || !ba->data) { vm_push(vm, val_undef()); return; }
    int idx = (int)idx_val.hi;
    if (idx < 0 || idx + 1 >= ba->length) { vm_push(vm, val_undef()); return; }
    uint16_t val = ba->data[idx] | (ba->data[idx+1] << 8);
    vm_push(vm, val_number(val, 0));
}


static void handle_get32(VM* vm, uint32_t param) {
    (void)param;
    Value idx_val = vm_pop(vm);
    Value var_val = vm_pop(vm);
    if (idx_val.tag != V_NUMBER) { vm_push(vm, val_undef()); return; }
    ByteArray* ba = NULL;
    if (var_val.tag == V_STRING || var_val.tag == V_ARRAY) {
        ba = (ByteArray*)var_val.hi;
    } else { vm_push(vm, val_undef()); return; }
    if (!ba || !ba->data) { vm_push(vm, val_undef()); return; }
    int idx = (int)idx_val.hi;
    if (idx < 0 || idx + 3 >= ba->length) { vm_push(vm, val_undef()); return; }
    uint32_t val = ba->data[idx] | (ba->data[idx+1] << 8) | (ba->data[idx+2] << 16) | (ba->data[idx+3] << 24);
    vm_push(vm, val_number((int64_t)val, 0));

    vm_push(vm, val_number(val, 0));
}

static void handle_get64(VM* vm, uint32_t param) {
    (void)param;
    Value idx_val = vm_pop(vm);
    Value var_val = vm_pop(vm);
    if (idx_val.tag != V_NUMBER) { vm_push(vm, val_undef()); return; }
    ByteArray* ba = NULL;
    if (var_val.tag == V_STRING || var_val.tag == V_ARRAY) {
        ba = (ByteArray*)var_val.hi;
    } else { vm_push(vm, val_undef()); return; }
    if (!ba || !ba->data) { vm_push(vm, val_undef()); return; }
    int idx = (int)idx_val.hi;
    if (idx < 0 || idx + 7 >= ba->length) { vm_push(vm, val_undef()); return; }
    uint64_t val = 0;
    for (int i = 0; i < 8; i++) val |= (uint64_t)ba->data[idx+i] << (i*8);
    vm_push(vm, val_number((int64_t)val, 0));

}

// ; — set8(var, byte_index, value) — записать 1 байт
static void handle_set8(VM* vm, uint32_t param) {
    (void)param;
    Value val_val = vm_pop(vm);
    Value idx_val = vm_pop(vm);
    Value var_val = vm_pop(vm);
    if (idx_val.tag != V_NUMBER || val_val.tag != V_NUMBER) return;
    ByteArray* ba = NULL;
    if (var_val.tag == V_STRING || var_val.tag == V_ARRAY) {
        ba = (ByteArray*)var_val.hi;
    } else return;
    if (!ba || !ba->data) return;
    int idx = (int)idx_val.hi;
    if (idx < 0 || idx >= ba->length) return;
    ba->data[idx] = (uint8_t)val_val.hi;
    vm_push(vm, val_undef());
}

// , — set16
static void handle_set16(VM* vm, uint32_t param) {
    (void)param;
    Value val_val = vm_pop(vm);
    Value idx_val = vm_pop(vm);
    Value var_val = vm_pop(vm);
    if (idx_val.tag != V_NUMBER || val_val.tag != V_NUMBER) return;
    ByteArray* ba = NULL;
    if (var_val.tag == V_STRING || var_val.tag == V_ARRAY) {
        ba = (ByteArray*)var_val.hi;
    } else return;
    if (!ba || !ba->data) return;
    int idx = (int)idx_val.hi;
    if (idx < 0 || idx + 1 >= ba->length) return;
    uint16_t v = (uint16_t)val_val.hi;
    ba->data[idx] = v & 0xFF;
    ba->data[idx+1] = (v >> 8) & 0xFF;
    vm_push(vm, val_undef());
}

// . — set32
static void handle_set32(VM* vm, uint32_t param) {
    (void)param;
    Value val_val = vm_pop(vm);
    Value idx_val = vm_pop(vm);
    Value var_val = vm_pop(vm);
    if (idx_val.tag != V_NUMBER || val_val.tag != V_NUMBER) return;
    ByteArray* ba = NULL;
    if (var_val.tag == V_STRING || var_val.tag == V_ARRAY) {
        ba = (ByteArray*)var_val.hi;
    } else return;
    if (!ba || !ba->data) return;
    int idx = (int)idx_val.hi;
    if (idx < 0 || idx + 3 >= ba->length) return;
    uint32_t v = (uint32_t)val_val.hi;
    for (int i = 0; i < 4; i++) ba->data[idx+i] = (v >> (i*8)) & 0xFF;
    vm_push(vm, val_undef());
}

// ` — set64 (backtick)
static void handle_set64(VM* vm, uint32_t param) {
    (void)param;
    Value val_val = vm_pop(vm);
    Value idx_val = vm_pop(vm);
    Value var_val = vm_pop(vm);
    if (idx_val.tag != V_NUMBER || val_val.tag != V_NUMBER) return;
    ByteArray* ba = NULL;
    if (var_val.tag == V_STRING || var_val.tag == V_ARRAY) {
        ba = (ByteArray*)var_val.hi;
    } else return;
    if (!ba || !ba->data) return;
    int idx = (int)idx_val.hi;
    if (idx < 0 || idx + 7 >= ba->length) return;
    uint64_t v = (uint64_t)val_val.hi;
    for (int i = 0; i < 8; i++) ba->data[idx+i] = (v >> (i*8)) & 0xFF;
    vm_push(vm, val_undef());
}

// % — hvalues(%h) — получить массив значений хеша
static void handle_hvalues(VM* vm, uint32_t param) {
    (void)param;
    Value hash_val = vm_pop(vm);
    if (hash_val.tag != V_HASH) { vm_push(vm, val_undef()); return; }

    ByteArray* base = (ByteArray*)hash_val.hi;
    Value* parts = (Value*)base->data;
    int count = (int)parts[0].hi;
    ByteArray* vals_ba = (ByteArray*)parts[2].hi;
    Value* vals = (Value*)vals_ba->data;

    // Создаём массив значений
    ByteArray* result = malloc(sizeof(ByteArray));
    result->length = count * sizeof(Value);
    result->data = malloc(result->length);
    Value* res_vals = (Value*)result->data;

    for (int i = 0; i < count; i++) {
        res_vals[i] = vals[1 + i];
    }

    vm_push(vm, val_array(result));
}

// \ — hkeys(%h) — получить массив ключей хеша
static void handle_hkeys(VM* vm, uint32_t param) {
    (void)param;
    Value hash_val = vm_pop(vm);
    if (hash_val.tag != V_HASH) { vm_push(vm, val_undef()); return; }

    ByteArray* base = (ByteArray*)hash_val.hi;
    Value* parts = (Value*)base->data;
    int count = (int)parts[0].hi;
    ByteArray* keys_ba = (ByteArray*)parts[1].hi;
    Value* keys = (Value*)keys_ba->data;

    // Создаём массив ключей
    ByteArray* result = malloc(sizeof(ByteArray));
    result->length = count * sizeof(Value);
    result->data = malloc(result->length);
    Value* res_keys = (Value*)result->data;

    for (int i = 0; i < count; i++) {
        res_keys[i] = keys[1 + i];
    }

    vm_push(vm, val_array(result));
}


// exit
static void handle_exit(VM* vm, uint32_t param) {
    (void)vm; (void)param;
}
// Вторая таблица для файловых функций
typedef void (*FileHandler)(VM* vm);
static FileHandler file_handlers[256];

// 0 — fopen(name, mode)
static void handle_fopen(VM* vm) {
    Value mode = vm_pop(vm);
    Value name = vm_pop(vm);


    if (name.tag == V_STRING && mode.tag == V_STRING && vm->open_file_count < 256) {
        ByteArray* nba = (ByteArray*)name.hi;
        ByteArray* mba = (ByteArray*)mode.hi;
        FILE* f = fopen((char*)nba->data, (char*)mba->data);
        if (f) {
            int idx = vm->open_file_count++;
            vm->open_files[idx] = f;
            vm_push(vm, val_number(idx, 0));
        } else {
            vm_push(vm, val_number(-1, 0));
        }
    } else {
        vm_push(vm, val_undef());
    }
}

// 1 — fclose(fh)
static void handle_fclose(VM* vm) {
    Value fh = vm_pop(vm);
    if (fh.tag == V_NUMBER && fh.hi >= 0 && fh.hi < vm->open_file_count && vm->open_files[fh.hi]) {
        fclose(vm->open_files[fh.hi]);
        vm->open_files[fh.hi] = NULL;
        vm_push(vm, val_number(1, 0));
    } else {
        vm_push(vm, val_number(0, 0));
    }
}

// 2 — freadline(fh)
static void handle_freadline(VM* vm) {
    Value fh = vm_pop(vm);
    if (fh.tag == V_NUMBER && fh.hi >= 0 && fh.hi < vm->open_file_count && vm->open_files[fh.hi]) {
        char* line = NULL;
        size_t len = 0;
        if (getline(&line, &len, vm->open_files[fh.hi]) != -1) {
            if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
            ByteArray* ba = malloc(sizeof(ByteArray));
            ba->data = (uint8_t*)line;
            ba->length = strlen(line);
            vm_push(vm, val_string(ba));
        } else {
            free(line);
            vm_push(vm, val_undef());
        }
    } else {
        vm_push(vm, val_undef());
    }
}

// 3 — fread(fh, len)
static void handle_fread(VM* vm) {
    Value len_val = vm_pop(vm);
    Value fh = vm_pop(vm);
    if (fh.tag == V_NUMBER && len_val.tag == V_NUMBER &&
        fh.hi >= 0 && fh.hi < vm->open_file_count && vm->open_files[fh.hi]) {
        int len = (int)len_val.hi;
    if (len > 0) {
        char* buf = malloc(len + 1);
        int n = fread(buf, 1, len, vm->open_files[fh.hi]);
        buf[n] = '\0';
        ByteArray* ba = malloc(sizeof(ByteArray));
        ba->data = (uint8_t*)buf;
        ba->length = n;
        vm_push(vm, val_string(ba));
    } else {
        vm_push(vm, val_undef());
    }
        } else {
            vm_push(vm, val_undef());
        }
}

// 4 — fprint(fh, data)
static void handle_fprint(VM* vm) {
    Value data = vm_pop(vm);
    Value fh = vm_pop(vm);
    if (fh.tag == V_NUMBER && data.tag == V_STRING &&
        fh.hi >= 0 && fh.hi < vm->open_file_count && vm->open_files[fh.hi]) {
        ByteArray* ba = (ByteArray*)data.hi;
    fputs((char*)ba->data, vm->open_files[fh.hi]);
    vm_push(vm, val_number(1, 0));
        } else {
            vm_push(vm, val_undef());
        }
}

// 5 — sysread(fh, len)
static void handle_sysread(VM* vm) {
    Value len_val = vm_pop(vm);
    Value fh = vm_pop(vm);
    if (fh.tag == V_NUMBER && len_val.tag == V_NUMBER &&
        fh.hi >= 0 && fh.hi < vm->open_file_count && vm->open_files[fh.hi]) {
        int len = (int)len_val.hi;
    if (len > 0) {
        char* buf = malloc(len + 1);
        int n = fread(buf, 1, len, vm->open_files[fh.hi]);
        buf[n] = '\0';
        ByteArray* ba = malloc(sizeof(ByteArray));
        ba->data = (uint8_t*)buf;
        ba->length = n;
        vm_push(vm, val_string(ba));
    } else {
        vm_push(vm, val_undef());
    }
        } else {
            vm_push(vm, val_undef());
        }
}

// 6 — syswrite(fh, data)
static void handle_syswrite(VM* vm) {
    Value data = vm_pop(vm);
    Value fh = vm_pop(vm);
    if (fh.tag == V_NUMBER && data.tag == V_STRING &&
        fh.hi >= 0 && fh.hi < vm->open_file_count && vm->open_files[fh.hi]) {
        ByteArray* ba = (ByteArray*)data.hi;
    fwrite(ba->data, 1, ba->length, vm->open_files[fh.hi]);
        }
        vm_push(vm, val_undef());
}

// 7 — frename(old, new)
static void handle_frename(VM* vm) {
    Value new_name = vm_pop(vm);
    Value old_name = vm_pop(vm);
    if (old_name.tag == V_STRING && new_name.tag == V_STRING) {
        int result = rename((char*)((ByteArray*)old_name.hi)->data,
                            (char*)((ByteArray*)new_name.hi)->data);
        vm_push(vm, val_number(result == 0 ? 1 : 0, 0));
    } else {
        vm_push(vm, val_undef());
    }
}

// 8 — funlink(file)
static void handle_funlink(VM* vm) {
    Value file = vm_pop(vm);
    if (file.tag == V_STRING) {
        int result = remove((char*)((ByteArray*)file.hi)->data);
        vm_push(vm, val_number(result == 0 ? 1 : 0, 0));
    } else {
        vm_push(vm, val_undef());
    }
}

// 9 fseek(fh, offset, whence) — позиционирование
static void handle_fseek(VM* vm) {
    Value whence_val = vm_pop(vm);
    Value offset_val = vm_pop(vm);
    Value fh = vm_pop(vm);
    if (fh.tag == V_NUMBER && offset_val.tag == V_NUMBER && whence_val.tag == V_NUMBER &&
        fh.hi >= 0 && fh.hi < vm->open_file_count && vm->open_files[fh.hi]) {
        int offset = (int)offset_val.hi;
    int whence = (int)whence_val.hi;
    fseek(vm->open_files[fh.hi], offset, whence);
        }
        vm_push(vm, val_undef());
}

// 10 ftell(fh) — текущая позиция
static void handle_ftell(VM* vm) {
    Value fh = vm_pop(vm);
    if (fh.tag == V_NUMBER && fh.hi >= 0 && fh.hi < vm->open_file_count && vm->open_files[fh.hi]) {
        long pos = ftell(vm->open_files[fh.hi]);
        vm_push(vm, val_number(pos, 0));
    } else {
        vm_push(vm, val_undef());
    }
}

// 11 — feof(fh) — проверка конца файла
static void handle_feof(VM* vm) {
    Value fh = vm_pop(vm);
    if (fh.tag == V_NUMBER && fh.hi >= 0 && fh.hi < vm->open_file_count && vm->open_files[fh.hi]) {
        vm_push(vm, val_number(feof(vm->open_files[fh.hi]) ? 1 : 0, 0));
    } else {
        vm_push(vm, val_undef());
    }
}

// * — файловые функции (вторая таблица)
static void handle_file(VM* vm, uint32_t param) {
    if (param < 256 && file_handlers[param]) {
        file_handlers[param](vm);
    } else {
        vm_push(vm, val_undef());
    }
}


static void vm_init_file_handlers(void) {
    for (int i = 0; i < 256; i++) file_handlers[i] = NULL;
    file_handlers[0] = handle_fopen;
    file_handlers[1] = handle_fclose;
    file_handlers[2] = handle_freadline;
    file_handlers[3] = handle_fread;
    file_handlers[4] = handle_fprint;
    file_handlers[5] = handle_sysread;
    file_handlers[6] = handle_syswrite;
    file_handlers[7] = handle_frename;
    file_handlers[8] = handle_funlink;
    file_handlers[9] = handle_fseek;
    file_handlers[10] = handle_ftell;
    file_handlers[11] = handle_feof;
    file_handlers[12] = handle_socket;
    file_handlers[13] = handle_connect;
    file_handlers[14] = handle_bind;
    file_handlers[15] = handle_listen;
    file_handlers[16] = handle_accept;
    file_handlers[17] = handle_send;
    file_handlers[18] = handle_recv;
    file_handlers[19] = handle_sockclose;

}


// === Таблица диспетчеризации ===
static void vm_init_handlers(VM* vm) {
    for (int i = 0; i < 256; i++) vm->handlers[i] = NULL;
    vm->handlers['i'] = handle_integer;
    vm->handlers['('] = handle_number_new;
    vm->handlers['s'] = handle_string;
    vm->handlers['v'] = handle_load_global;
    vm->handlers['p'] = handle_store_global;
    vm->handlers['P'] = handle_print;
    vm->handlers['c'] = handle_call;
    vm->handlers['y'] = handle_return;
    vm->handlers['S'] = handle_array_create;
    vm->handlers['A'] = handle_array_index;
    vm->handlers['L'] = handle_length;
    vm->handlers['['] = handle_array_new;
    vm->handlers['N'] = handle_clone;
    vm->handlers['x'] = handle_deallocate;
    vm->handlers['M'] = handle_array_store;
    vm->handlers['R'] = handle_reverse;
    vm->handlers['O'] = handle_sort;
    vm->handlers['U'] = handle_undef;
    vm->handlers['a'] = handle_add;
    vm->handlers['-'] = handle_sub;
    vm->handlers['m'] = handle_mul;
    vm->handlers['/'] = handle_div;
    vm->handlers['%'] = handle_mod;
    vm->handlers['e'] = handle_equal;
    vm->handlers['n'] = handle_not_equal;
    vm->handlers['l'] = handle_less;
    vm->handlers['g'] = handle_greater;
    vm->handlers['b'] = handle_less_equal;
    vm->handlers[']'] = handle_greater_equal;
    vm->handlers['j'] = handle_jump;
    vm->handlers['f'] = handle_jump_if_false;
    vm->handlers['z'] = handle_lc;
    vm->handlers['Z'] = handle_uc;
    vm->handlers['5'] = handle_chr;
    vm->handlers['6'] = handle_ord;
    vm->handlers['K'] = handle_chomp;
    vm->handlers['t'] = handle_chop;
    vm->handlers['1'] = handle_lcfirst;
    vm->handlers['2'] = handle_ucfirst;
    vm->handlers['w'] = handle_str_index;
    vm->handlers['W'] = handle_str_rindex;
    vm->handlers['k'] = handle_substr;
    vm->handlers['3'] = handle_split;
    vm->handlers['4'] = handle_join;
    vm->handlers['^'] = handle_pow;
    vm->handlers['7'] = handle_strcmp;
    vm->handlers['H'] = handle_hash_new;
    vm->handlers['8'] = handle_hash_index_get;
    vm->handlers['9'] = handle_hash_index_set;
    vm->handlers['h'] = handle_hash_insert;
    vm->handlers['?'] = handle_haskeys;
    vm->handlers['E'] = handle_exit;
    vm->handlers['$'] = handle_getkey;
    vm->handlers['+'] = handle_hash_add;
    vm->handlers['D'] = handle_hash_delete;
    vm->handlers['B'] = handle_abs;
    vm->handlers['C'] = handle_sign;
    vm->handlers['I'] = handle_int;
    vm->handlers['u'] = handle_neg;
    vm->handlers['F'] = handle_frac;
    vm->handlers['V'] = handle_inv;
    vm->handlers['0'] = handle_inc_var;
    vm->handlers['@'] = handle_dec_var;
    vm->handlers['J'] = handle_sqrt;
    vm->handlers['*'] = handle_file;
    vm->handlers['V'] = handle_is_undef;
    vm->handlers['Y'] = handle_ifundef;
    vm->handlers['X'] = handle_exists;
    vm->handlers['!'] = handle_setkey;
    vm->handlers['G'] = handle_exp;
    vm->handlers['Q'] = handle_ln;
    vm->handlers['T'] = handle_log10;
    vm->handlers['{'] = handle_get8;
    vm->handlers['}'] = handle_get16;
    vm->handlers['~'] = handle_get32;
    vm->handlers['_'] = handle_get64;
    vm->handlers[';'] = handle_set8;
    vm->handlers[','] = handle_set16;
    vm->handlers['.'] = handle_set32;
    vm->handlers['`'] = handle_set64;
    vm->handlers['&'] = handle_hvalues;
    vm->handlers['\\'] = handle_hkeys;
}

// === VM ===
VM* vm_new(void) {
    VM* vm = calloc(1, sizeof(VM));
    vm->sp = vm->stack;
    vm->string_pool_count = 0;
    vm->global_count = 0;
    vm->globals = NULL;
    vm_init_handlers(vm);

    vm->open_file_count = 0;
    for (int i = 0; i < 256; i++) vm->open_files[i] = NULL;
    vm_init_file_handlers();

    return vm;
}

void vm_free(VM* vm) {
    for (int i = 0; i < vm->string_pool_count; i++) free(vm->string_pool[i]);
    free(vm->globals);

    // Освобождаем загруженные модули
    for (int i = 0; i < loaded_module_count; i++) {
        LoadedModule* lm = &loaded_modules[i];
        for (int j = 0; j < lm->string_pool_count; j++) free(lm->string_pool[j]);
        free(lm->globals);
        free(lm->bytecode);
    }
    loaded_module_count = 0;

    free(vm);
}

void vm_add_inc_path(VM* vm, const char* path) {
    (void)vm; (void)path;
}

// Парсинг ТДФ основного модуля
static void vm_parse_main_tdf(const uint8_t* code, uint32_t tdf_off, int code_size) {
    const uint8_t* tp = code + tdf_off;
    main_module.function_count = 0;

    while (tp < code + code_size && main_module.function_count < 256) {
        if (tp[0] == '<' && tp[1] == 'f' && tp[2] == ':') {
            tp += 3;

            // Читаем имя функции до ':'
            char fname[128]; int i = 0;
            while (tp < code + code_size && *tp != ':') fname[i++] = *tp++;
            fname[i] = '\0';
            if (*tp == ':') tp++;

            // Читаем имя модуля до ':'
            char mname[64]; i = 0;
            while (tp < code + code_size && *tp != ':') mname[i++] = *tp++;
            mname[i] = '\0';
            if (*tp == ':') tp++;

            // PARAM_COUNT (2 hex)
            char tmp[3]; memcpy(tmp, tp, 2); tmp[2] = '\0';
            int param_count = strtoul(tmp, NULL, 16);
            tp += 2;
            if (*tp == ':') tp++;

            // LOCAL_COUNT (2 hex)
            memcpy(tmp, tp, 2); tmp[2] = '\0';
            int local_count = strtoul(tmp, NULL, 16);
            tp += 2;
            if (*tp == ':') tp++;

            // CODE_OFFSET (8 hex)
            char otmp[9]; memcpy(otmp, tp, 8); otmp[8] = '\0';
            int code_offset = strtoul(otmp, NULL, 16);
            tp += 8;
            if (*tp == ':') tp++;

            // EXPORT_FLAG (пропускаем)
            while (tp < code + code_size && *tp != '>') tp++;
            if (*tp == '>') tp++;

            // Сохраняем функцию
            strcpy(main_module.functions[main_module.function_count].name, fname);
            strcpy(main_module.functions[main_module.function_count].module_name, mname);
            main_module.functions[main_module.function_count].code_offset = code_offset;
            main_module.functions[main_module.function_count].param_count = param_count;
            main_module.functions[main_module.function_count].local_count = local_count;
            main_module.function_count++;
        } else {
            tp++;
        }
    }
}

void vm_execute(VM* vm, const uint8_t* code, int code_size) {
    const uint8_t* ptr = code;
    while (*ptr == ' ') ptr++;

    if (memcmp(ptr, "<OYCE;", 6) != 0) {
        fprintf(stderr, "Invalid header\n");
        return;
    }
    ptr += 6;

    const char* hs = (char*)ptr;
    const char* he = strchr(hs, '>');
    if (!he) return;

    size_t hl = he - hs;
    char* hb = malloc(hl + 1);
    memcpy(hb, hs, hl);
    hb[hl] = '\0';

    char so[16]={0}, co[16]={0}, gs[16]={0}, fo[16]={0}, no[16]={0};
    char* tok = strtok(hb, ";");
    while (tok) {
        if (tok[0]=='S') strcpy(so, tok+2);
        else if (tok[0]=='C') strcpy(co, tok+2);
        else if (tok[0]=='G') strcpy(gs, tok+2);
        else if (tok[0]=='F') strcpy(fo, tok+2);
        else if (tok[0]=='N') strcpy(no, tok+2);
        tok = strtok(NULL, ";");
    }
    free(hb);
    uint32_t n_off = strtoul(no, NULL, 16);
    uint32_t sp_off = strtoul(so, NULL, 16);
    uint32_t cd_off = strtoul(co, NULL, 16);
    uint32_t gc = strtoul(gs, NULL, 16);
    uint32_t tdf_off = strtoul(fo, NULL, 16);

    // Инициализируем main_module
    memset(&main_module, 0, sizeof(main_module));
    main_module.code_start = code + cd_off;
    while (*main_module.code_start == ' ') main_module.code_start++;
    main_module.code_size = code_size - (main_module.code_start - code);

    // Парсим пул строк основного модуля
    const uint8_t* pool = code + sp_off;
    const uint8_t* const_pool = code + n_off;  // таблица констант
    const uint8_t* code_ptr = code + cd_off;
    while (*code_ptr == ' ') code_ptr++;

    int si = 0;
    while (pool < const_pool && si < 256) {  // парсим до таблицы констант
        if (pool[0]=='<' && pool[1]=='l' && pool[2]==':') {
            pool += 3;
            int len = (int)strtoul((char*)pool, NULL, 16);
            pool += 4;
            if (*pool != '>') break;
            pool++;
            char* str = malloc(len + 1);
            memcpy(str, pool, len);
            str[len] = '\0';
            pool += len;
            vm->string_pool[si] = str;
            vm->string_pool_lengths[si] = len;  // ← СОХРАНЯЕМ ДЛИНУ
            main_module.string_pool[si] = str;  // Делим пул с main_module
            si++;
        } else break;
    }
    vm->string_pool_count = si;
    main_module.string_pool_count = si;

    // Парсим ТДФ основного модуля
    vm_parse_main_tdf(code, tdf_off, code_size);

    // Инициализируем глобальные переменные
    vm->global_count = gc;
    vm->globals = malloc(gc * sizeof(Value));
    for (uint32_t i = 0; i < gc; i++) vm->globals[i] = val_undef();

    vm->code = code_ptr;
    vm->code_size = code_size - (int)(code_ptr - code);
    vm->ip = (uint8_t*)code_ptr;
    vm->frame = NULL;

    // Главный цикл выполнения
    while (1) {
        if (vm->ip >= vm->code + vm->code_size) break;
        if (*vm->ip == ' ') { vm->ip++; continue; }
        if (*vm->ip == '#') {
            while (vm->ip < vm->code + vm->code_size && *vm->ip != '\n') vm->ip++;
            if (vm->ip < vm->code + vm->code_size && *vm->ip == '\n') vm->ip++;
            continue;
        }
        if (*vm->ip != '<') break;
        vm->ip++;
        uint8_t op = *vm->ip++;
        if (*vm->ip++ != ':') break;
        const uint8_t* pp = vm->ip;
        uint32_t param = read_hex_param(&pp);
        vm->ip = (uint8_t*)pp;
        if (*vm->ip++ != '>') break;
        InstrHandler h = vm->handlers[op];
        if (h) h(vm, param);
        else break;
        if (op == 'E') break;
    }
}
