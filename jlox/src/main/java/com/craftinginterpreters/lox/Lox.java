package com.craftinginterpreters.lox;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.charset.Charset;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.List;


public class Lox {
    private enum Mode {
        RUN,
        SCAN,
        PRINT_AST
    }

    private static final Interpreter interpreter = new Interpreter();

    static boolean hadError = false;
    static boolean hadRuntimeError = false;

    public static void main(String[] args) throws IOException {
        Mode mode = Mode.RUN;
        String path = null;

        for (String arg : args) {
            switch (arg) {
                case "--scan":
                    mode = Mode.SCAN;
                    break;
                case "--print-ast":
                    mode = Mode.PRINT_AST;
                    break;
                default:
                    if (path != null) {
                        System.out.println("Usage: jlox [--scan|--print-ast] [script]");
                        System.exit(64);
                    }
                    path = arg;
                    break;
            }
        }

        if (path != null) {
            runFile(path, mode);
        } else if (mode == Mode.RUN) {
            runPrompt();
        } else {
            System.out.println("Usage: jlox [--scan|--print-ast] [script]");
            System.exit(64);
        }
    }

    private static void runFile(String path, Mode mode) throws IOException {
        byte[] bytes = Files.readAllBytes(Paths.get(path));
        run(new String(bytes, Charset.defaultCharset()), mode);

        // Indicate an error in the exit code.
        if (hadError) System.exit(65);
        if (hadRuntimeError) System.exit(70);
    }

    private static void runPrompt() throws IOException {
        InputStreamReader input = new InputStreamReader(System.in);
        BufferedReader reader = new BufferedReader(input);

        for (; ; ) {
            System.out.print("> ");
            String line = reader.readLine();
            if (line == null) break;
            run(line, Mode.RUN);
            hadError = false;
        }
    }

    private static void run(String source, Mode mode) {
        Scanner scanner = new Scanner(source);
        List<Token> tokens = scanner.scanTokens();

        if (mode == Mode.SCAN) {
            for (Token token : tokens) {
                System.out.println(formatToken(token));
            }
            return;
        }

        Parser parser = new Parser(tokens);

        if (mode == Mode.PRINT_AST) {
            Expr expression = parser.parseExpression();
            if (hadError) return;
            System.out.println(new AstPrinter().print(expression));
            return;
        }

        List<Stmt> statements = parser.parse();

        // Stop if there was a syntax error.
        if (hadError) return;

        Resolver resolver = new Resolver(interpreter);
        resolver.resolve(statements);

        if (hadError) return;

        interpreter.interpret(statements);
    }

    static void error(int line, String message) {
        report(line, "", message);
    }

    static void runtimeError(RuntimeError error) {
        System.err.println(error.getMessage() +
                "\n[line " + error.token.line + "]");
        hadRuntimeError = true;
    }

    private static void report(int line, String where,
                               String message) {
        System.err.println(
                "[line " + line + "] Error" + where + ": " + message);
        hadError = true;
    }

    static void error(Token token, String message) {
        if (token.type == TokenType.EOF) {
            report(token.line, " at end", message);
        } else {
            report(token.line, " at '" + token.lexeme + "'", message);
        }
    }

    private static String formatToken(Token token) {
        if (token.type == TokenType.EOF) {
            return "EOF null";
        }
        if (token.literal == null) {
            return token.type + " " + token.lexeme + " null";
        }
        if (token.literal instanceof String && ((String) token.literal).isEmpty()) {
            return token.type + " " + token.lexeme;
        }
        return token.type + " " + token.lexeme + " " + token.literal;
    }
}
