#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// #define TRACE_EXECUTION

// === TYPES AND GLOBALS ===

typedef size_t Word;

typedef enum { VAL_INT = 0,
               VAL_STR,
               VAL_COUNT } ValueType;

typedef enum {
  TOK_EOF = 0,
  TOK_INT,
  TOK_STR,
  TOK_PLUS,
  TOK_MINUS,
  TOK_STAR,
  TOK_SLASH,
  TOK_DUP,
  TOK_DOT,
  TOK_COUNT
} TokenType;

typedef struct {
  const char *filepath;
  int row, col;
} Location;

typedef struct {
  Location Location;

  const char *data;
  int len;

  TokenType type;
} Token;

typedef struct {
  char *data;
  int len;
} String;

typedef struct {
  ValueType type;
  union {
    Word word;
    int integer;
    char *cstr;
  };
} Value;

#define STACK_CAPACITY 256

typedef struct ArenaChunk {
  struct ArenaChunk *next;
  int size;
  int offset;
  char mem[];
} ArenaChunk;

typedef struct {
  ArenaChunk *chunk;
} Arena;

typedef enum {
  INSTR_INT,
  INSTR_STRING,
  INSTR_ADD,
  INSTR_SUB,
  INSTR_MUL,
  INSTR_DIV,
  INSTR_DUP,
  INSTR_DUMP,
  INSTR_DONE,
  INSTR_COUNT,
} Instr;

typedef struct {
  Word program[STACK_CAPACITY];
  int ip;

  Value stack[STACK_CAPACITY];
  int sp;

  char data[STACK_CAPACITY];
  int data_offset;
} VM;
VM vm;

// === FORWARD DECLARATIONS ===
void vm_push_instr(Instr instr, Word arg);
bool vm_run();
ArenaChunk *arena_chunk_create(int chunk_size);
Arena arena_create(int chunk_size);
void arena_destroy(Arena *a);
void *arena_alloc(Arena *a, int size);
void tokens_init(void);
void tokens_free(void);
Token *make_token(const Token *source);
Token *next_token(void);
void token_print(const Token *token);
void value_print(Value value);
void vm_dump(void);
void vm_dump_stack(void);
const char *instr_to_cstr(Instr instr);
void compile(void);
int get_file_size(const char *filename);
bool read_entire_file(const char *filename, Arena *arena);

// === DEFINITIONS ===
void vm_push_instr(Instr instr, Word arg) {
  assert(vm.ip < STACK_CAPACITY);

  static_assert(INSTR_COUNT == 9, "Update Instr is required");
  switch (instr) {
  case INSTR_INT:
    assert(vm.ip + 1 < STACK_CAPACITY);
    vm.program[vm.ip++] = (Word)instr;
    vm.program[vm.ip++] = arg;
    break;

  case INSTR_STRING: {
    assert(vm.ip + 1 < STACK_CAPACITY);
    vm.program[vm.ip++] = (Word)instr;
    String string = *(String *)arg;
    memcpy(vm.data + vm.data_offset, string.data, string.len);
    vm.program[vm.ip++] = (Word)vm.data_offset;
    vm.data_offset += string.len;
    vm.data[vm.data_offset++] = '\0';
  } break;

  case INSTR_ADD:
  case INSTR_SUB:
  case INSTR_MUL:
  case INSTR_DIV:
  case INSTR_DUMP:
  case INSTR_DUP:
  case INSTR_DONE:
    vm.program[vm.ip++] = (Word)instr;
    break;

  default:
    assert(0 && "unreachable");
  }
}

