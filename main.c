#include <assert.h>
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { VAL_INT, VAL_STR } ValueType;

#define STRING_CAPACITY 32
typedef struct {
  char data[STRING_CAPACITY];
} String;

typedef struct {
  ValueType type;
  union {
    int i;
    String s;
  };
} Value;

#define STACK_CAPACITY 256
Value stack[STACK_CAPACITY];
size_t sp;

void value_print(Value value) {
  switch (value.type) {
  case VAL_INT:
    printf("%d\n", value.i);
    break;
  case VAL_STR:
    printf("%s\n", value.s.data);
    break;
  default:
    assert(0 && "unreachable");
  }
}

void dump_stack(void) {
  printf("stack[%zu]:\n", sp);
  for (size_t i = 0; i < sp; ++i) {
    printf("  ");
    value_print(stack[i]);
  }
}

void run_source_code(const char *src) {
  for (const char *it = src, *start = src; *it;) {
    if (isspace(*it)) {
      if (isdigit(*start)) {
        assert(sp < STACK_CAPACITY);
        stack[sp++] = (Value){.type = VAL_INT, .i = atoi(start)};
      } else if (*start == '"') {
        if (it[-1] != '"') {
          while (*it && *it != '"')
            ++it;
          if (*it != '"') {
            fprintf(stderr, "Error: missing closing \" for stringliteral\n");
            exit(1);
          }
        }
        size_t n = it - start - 1;
        it += *it == '"';
        if (n + 1 >= STACK_CAPACITY) {
          fprintf(stderr, "Error: maximum string length is %d character\n",
                  STACK_CAPACITY - 1);
          exit(1);
        }
        String str;
        strncpy(str.data, start + 1, n);
        str.data[n + 1] = 0;
        stack[sp++] = (Value){.type = VAL_STR, .s = str};
      } else if (*start == '*') {
        if (sp < 2) {
          fprintf(stderr, "Error: operator * requires 2 int arguments\n");
          exit(1);
        }
        Value b = stack[--sp];
        Value a = stack[--sp];
        if (a.type != VAL_INT || b.type != VAL_INT) {
          fprintf(stderr, "Error: operator * requires 2 int arguments\n");
          exit(1);
        }
        stack[sp++] = (Value){.type = VAL_INT, .i = a.i * b.i};
      } else if (*start == '+') {
        if (sp < 2) {
          fprintf(stderr, "Error: operator + requires 2 int arguments\n");
          exit(1);
        }
        Value b = stack[--sp];
        Value a = stack[--sp];
        if (a.type != VAL_INT || b.type != VAL_INT) {
          fprintf(stderr, "Error: operator + requires 2 int arguments\n");
          exit(1);
        }
        stack[sp++] = (Value){.type = VAL_INT, .i = a.i + b.i};
      } else {
        fprintf(stderr, "Error: invalid token %s\n", it);
        exit(1);
      }

      while (isspace(*it))
        ++it;

      start = it;

      dump_stack();
    } else
      ++it;
  }

  if (sp) {
    Value value = stack[--sp];
    switch (value.type) {
    case VAL_INT:
      printf("%d\n", value.i);
      break;
    case VAL_STR:
      printf("%s\n", value.s.data);
      break;
    default:
      assert(0 && "unreachable");
    }
  }
}

int main(void) {
  const char *src;

  src = "1 2 3 * + ";
  run_source_code(src);
  src = "\"Hello World!\" ";
  run_source_code(src);

  return 0;
}
