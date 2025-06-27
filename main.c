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
typedef size_t word_t;

#define WORD_UNION \
  union {          \
    word_t word;   \
    int integer;   \
    float float_;  \
    char *cstr;    \
  }

typedef WORD_UNION Word;
Word word0;

typedef enum { VAL_INT = 0,
               VAL_FLOAT,
               VAL_STR,
               VAL_COUNT } ValueType;

typedef enum {
  // keywords
  TOK_EOF = 0,
  TOK_PLUS,
  TOK_MINUS,
  TOK_STAR,
  TOK_SLASH,
  TOK_MOD,
  TOK_PLUS_DOT,
  TOK_MINUS_DOT,
  TOK_STAR_DOT,
  TOK_SLASH_DOT,
  TOK_EQ,
  TOK_NEQ,
  TOK_LT,
  TOK_LE,
  TOK_GT,
  TOK_GE,
  TOK_DUP,
  TOK_OVER,
  TOK_SWAP,
  TOK_DROP,
  TOK_ROT,
  TOK_DOT,

  TOK_KW_COUNT,

  // literals
  TOK_INT,
  TOK_FLOAT,
  TOK_STR,

  TOK_COUNT
} TokenType;

typedef struct {
  const char *filename;
  int line, col;
} Location;

typedef struct {
  char *data;
  int len;
} String;

typedef struct {
  const char *data;
  int len;
} SV;

typedef struct {
  Location Location;
  SV source;
  TokenType type;
} Token;