bool vm_run() {
  vm.ip = 0;

#ifdef TRACE_EXECUTION
  vm_dump();
  printf("\n");
#endif

  for (Instr instr = (Instr)vm.program[vm.ip]; instr != INSTR_DONE;
       instr = (Instr)vm.program[vm.ip]) {
    static_assert(INSTR_COUNT == 9, "Update Instr is required");
    switch (instr) {
    case INSTR_INT: {
      assert(vm.sp < STACK_CAPACITY);
      assert(vm.ip + 1 < STACK_CAPACITY);
      int value = vm.program[++vm.ip];
      vm.stack[vm.sp++] = (Value){.type = VAL_INT, .integer = value};
      vm.ip += 1;
    } break;

    case INSTR_STRING: {
      assert(vm.sp < STACK_CAPACITY);
      assert(vm.ip + 1 < STACK_CAPACITY);
      int offset = (int)vm.program[++vm.ip];
      vm.stack[vm.sp++] = (Value){.type = VAL_STR, .cstr = vm.data + offset};
      vm.ip += 1;
    } break;

    case INSTR_ADD:
    case INSTR_SUB:
    case INSTR_MUL:
    case INSTR_DIV: {
      assert(vm.sp >= 2);
      Value b = vm.stack[--vm.sp];
      Value a = vm.stack[--vm.sp];
      assert(a.type == VAL_INT && b.type == VAL_INT);
      if (instr == INSTR_ADD) {
        vm.stack[vm.sp++] =
            (Value){.type = VAL_INT, .integer = a.integer + b.integer};
      } else if (instr == INSTR_SUB) {
        vm.stack[vm.sp++] =
            (Value){.type = VAL_INT, .integer = a.integer - b.integer};
      } else if (instr == INSTR_MUL) {
        vm.stack[vm.sp++] =
            (Value){.type = VAL_INT, .integer = a.integer * b.integer};
      } else {
        vm.stack[vm.sp++] =
            (Value){.type = VAL_INT, .integer = a.integer / b.integer};
      }
      vm.ip += 1;
    } break;

    case INSTR_DUP: {
      assert(vm.sp >= 1 && vm.sp + 1 < STACK_CAPACITY);
      vm.stack[vm.sp] = vm.stack[vm.sp - 1];
      vm.sp += 1;
      vm.ip += 1;
    } break;

    case INSTR_DUMP:
      assert(vm.sp >= 1);
      value_print(vm.stack[--vm.sp]);
      vm.ip += 1;
      break;

    default:
      assert(0 && "unreachable");
    }

#ifdef TRACE_EXECUTION
    printf("%s\n", instr_to_cstr(instr));
    vm_dump();
    printf("\n");
#endif
  }

  return true;
}

ArenaChunk *arena_chunk_create(int chunk_size) {
  ArenaChunk *chunk = malloc(sizeof(ArenaChunk) + chunk_size);
  if (chunk == NULL) {
    fprintf(stderr, "Error: memory issue...");
    abort();
  }
  chunk->next = NULL;
  chunk->size = chunk_size;
  chunk->offset = 0;
  return chunk;
}

Arena arena_create(int chunk_size) {
  return (Arena){arena_chunk_create(chunk_size)};
}

void arena_destroy(Arena *a) {
  ArenaChunk *chunk = a->chunk;
  while (chunk) {
    ArenaChunk *next = chunk->next;
    free(chunk);
    chunk = next;
  }
  a->chunk = NULL;
}

void *arena_alloc(Arena *a, int size) {
  if (a->chunk->size < size)
    return NULL;

  ArenaChunk *chunk = a->chunk;
  while (chunk && chunk->offset + size > chunk->size)
    chunk = chunk->next;

  if (!chunk) {
    chunk = arena_chunk_create(a->chunk->size);
    chunk->next = a->chunk;
  }

  void *ptr = chunk->mem + chunk->offset;
  chunk->offset += size;
  return ptr;
}

#define TOKENS_CHUNK_SIZE (1024 * sizeof(Token))
Arena tokens;
int tp;         // token pointer
ArenaChunk *cp; // chunk pointer

void tokens_init(void) { tokens = arena_create(TOKENS_CHUNK_SIZE); }
void tokens_free(void) { arena_destroy(&tokens); }

Token *make_token(const Token *source) {
  Token *token = arena_alloc(&tokens, sizeof(Token));
  *token = *source;
  return token;
}

