# Quick Start Guide

Welcome to the TulparLang Quick Start guide. This page will help you install Tulpar, write and run your first program, and see a few core features in practice.

## Installation (Summary)

For detailed installation steps, see the main `README.md` and `docs/PLATFORM_SUPPORT.md`. This section is just a short summary.

- **Requirements**
  - GCC or Clang
  - LLVM 18+
  - CMake 3.14+

- **Linux / macOS**

  ```bash
  git clone https://github.com/hamer1818/TulparLang.git
  cd TulparLang
  ./build.sh
  ```

- **Windows**

  ```powershell
  git clone https://github.com/hamer1818/TulparLang.git
  cd TulparLang
  .\build.bat
  ```

When the build finishes successfully, you should see the `tulpar` executable in the project directory.

## Your First Program

Create a file named `hello.tpr` in the project directory and put the following code into it:

```tulpar
str message = "Hello, Tulpar!";
print(message);
```

Run the program:

```bash
./tulpar hello.tpr
```

Expected output:

```
Hello, Tulpar!
```

## A Quick Look at Core Language Concepts

### Variables and Basic Types

```tulpar
int age = 25;
float pi = 3.14159;
str name = "Tulpar";
bool active = true;

print(name, ":", age);
```

### Control Flow

```tulpar
int x = 10;

if (x > 0) {
    print("positive");
} else {
    print("non-positive");
}

for (int i = 0; i < 3; i++) {
    print("i =", i);
}
```

### Functions

```tulpar
func square(int n) {
    return n * n;
}

print(square(5));  // 25
```

## Mini Example with JSON and Arrays

One of Tulpar’s strengths is first-class support for JSON values and arrays.

```tulpar
json user = {
    "name": "Hamza",
    "age": 25,
    "skills": ["C", "Python", "Tulpar"]
};

arrayJson users = [
    user,
    {"name": "Alice", "age": 30, "skills": ["Tulpar"]}
];

for (int i = 0; i < length(users); i++) {
    json u = users[i];
    print("User:", u["name"], "- Age:", u["age"]);
}
```

In this small example you see:

- JSON literal definition,
- Reading fields from JSON (`u["name"]`),
- Iterating over a JSON array,
- Using the `length` function.

## Using the REPL (Interactive Shell)

Tulpar provides a REPL (Read–Eval–Print Loop) mode so you can quickly experiment with code:

```bash
./tulpar --repl
```

Inside the REPL:

- Type `exit` or `quit` to leave.
- Use `help` to see a short help message.
- Use `clear` to clear the screen.

You can type simple expressions and immediately see the result:

```text
>>> 1 + 2 * 3
7
>>> print("Hello")
Hello
```

## AOT (Ahead-of-Time) Compilation to a Native Binary

You can compile Tulpar programs directly to a native binary. For example, for `hello.tpr`:

```bash
./tulpar --aot hello.tpr hello_bin
```

This command will produce an executable named `hello_bin`. Then you can run:

```bash
./hello_bin
```

to see the output.

## Next Steps

- For a detailed explanation of the language syntax and all features: `docs/KULLANIM.md`
- For a full reference of math functions: `docs/MATH_FUNCTIONS.md`
- For platform-specific installation and common issues: `docs/PLATFORM_SUPPORT.md`
- For more example programs: check the `examples/` directory (`01_hello_world.tpr`, `02_basics.tpr`, `04_math_logic.tpr`, `06_data_structures.tpr`, etc.).

You are now ready to start experimenting with Tulpar. Continue with the language reference and examples to explore more.