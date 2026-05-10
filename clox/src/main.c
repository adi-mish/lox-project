//> Chunks of Bytecode main-c
//> Scanning on Demand main-includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//< Scanning on Demand main-includes
#include "common.h"
//> main-include-chunk
#include "chunk.h"
//< main-include-chunk
//> main-include-debug
#include "debug.h"
//< main-include-debug
//> main-include-scanner
#include "scanner.h"
//< main-include-scanner
//> A Virtual Machine main-include-vm
#include "vm.h"
//< A Virtual Machine main-include-vm
//> Scanning on Demand repl

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
//< Scanning on Demand repl
//> Scanning on Demand read-file
static char* readFile(const char* path) {
  FILE* file = fopen(path, "rb");
//> no-file
  if (file == NULL) {
    fprintf(stderr, "Could not open file \"%s\".\n", path);
    exit(74);
  }
//< no-file

  fseek(file, 0L, SEEK_END);
  size_t fileSize = ftell(file);
  rewind(file);

  char* buffer = (char*)malloc(fileSize + 1);
//> no-buffer
  if (buffer == NULL) {
    fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
    exit(74);
  }

//< no-buffer
  size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
//> no-read
  if (bytesRead < fileSize) {
    fprintf(stderr, "Could not read file \"%s\".\n", path);
    exit(74);
  }

//< no-read
  buffer[bytesRead] = '\0';

  fclose(file);
  return buffer;
}
//< Scanning on Demand read-file
//> Scanning on Demand run-file
static void runFile(const char* path) {
  char* source = readFile(path);
  InterpretResult result = interpret(source);
  free(source); // [owner]

  if (result == INTERPRET_COMPILE_ERROR) exit(65);
  if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}
//< Scanning on Demand run-file

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
//> A Virtual Machine main-init-vm
  initVM();

//< A Virtual Machine main-init-vm
/* Chunks of Bytecode main-chunk < Scanning on Demand args
  Chunk chunk;
  initChunk(&chunk);
*/
/* Chunks of Bytecode main-constant < Scanning on Demand args

  int constant = addConstant(&chunk, 1.2);
*/
/* Chunks of Bytecode main-constant < Chunks of Bytecode main-chunk-line
  writeChunk(&chunk, OP_CONSTANT);
  writeChunk(&chunk, constant);

*/
/* Chunks of Bytecode main-chunk-line < Scanning on Demand args
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, constant, 123);
*/
/* A Virtual Machine main-chunk < Scanning on Demand args

  constant = addConstant(&chunk, 3.4);
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, constant, 123);

  writeChunk(&chunk, OP_ADD, 123);

  constant = addConstant(&chunk, 5.6);
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, constant, 123);

  writeChunk(&chunk, OP_DIVIDE, 123);
*/
/* A Virtual Machine main-negate < Scanning on Demand args
  writeChunk(&chunk, OP_NEGATE, 123);
*/
/* Chunks of Bytecode main-chunk < Chunks of Bytecode main-chunk-line
  writeChunk(&chunk, OP_RETURN);
*/
/* Chunks of Bytecode main-chunk-line < Scanning on Demand args

  writeChunk(&chunk, OP_RETURN, 123);
*/
/* Chunks of Bytecode main-disassemble-chunk < Scanning on Demand args

  disassembleChunk(&chunk, "test chunk");
*/
/* A Virtual Machine main-interpret < Scanning on Demand args
  interpret(&chunk);
*/
//> Scanning on Demand args
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
//< Scanning on Demand args
/* A Virtual Machine main-free-vm < Scanning on Demand args
  freeVM();
*/
/* Chunks of Bytecode main-chunk < Scanning on Demand args
  freeChunk(&chunk);
*/
  return 0;
}