bool tokenize(const char *source) {
  if (NULL == source)
    return true;

  const char *ptr = source;

  while (*ptr) {
    while (*ptr && isspace(*ptr))
      ++ptr;

    if (isdigit(*ptr) || (*ptr == '-' && isdigit(ptr[1]))) {
      const char *end = ptr;
      end += *end == '-';
      while (*end && isdigit(*end))
        ++end;
      Token token = {(Location){0}, ptr, end - ptr, TOK_INT};
      make_token(&token);
      ptr = end;
    } else if (*ptr == '"') {
      const char *end = ptr + 1;
      while (*end && *end != '"')
        ++end;
      Token token = {(Location){0}, ptr + 1, end - ptr - 1, TOK_STR};
      make_token(&token);
      ptr = end + 1;
    } else if (strncmp(ptr, "dup", 3) == 0) {
      Token token = {(Location){0}, ptr, 3, TOK_DUP};
      make_token(&token);
      ptr += 3;
    } else if (*ptr == '*') {
      Token token = {(Location){0}, ptr, 1, TOK_STAR};
      make_token(&token);
      ptr += 1;
    } else if (*ptr == '+') {
      Token token = {(Location){0}, ptr, 1, TOK_PLUS};
      make_token(&token);
      ptr += 1;
    } else if (*ptr == '/') {
      Token token = {(Location){0}, ptr, 1, TOK_SLASH};
      make_token(&token);
      ptr += 1;
    } else if (*ptr == '-') {
      Token token = {(Location){0}, ptr, 1, TOK_MINUS};
      make_token(&token);
      ptr += 1;
    } else if (*ptr == '.') {
      Token token = {(Location){0}, ptr, 1, TOK_DOT};
      make_token(&token);
      ptr += 1;
    }
  }

  Token token = {(Location){0}, NULL, 0, TOK_EOF};
  make_token(&token);

  return true;
}

Token *next_token(void) {
  if (!cp)
    cp = tokens.chunk;

  if (tp >= cp->size) {
    tp = 0;
    cp = cp->next;
    if (!cp) {
      fprintf(stderr, "Error: Unexpected EOF");
      exit(1);
    }
  }

  Token *token_arr = (Token *)cp->mem;

  return &token_arr[tp++];
}

void token_print(const Token *token) {
  static_assert(TOK_COUNT == 9, "Update TokenType is required");
  switch (token->type) {
  case TOK_INT:
    printf("int %.*s\n", token->len, token->data);
    break;
  case TOK_STR:
    printf("str %.*s\n", token->len, token->data);
    break;
  case TOK_PLUS:
  case TOK_MINUS:
  case TOK_STAR:
  case TOK_SLASH:
  case TOK_DOT:
  case TOK_DUP:
    printf("%.*s\n", token->len, token->data);
    break;
  case TOK_EOF:
    printf("EOF\n");
    break;
  default:
    assert(0 && "unreachable");
  }
}

void value_print(Value value) {
  static_assert(VAL_COUNT == 2, "Update ValueType is required");
  switch (value.type) {
  case VAL_INT:
    printf("%d\n", value.integer);
    break;
  case VAL_STR:
    printf("%s\n", value.cstr);
    break;
  default:
    assert(0 && "unreachable");
  }
}

void vm_dump_stack(void) {
  printf("stack[%d]:\n", vm.sp);
  for (int i = 0; i < vm.sp; ++i) {
    printf("  ");
    value_print(vm.stack[i]);
  }
}

void vm_dump(void) {
  printf("VM:\n");

  printf("ip = %d\n", vm.ip);
  printf("program:\n");
  Instr instr;
  for (int ip = 0; ip < STACK_CAPACITY;) {
    instr = vm.program[ip];
    if (instr == INSTR_DONE)
      break;

    static_assert(INSTR_COUNT == 9, "Update Instr is required");
    switch (instr) {
    case INSTR_INT: {
      assert(ip + 1 < STACK_CAPACITY);
      int value = vm.program[++ip];
      printf("int(%d) ", value);
      ip += 1;
    } break;
    case INSTR_STRING:
      break;
    case INSTR_ADD:
      printf("+ ");
      ip += 1;
      break;
    case INSTR_SUB:
      printf("- ");
      ip += 1;
      break;
    case INSTR_MUL:
      printf("* ");
      ip += 1;
      break;
    case INSTR_DIV:
      printf("/ ");
      ip += 1;
      break;
    case INSTR_DUP:
      printf("dup ");
      ip += 1;
      break;
    case INSTR_DUMP:
      printf(". ");
      ip += 1;
      break;
    case INSTR_DONE:
      printf("done ");
      ip += 1;
      break;
    case INSTR_COUNT:
      assert(0 && "unreachable");
    default:
      assert(0 && "unreachable");
    }
  }
  printf("\n");

  vm_dump_stack();

  printf("data: is not supported yet\n");
}

