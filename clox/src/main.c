// Command-line entry point and REPL for the bytecode interpreter.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "chunk.h"
#include "debug.h"
#include "scanner.h"
#include "vm.h"

static void repl() {
  char line[1024];
  for (;;) {
    printf("> ");

    if (!fgets(line, sizeof(line), stdin)) {
      printf("\n");
      break;
    }

    interpret(line);
  }
}
static char* readFile(const char* path) {
  FILE* file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "Could not open file \"%s\".\n", path);
    exit(74);
  }

  fseek(file, 0L, SEEK_END);
  size_t fileSize = ftell(file);
  rewind(file);

  char* buffer = (char*)malloc(fileSize + 1);
  if (buffer == NULL) {
    fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
    exit(74);
  }

  size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
  if (bytesRead < fileSize) {
    fprintf(stderr, "Could not read file \"%s\".\n", path);
    exit(74);
  }

  buffer[bytesRead] = '\0';

  fclose(file);
  return buffer;
}
static void runFile(const char* path) {
  char* source = readFile(path);
  InterpretResult result = interpret(source);
  free(source);

  if (result == INTERPRET_COMPILE_ERROR) exit(65);
  if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}

static const char* tokenTypeName(TokenType type) {
  switch (type) {
    case TOKEN_LEFT_PAREN: return "LEFT_PAREN";
    case TOKEN_RIGHT_PAREN: return "RIGHT_PAREN";
    case TOKEN_LEFT_BRACE: return "LEFT_BRACE";
    case TOKEN_RIGHT_BRACE: return "RIGHT_BRACE";
    case TOKEN_COMMA: return "COMMA";
    case TOKEN_DOT: return "DOT";
    case TOKEN_MINUS: return "MINUS";
    case TOKEN_PLUS: return "PLUS";
    case TOKEN_SEMICOLON: return "SEMICOLON";
    case TOKEN_SLASH: return "SLASH";
    case TOKEN_STAR: return "STAR";
    case TOKEN_BANG: return "BANG";
    case TOKEN_BANG_EQUAL: return "BANG_EQUAL";
    case TOKEN_EQUAL: return "EQUAL";
    case TOKEN_EQUAL_EQUAL: return "EQUAL_EQUAL";
    case TOKEN_GREATER: return "GREATER";
    case TOKEN_GREATER_EQUAL: return "GREATER_EQUAL";
    case TOKEN_LESS: return "LESS";
    case TOKEN_LESS_EQUAL: return "LESS_EQUAL";
    case TOKEN_IDENTIFIER: return "IDENTIFIER";
    case TOKEN_STRING: return "STRING";
    case TOKEN_NUMBER: return "NUMBER";
    case TOKEN_AND: return "AND";
    case TOKEN_CLASS: return "CLASS";
    case TOKEN_ELSE: return "ELSE";
    case TOKEN_FALSE: return "FALSE";
    case TOKEN_FOR: return "FOR";
    case TOKEN_FUN: return "FUN";
    case TOKEN_IF: return "IF";
    case TOKEN_NIL: return "NIL";
    case TOKEN_OR: return "OR";
    case TOKEN_PRINT: return "PRINT";
    case TOKEN_RETURN: return "RETURN";
    case TOKEN_SUPER: return "SUPER";
    case TOKEN_THIS: return "THIS";
    case TOKEN_TRUE: return "TRUE";
    case TOKEN_VAR: return "VAR";
    case TOKEN_WHILE: return "WHILE";
    case TOKEN_ERROR: return "ERROR";
    case TOKEN_EOF: return "EOF";
  }
  return "UNKNOWN";
}

static bool tokenContains(Token token, char needle) {
  for (int i = 0; i < token.length; i++) {
    if (token.start[i] == needle) return true;
  }
  return false;
}

static void printScanToken(Token token) {
  if (token.type == TOKEN_EOF) {
    printf("EOF null\n");
    return;
  }

  printf("%s %.*s", tokenTypeName(token.type), token.length, token.start);
  switch (token.type) {
    case TOKEN_NUMBER:
      if (tokenContains(token, '.')) {
        printf(" %.*s\n", token.length, token.start);
      } else {
        printf(" %.*s.0\n", token.length, token.start);
      }
      break;
    case TOKEN_STRING:
      if (token.length > 2) {
        printf(" %.*s\n", token.length - 2, token.start + 1);
      } else {
        printf("\n");
      }
      break;
    default:
      printf(" null\n");
      break;
  }
}

static int scanSource(const char* source) {
  initScanner(source);
  bool hadError = false;

  for (;;) {
    Token token = scanToken();
    if (token.type == TOKEN_ERROR) {
      fprintf(stderr, "[line %d] Error: %.*s\n",
              token.line, token.length, token.start);
      hadError = true;
      continue;
    }

    printScanToken(token);
    if (token.type == TOKEN_EOF) break;
  }

  return hadError ? 65 : 0;
}

static int scanFile(const char* path) {
  char* source = readFile(path);
  int result = scanSource(source);
  free(source);
  return result;
}

int main(int argc, const char* argv[]) {
  initVM();

  if (argc == 1) {
    repl();
  } else if (argc == 3 && strcmp(argv[1], "--scan") == 0) {
    int result = scanFile(argv[2]);
    freeVM();
    return result;
  } else if (argc == 2) {
    runFile(argv[1]);
  } else {
    fprintf(stderr, "Usage: clox [--scan] [path]\n");
    exit(64);
  }

  freeVM();
  return 0;
}