typedef struct {
  ValueType type;
  WORD_UNION;
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
  INSTR_FLOAT,
  INSTR_STRING,
  INSTR_ADD,
  INSTR_SUB,
  INSTR_MUL,
  INSTR_DIV,
  INSTR_MOD,
  INSTR_ADDF,
  INSTR_SUBF,
  INSTR_MULF,
  INSTR_DIVF,
  INSTR_EQ,
  INSTR_NEQ,
  INSTR_LT,
  INSTR_LE,
  INSTR_GT,
  INSTR_GE,
  INSTR_DUP,
  INSTR_OVER,
  INSTR_SWAP,
  INSTR_DROP,
  INSTR_ROT,
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
bool tokenize(const char *source, const char *filename);
void compile(void);
int get_file_size(const char *filename);
bool read_entire_file(const char *filename, Arena *arena);
bool sv_eq(SV lhs, SV rhs);
bool sv_contains(SV sv, SV substr);
SV sv_strip(SV sv);
SV sv_stripr(SV sv);
SV sv_stripl(SV sv);
SV sv_chop(SV *sv, SV delim);

// default constuctor takes cstr
#define sv(cstr) \
  (SV) { (cstr), strlen((cstr)) }

// constuctor from literal
#define svl(literal) \
  (SV){(literal), sizeof((literal)) - 1}

// constuctor-initializer from literal
#define svli(literal) \
  {(literal), sizeof((literal)) - 1}

// constuctor from String
#define svs(string) \
  (SV) { (string).data, (string).len }

#define sv_advance(sv) (++(sv).data, --(sv).len)
#define sv_slice(sv, offset, len) \
  (SV) { (sv).data + (offset), (len) }
#define svf(sv) (sv).len, (sv).data

static_assert(TOK_KW_COUNT == 22, "Update TokenType is required");
SV keywords[TOK_KW_COUNT] = {
    [TOK_EOF] = svli("\0"),
    [TOK_PLUS] = svli("+"),
    [TOK_MINUS] = svli("-"),
    [TOK_STAR] = svli("*"),
    [TOK_SLASH] = svli("/"),
    [TOK_MOD] = svli("%"),
    [TOK_PLUS_DOT] = svli("+."),
    [TOK_MINUS_DOT] = svli("-."),
    [TOK_STAR_DOT] = svli("*."),
    [TOK_SLASH_DOT] = svli("/."),
    [TOK_EQ] = svli("="),
    [TOK_NEQ] = svli("!="),
    [TOK_LT] = svli("<"),
    [TOK_LE] = svli("<="),
    [TOK_GT] = svli(">"),
    [TOK_GE] = svli(">="),
    [TOK_DUP] = svli("dup"),
    [TOK_OVER] = svli("over"),
    [TOK_SWAP] = svli("swap"),
    [TOK_DROP] = svli("drop"),
    [TOK_ROT] = svli("rot"),
    [TOK_DOT] = svli("."),
};

// === DEFINITIONS ===
void vm_push_instr(Instr instr, Word arg) {
  assert(vm.ip < STACK_CAPACITY);

  static_assert(INSTR_COUNT == 25, "Update Instr is required");
  switch (instr) {
  case INSTR_INT:
  case INSTR_FLOAT:
    assert(vm.ip + 1 < STACK_CAPACITY);
    vm.program[vm.ip++] = (Word){.word = instr};
    vm.program[vm.ip++] = arg;
    break;

  case INSTR_STRING: {
    assert(vm.ip + 1 < STACK_CAPACITY);
    vm.program[vm.ip++] = (Word){.word = instr};
    SV string = *(SV *)arg.word;
    memcpy(vm.data + vm.data_offset, string.data, string.len);
    vm.program[vm.ip++] = (Word){.integer = vm.data_offset};
    vm.data_offset += string.len;
    vm.data[vm.data_offset++] = '\0';
  } break;

  case INSTR_ADD:
  case INSTR_SUB:
  case INSTR_MUL:
  case INSTR_DIV:
  case INSTR_MOD:
  case INSTR_ADDF:
  case INSTR_SUBF:
  case INSTR_MULF:
  case INSTR_DIVF:
  case INSTR_EQ:
  case INSTR_NEQ:
  case INSTR_LT:
  case INSTR_LE:
  case INSTR_GT:
  case INSTR_GE:
  case INSTR_DUMP:
  case INSTR_DUP:
  case INSTR_OVER:
  case INSTR_SWAP:
  case INSTR_DROP:
  case INSTR_ROT:
  case INSTR_DONE:
    vm.program[vm.ip++] = (Word){.word = instr};
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

  for (Instr instr = (Instr)vm.program[vm.ip].word; instr != INSTR_DONE;
       instr = (Instr)vm.program[vm.ip].word) {
    static_assert(INSTR_COUNT == 25, "Update Instr is required");
    switch (instr) {
    case INSTR_INT:
    case INSTR_FLOAT: {
      assert(vm.sp < STACK_CAPACITY);
      assert(vm.ip + 1 < STACK_CAPACITY);
      word_t word = vm.program[++vm.ip].word;
      vm.stack[vm.sp++] = (Value){.type = instr == INSTR_INT ? VAL_INT : VAL_FLOAT, .word = word};
      vm.ip += 1;
    } break;

    case INSTR_STRING: {
      assert(vm.sp < STACK_CAPACITY);
      assert(vm.ip + 1 < STACK_CAPACITY);
      int offset = vm.program[++vm.ip].integer;
      vm.stack[vm.sp++] = (Value){.type = VAL_STR, .cstr = vm.data + offset};
      vm.ip += 1;
    } break;

    case INSTR_ADD:
    case INSTR_SUB:
    case INSTR_MUL:
    case INSTR_DIV:
    case INSTR_MOD: {
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
      } else if (instr == INSTR_DIV) {
        vm.stack[vm.sp++] =
            (Value){.type = VAL_INT, .integer = a.integer / b.integer};
      } else {
        vm.stack[vm.sp++] =
            (Value){.type = VAL_INT, .integer = a.integer % b.integer};
      }
      vm.ip += 1;
    } break;

    case INSTR_ADDF:
    case INSTR_SUBF:
    case INSTR_MULF:
    case INSTR_DIVF: {
      assert(vm.sp >= 2);
      Value b = vm.stack[--vm.sp];
      Value a = vm.stack[--vm.sp];
      assert(a.type == VAL_FLOAT && b.type == VAL_FLOAT);
      if (instr == INSTR_ADDF) {
        vm.stack[vm.sp++] =
            (Value){.type = VAL_FLOAT, .float_ = a.float_ + b.float_};
      } else if (instr == INSTR_SUBF) {
        vm.stack[vm.sp++] =
            (Value){.type = VAL_FLOAT, .float_ = a.float_ - b.float_};
      } else if (instr == INSTR_MULF) {
        vm.stack[vm.sp++] =
            (Value){.type = VAL_FLOAT, .float_ = a.float_ * b.float_};
      } else {
        vm.stack[vm.sp++] =
            (Value){.type = VAL_FLOAT, .float_ = a.float_ / b.float_};
      }
      vm.ip += 1;
    } break;

    case INSTR_EQ: {
      assert(vm.sp >= 2);
      Value b = vm.stack[--vm.sp];
      Value a = vm.stack[--vm.sp];
      assert(a.type == VAL_INT && b.type == VAL_INT);
      vm.stack[vm.sp++] = (Value){VAL_INT, .integer = a.integer == b.integer};
      vm.ip += 1;
    } break;
    case INSTR_NEQ: {
      assert(vm.sp >= 2);
      Value b = vm.stack[--vm.sp];
      Value a = vm.stack[--vm.sp];
      assert(a.type == VAL_INT && b.type == VAL_INT);
      vm.stack[vm.sp++] = (Value){VAL_INT, .integer = a.integer != b.integer};
      vm.ip += 1;
    } break;
    case INSTR_LT: {
      assert(vm.sp >= 2);
      Value b = vm.stack[--vm.sp];
      Value a = vm.stack[--vm.sp];
      assert(a.type == VAL_INT && b.type == VAL_INT);
      vm.stack[vm.sp++] = (Value){VAL_INT, .integer = a.integer < b.integer};
      vm.ip += 1;
    } break;
    case INSTR_LE: {
      assert(vm.sp >= 2);
      Value b = vm.stack[--vm.sp];
      Value a = vm.stack[--vm.sp];
      assert(a.type == VAL_INT && b.type == VAL_INT);
      vm.stack[vm.sp++] = (Value){VAL_INT, .integer = a.integer <= b.integer};
      vm.ip += 1;
    } break;
    case INSTR_GT: {
      assert(vm.sp >= 2);
      Value b = vm.stack[--vm.sp];
      Value a = vm.stack[--vm.sp];
      assert(a.type == VAL_INT && b.type == VAL_INT);
      vm.stack[vm.sp++] = (Value){VAL_INT, .integer = a.integer > b.integer};
      vm.ip += 1;
    } break;
    case INSTR_GE: {
      assert(vm.sp >= 2);
      Value b = vm.stack[--vm.sp];
      Value a = vm.stack[--vm.sp];
      assert(a.type == VAL_INT && b.type == VAL_INT);
      vm.stack[vm.sp++] = (Value){VAL_INT, .integer = a.integer >= b.integer};
      vm.ip += 1;
    } break;

    case INSTR_DUP: {
      assert(vm.sp >= 1 && vm.sp + 1 < STACK_CAPACITY);
      vm.stack[vm.sp] = vm.stack[vm.sp - 1];
      vm.sp += 1;
      vm.ip += 1;
    } break;

    case INSTR_OVER: {
      assert(vm.sp >= 2 && vm.sp + 1 < STACK_CAPACITY);
      vm.stack[vm.sp] = vm.stack[vm.sp - 2];
      vm.sp += 1;
      vm.ip += 1;
    } break;

    case INSTR_SWAP: {
      assert(vm.sp >= 2);
      Value tmp = vm.stack[vm.sp - 1];
      vm.stack[vm.sp - 1] = vm.stack[vm.sp - 2];
      vm.stack[vm.sp - 2] = tmp;
      vm.ip += 1;
    } break;

    case INSTR_DROP: {
      assert(vm.sp >= 1);
      vm.sp -= 1;
      vm.ip += 1;
    } break;

    case INSTR_ROT: {
      assert(vm.sp >= 3);
      Value tmp = vm.stack[vm.sp - 3];
      vm.stack[vm.sp - 3] = vm.stack[vm.sp - 2];
      vm.stack[vm.sp - 2] = vm.stack[vm.sp - 1];
      vm.stack[vm.sp - 1] = tmp;
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

bool tokenize(const char *source, const char *filename) {
  if (NULL == source)
    return true;

  Location loc = {.filename = filename, .col = 1, .line = 0};
  Location prev_loc = loc;
  int last_token_len = 0;
  SV sv = sv(source);
  while (true) {
    SV line = sv_chop(&sv, svl("\n"));
    if (line.len <= 0) {
      if (loc.line == prev_loc.line)
        loc.col += last_token_len;
      make_token(&(Token){loc, sv, TOK_EOF});
      printf("%d:%d: ", loc.line, loc.col);
      token_print(&(Token){loc, sv, TOK_EOF});
      return true;
    }

    const char *line_start = line.data;
    loc.line += 1;
    loc.col = 1;

    while (line.len > 0) {
      line = sv_strip(line);
      if (line.len <= 0)
        break;

      SV token_text;
      TokenType type = TOK_COUNT;
      if (*line.data == '"') {
        // string
        int i = 1;
        while (i < line.len && line.data[i] != '"')
          i += 1;
        if (line.data[i] != '"')
          return false; // TODO: parse error
        token_text = sv_slice(line, 1, i - 1);
        line = sv_slice(line, i + 1, line.len - i - 1);
        loc.col = token_text.data - line_start;
        type = TOK_STR;
      } else {
        token_text = sv_strip(sv_chop(&line, svl(" ")));
        loc.col = token_text.data - line_start + 1;

        if ((token_text.len > 0 && isdigit(token_text.data[0])) || (token_text.len > 1 && token_text.data[0] == '-' && isdigit(token_text.data[1]))) {
          type = TOK_INT;
          SV tmp = token_text;
          while (tmp.len > 0) {
            sv_chop(&tmp, svl("."));
            if (tmp.data[-1] == '.') {
              if (type != TOK_FLOAT)
                type = TOK_FLOAT;
              else
                return false; // TODO: parse error
            } else
              break;
          }
        } else {
          // keyword
          for (int i = 0; i < TOK_KW_COUNT; ++i) {
            if (sv_eq(token_text, keywords[i])) {
              type = (TokenType)i;
              break;
            }
          }
          assert(TOK_COUNT != type);
        }
      }
      last_token_len = token_text.len;
      prev_loc = loc;
      make_token(&(Token){loc, token_text, type});
      printf("%d:%d: ", loc.line, loc.col);
      token_print(&(Token){loc, token_text, type});
    }
  }

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
  static_assert(TOK_COUNT == 26, "Update TokenType is required");
  switch (token->type) {
  case TOK_INT:
    printf("int %.*s\n", token->source.len, token->source.data);
    break;
  case TOK_FLOAT:
    printf("float %.*s\n", token->source.len, token->source.data);
    break;
  case TOK_STR:
    printf("str %.*s\n", token->source.len, token->source.data);
    break;
  case TOK_PLUS:
  case TOK_MINUS:
  case TOK_STAR:
  case TOK_SLASH:
  case TOK_MOD:
  case TOK_PLUS_DOT:
  case TOK_MINUS_DOT:
  case TOK_STAR_DOT:
  case TOK_SLASH_DOT:
  case TOK_EQ:
  case TOK_NEQ:
  case TOK_LT:
  case TOK_LE:
  case TOK_GT:
  case TOK_GE:
  case TOK_DOT:
  case TOK_DUP:
  case TOK_OVER:
  case TOK_SWAP:
  case TOK_DROP:
  case TOK_ROT:
    printf("%.*s\n", token->source.len, token->source.data);
    break;
  case TOK_EOF:
    printf("EOF\n");
    break;
  default:
    assert(0 && "unreachable");
  }
}

void value_print(Value value) {
  static_assert(VAL_COUNT == 3, "Update ValueType is required");
  switch (value.type) {
  case VAL_INT:
    printf("%d\n", value.integer);
    break;
  case VAL_STR:
    printf("%s\n", value.cstr);
    break;
  case VAL_FLOAT:
    printf("%g\n", value.float_);
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
    instr = vm.program[ip].word;
    if (instr == INSTR_DONE)
      break;

    static_assert(INSTR_COUNT == 25, "Update Instr is required");
    switch (instr) {
    case INSTR_INT: {
      assert(ip + 1 < STACK_CAPACITY);
      int value = vm.program[++ip].integer;
      printf("int(%d) ", value);
      ip += 1;
    } break;
    case INSTR_FLOAT: {
      assert(ip + 1 < STACK_CAPACITY);
      float value = vm.program[++ip].float_;
      printf("float(%g) ", value);
      ip += 1;
    } break;
    case INSTR_STRING:
      assert(ip + 1 < STACK_CAPACITY);
      Word word = vm.program[++ip];
      printf("\"%s\"", (char *)word.cstr);
      ip += 1;
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
    case INSTR_ADDF:
      printf("+. ");
      ip += 1;
      break;
    case INSTR_SUBF:
      printf("-. ");
      ip += 1;
      break;
    case INSTR_MULF:
      printf("*. ");
      ip += 1;
      break;
    case INSTR_DIVF:
      printf("/. ");
      ip += 1;
      break;
    case INSTR_MOD:
      printf("%% ");
      ip += 1;
      break;

    case INSTR_EQ:
      printf("=");
      ip += 1;
      break;
    case INSTR_NEQ:
      printf("!=");
      ip += 1;
      break;
    case INSTR_LT:
      printf("<");
      ip += 1;
      break;
    case INSTR_LE:
      printf("<=");
      ip += 1;
      break;
    case INSTR_GT:
      printf(">");
      ip += 1;
      break;
    case INSTR_GE:
      printf(">=");
      ip += 1;
      break;

    case INSTR_DUP:
      printf("dup ");
      ip += 1;
      break;
    case INSTR_OVER:
      printf("over ");
      ip += 1;
      break;
    case INSTR_SWAP:
      printf("swap ");
      ip += 1;
      break;
    case INSTR_DROP:
      printf("drop ");
      ip += 1;
      break;
    case INSTR_ROT:
      printf("rot ");
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
  static_assert(INSTR_COUNT == 25, "Update Instr is required");
  switch (instr) {
  case INSTR_INT:    return "INSTR_INT";
  case INSTR_FLOAT:  return "INSTR_FLOAT";
  case INSTR_STRING: return "INSTR_STRING";
  case INSTR_ADD:    return "INSTR_ADD";
  case INSTR_SUB:    return "INSTR_SUB";
  case INSTR_MUL:    return "INSTR_MUL";
  case INSTR_DIV:    return "INSTR_DIV";
  case INSTR_MOD:    return "INSTR_MOD";
  case INSTR_ADDF:   return "INSTR_ADDF";
  case INSTR_SUBF:   return "INSTR_SUBF";
  case INSTR_MULF:   return "INSTR_MULF";
  case INSTR_DIVF:   return "INSTR_DIVF";
  case INSTR_EQ:     return "INSTR_EQ";
  case INSTR_NEQ:    return "INSTR_NEQ";
  case INSTR_LT:     return "INSTR_LT";
  case INSTR_LE:     return "INSTR_LE";
  case INSTR_GT:     return "INSTR_GT";
  case INSTR_GE:     return "INSTR_GE";
  case INSTR_DUP:    return "INSTR_DUP";
  case INSTR_OVER:   return "INSTR_OVER";
  case INSTR_SWAP:   return "INSTR_SWAP";
  case INSTR_DROP:   return "INSTR_DROP";
  case INSTR_ROT:    return "INSTR_ROT";
  case INSTR_DUMP:   return "INSTR_DUMP";
  case INSTR_DONE:   return "INSTR_DONE";
  case INSTR_COUNT:  return "INSTR_COUNT";
  default:
      assert(0 && "unreachable");
  }
  // clang-format on
}

void compile(void) {
  for (Token *token = next_token(); token->type != TOK_EOF; token = next_token()) {
    // clang-format off
    static_assert(TOK_COUNT == 26, "Update TokenType is required");
    switch (token->type) {
    case TOK_INT: {
      int i = atoi(token->source.data);
      vm_push_instr(INSTR_INT, (Word){.integer=i});
    } break;

    case TOK_FLOAT: {
      float f = strtof(token->source.data, NULL);
      vm_push_instr(INSTR_FLOAT, (Word){.float_=f});
    } break;

    case TOK_STR: {
      SV string = {(char *)token->source.data, token->source.len};
      vm_push_instr(INSTR_STRING, (Word){.word=(word_t)&string});
    } break;

    case TOK_PLUS:      vm_push_instr(INSTR_ADD, word0); break;
    case TOK_MINUS:     vm_push_instr(INSTR_SUB, word0); break;
    case TOK_STAR:      vm_push_instr(INSTR_MUL, word0); break;
    case TOK_SLASH:     vm_push_instr(INSTR_DIV, word0); break;
    case TOK_MOD:       vm_push_instr(INSTR_MOD, word0); break;
    case TOK_PLUS_DOT:  vm_push_instr(INSTR_ADDF, word0); break;
    case TOK_MINUS_DOT: vm_push_instr(INSTR_SUBF, word0); break;
    case TOK_STAR_DOT:  vm_push_instr(INSTR_MULF, word0); break;
    case TOK_SLASH_DOT: vm_push_instr(INSTR_DIVF, word0); break;
    case TOK_EQ:        vm_push_instr(INSTR_EQ, word0); break;
    case TOK_NEQ:       vm_push_instr(INSTR_NEQ, word0); break;
    case TOK_LT:        vm_push_instr(INSTR_LT, word0); break;
    case TOK_LE:        vm_push_instr(INSTR_LE, word0); break;
    case TOK_GT:        vm_push_instr(INSTR_GT, word0); break;
    case TOK_GE:        vm_push_instr(INSTR_GE, word0); break;
    case TOK_DUP:       vm_push_instr(INSTR_DUP, word0); break;
    case TOK_DOT:       vm_push_instr(INSTR_DUMP, word0); break;
    case TOK_OVER:      vm_push_instr(INSTR_OVER, word0); break;
    case TOK_SWAP:      vm_push_instr(INSTR_SWAP, word0); break;
    case TOK_DROP:      vm_push_instr(INSTR_DROP, word0); break;
    case TOK_ROT:       vm_push_instr(INSTR_ROT, word0); break;
    default:
      assert(0 && "unreachable");
    }
    // clang-format off
  }
  vm_push_instr(INSTR_DONE, word0);
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

bool sv_eq(SV lhs, SV rhs) {
  return lhs.len == rhs.len && strncmp(lhs.data, rhs.data, lhs.len) == 0;
}

bool sv_contains(SV sv, SV substr) {
  for (int i = 0; sv.len - i >= substr.len; ++i) {
    if (strncmp(sv.data + i, substr.data, substr.len) == 0)
      return true;
  }
  return false;
}

SV sv_strip(SV sv) {
  return sv_stripl(sv_stripr(sv));
}

SV sv_stripr(SV sv) {
  while (sv.len > 0 && isspace(sv.data[sv.len-1])) {
    sv.len -= 1;
  }
  return sv;
}

SV sv_stripl(SV sv) {
  while (sv.len > 0 && isspace((*sv.data))) {
    sv_advance(sv);
  }
  return sv;
}

SV sv_chop(SV *sv, SV delim) {
  int offset = 0;
  while (sv->len - offset >= delim.len 
    && strncmp(sv->data + offset, delim.data, delim.len) != 0) {
    offset += 1;
  }
  if (sv->len > 0) 
    offset += delim.len;
  SV result = sv_slice(*sv, 0, offset);
  *sv = sv_slice(*sv, offset, sv->len - offset);
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

  if (!tokenize(source_arena.chunk->mem, source_filename))
    return 1;
  compile();
  vm_run();

  tokens_free();

  return 0;
}