const char *instr_to_cstr(Instr instr) {
  // clang-format off
  static_assert(INSTR_COUNT == 9, "Update Instr is required");
  switch (instr) {
  case INSTR_INT:    return "INSTR_INT";
  case INSTR_STRING: return "INSTR_STRING";
  case INSTR_ADD:    return "INSTR_ADD";
  case INSTR_SUB:    return "INSTR_SUB";
  case INSTR_MUL:    return "INSTR_MUL";
  case INSTR_DIV:    return "INSTR_DIV";
  case INSTR_DUP:    return "INSTR_DUP";
  case INSTR_DUMP:   return "INSTR_DUMP";
  case INSTR_DONE:   return "INSTR_DONE";
  case INSTR_COUNT:  return "INSTR_COUNT";
  }
  assert(0 && "unreachable");
  // clang-format on
}

void compile(void) {
  for (Token *t = next_token(); t->type != TOK_EOF; t = next_token()) {
    // clang-format off
    static_assert(TOK_COUNT == 9, "Update TokenType is required");
    switch (t->type) {
    case TOK_INT: {
      int i = atoi(t->data);
      vm_push_instr(INSTR_INT, i);
    } break;
    case TOK_STR: {
      String string = {(char *)t->data, t->len};
      vm_push_instr(INSTR_STRING, (Word)(&string));
    } break;
    case TOK_PLUS:  vm_push_instr(INSTR_ADD, 0); break;
    case TOK_MINUS: vm_push_instr(INSTR_SUB, 0); break;
    case TOK_STAR:  vm_push_instr(INSTR_MUL, 0); break;
    case TOK_SLASH: vm_push_instr(INSTR_DIV, 0); break;
    case TOK_DUP:   vm_push_instr(INSTR_DUP, 0); break;
    case TOK_DOT:   vm_push_instr(INSTR_DUMP, 0); break;
    default:
      assert(0 && "unreachable");
    }
    // clang-format off
  }
  vm_push_instr(INSTR_DONE, 0);
}

int get_file_size(const char *filename) {
  FILE *f = fopen(filename, "rb");
  if (!f) {
    fprintf(stderr, "Error: could not open the file %s: %s\n", filename, strerror(errno));
    return -1;
  }

  if (fseek(f, 0, SEEK_END) != 0) {
    fprintf(stderr, "Error: fseek failed: %s\n", strerror(errno));
    fclose(f);
    return -1;
  }

  int size = ftell(f);
  fclose(f);
  return size;
}

bool read_entire_file(const char *filename, Arena *arena) {
  bool result = true;

  int size = get_file_size(filename);
  if (arena->chunk->size < size + 1)
    return false;

  FILE *f = fopen(filename, "rb");
  if (f == NULL) {
    result = false;
    goto defer;
  }

  fread(arena->chunk->mem, size, 1, f);
  if (ferror(f)) {
    result = false;
    goto defer;
  }
  arena->chunk->mem[size] = '\0';

defer:
  if (!result)
    fprintf(stderr, "Could not read file %s: %s\n", filename, strerror(errno));
  if (f)
    fclose(f);
  return result;
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <source.step>\n", argv[0]);
    return 1;
  }

  char *source_filename = argv[1];
  int source_file_size = get_file_size(source_filename);
  if (source_file_size < 0)
    return 1;

  Arena source_arena = arena_create(source_file_size + 1);
  if (!read_entire_file(source_filename, &source_arena))
    return 1;

  tokens_init();

  tokenize(source_arena.chunk->mem);
  compile();
  vm_run();

  tokens_free();

  return 0;
}
