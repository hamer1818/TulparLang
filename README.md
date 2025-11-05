# TulparLang 🐎

<div align="center">

![TulparLang Logo](https://img.shields.io/badge/TulparLang-v1.6.0-blue?style=for-the-badge)
[![License](https://img.shields.io/badge/license-MIT-green?style=for-the-badge)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey?style=for-the-badge)](PLATFORM_SUPPORT.md)
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen?style=for-the-badge)](BUILD.md)

**A modern, C-based programming language with UTF-8 support, JSON-native syntax, and comprehensive built-in libraries**

[Quick Start](#-quick-start) • [Documentation](#-documentation) • [Examples](#-examples) • [Contributing](#-contributing)

</div>

---

## 📋 Table of Contents

- [Overview](#-overview)
- [Key Features](#-key-features)
- [Installation](#-installation)
- [Quick Start](#-quick-start)
- [Language Syntax](#-language-syntax)
- [Built-in Functions](#-built-in-functions)
- [Examples](#-examples)
- [Architecture](#-architecture)
- [Project Structure](#-project-structure)
- [Platform Support](#-platform-support)
- [Roadmap](#-roadmap)
- [Contributing](#-contributing)
- [License](#-license)

---

## 🎯 Overview

TulparLang is a modern, statically-typed programming language built from scratch in C. It features a complete implementation including Lexer, Parser, and Interpreter, designed for both educational purposes and practical applications.

### Design Philosophy

- **Simplicity First**: Clean, readable syntax inspired by C and JavaScript
- **Type Safety**: Strong typing with type-safe arrays and structs
- **Modern Features**: JSON-native syntax, UTF-8 support, and extensive standard library
- **Cross-Platform**: Write once, run anywhere (Linux, macOS, Windows)

### Statistics

| Metric | Value |
|--------|-------|
| **Total Lines of Code** | ~4,300+ |
| **Built-in Functions** | 50+ |
| **Data Types** | 9 |
| **Example Programs** | 19 |
| **Supported Platforms** | 3 |

---

## ✨ Key Features

### 🌍 UTF-8 Support
Full Unicode support for variable names, strings, and identifiers
```tulpar
str şehir = "İstanbul";
str ülke = "Türkiye";
```

### 📦 JSON-Native Syntax
First-class JSON object support with hash table implementation
```tulpar
arrayJson user = {
    "name": "Hamza",
    "age": 25,
    "city": "İstanbul"
};
```

### 🔗 Chained Access
Unlimited depth nested object and array access
```tulpar
str email = company["ceo"]["contact"]["email"];
```

### 📐 Math Library
27 built-in mathematical functions including trigonometry, logarithms, and random numbers
```tulpar
float result = sqrt(pow(3.0, 2.0) + pow(4.0, 2.0));  // 5.0
```

### 🔤 String Operations
16 comprehensive string manipulation functions
```tulpar
str clean = lower(trim("  HELLO  "));  // "hello"
arrayStr parts = split("a,b,c", ",");   // ["a", "b", "c"]
```

### 🛡️ Type Safety
Type-safe arrays and custom struct definitions
```tulpar
type Person {
    str name;
    int age;
    str city = "İstanbul";
}
```

---

## 🚀 Installation

### Prerequisites

- GCC or Clang compiler
- Make (optional but recommended)
- CMake 3.10+ (optional)

### Build from Source

#### Linux / macOS

```bash
git clone https://github.com/hamer1818/TulparLang.git
cd TulparLang
chmod +x build.sh
./build.sh
```

#### Windows (WSL)

```bash
git clone https://github.com/hamer1818/TulparLang.git
cd TulparLang
wsl bash build.sh
```

#### Windows (Native)

```cmd
git clone https://github.com/hamer1818/TulparLang.git
cd TulparLang
build.bat
```

### Using Makefile

```bash
make          # Build the project
make clean    # Clean build artifacts
make run      # Build and run demo
```

For detailed platform-specific instructions, see [PLATFORM_SUPPORT.md](PLATFORM_SUPPORT.md) and [QUICK_INSTALL.md](QUICK_INSTALL.md).

---

## 🎯 Quick Start

### Your First Program

Create a file named `hello.tpr`:

```tulpar
// Hello World with UTF-8 support
str greeting = "Merhaba Dünya!";
print(greeting);

// Function definition
func square(int n) {
    return n * n;
}

// Usage
int result = square(5);
print("5'in karesi:", result);
```

### Run the Program

```bash
./tulpar hello.tpr          # Linux/macOS
wsl ./tulpar hello.tpr      # Windows (WSL)
```

### Interactive REPL Mode

```bash
./tulpar                    # Run without arguments for demo mode
```

---

## 📚 Language Syntax

### Data Types

| Type | Description | Example |
|------|-------------|---------|
| `int` | Integer numbers | `int x = 42;` |
| `float` | Floating-point numbers | `float pi = 3.14;` |
| `str` | UTF-8 strings | `str name = "Hamza";` |
| `bool` | Boolean values | `bool flag = true;` |
| `array` | Mixed-type arrays | `array mix = [1, "text", 3.14];` |
| `arrayInt` | Type-safe integer arrays | `arrayInt nums = [1, 2, 3];` |
| `arrayFloat` | Type-safe float arrays | `arrayFloat vals = [1.5, 2.5];` |
| `arrayStr` | Type-safe string arrays | `arrayStr names = ["Ali", "Veli"];` |
| `arrayBool` | Type-safe boolean arrays | `arrayBool flags = [true, false];` |
| `arrayJson` | JSON-like objects | `arrayJson obj = {"key": "value"};` |

### Variables and Constants

```tulpar
// Variable declaration
int x = 10;
float y = 3.14;
str name = "TulparLang";
bool active = true;

// Compound assignment
x += 5;   // x = 15
x *= 2;   // x = 30

// Increment/Decrement
x++;      // x = 31
x--;      // x = 30
```

### Control Flow

#### If-Else Statements

```tulpar
int age = 18;

if (age >= 18) {
    print("Adult");
} else {
    print("Minor");
}

// Logical operators
if (age >= 18 && age < 65) {
    print("Working age");
}
```

#### While Loop

```tulpar
int i = 0;
while (i < 10) {
    if (i == 5) continue;
    if (i == 8) break;
    print(i);
    i++;
}
```

#### For Loop

```tulpar
// C-style for loop
for (int i = 0; i < 10; i++) {
    print("i =", i);
}

// For-each with range
for (i in range(10)) {
    print("i =", i);
}
```

### Functions

```tulpar
// Function definition
func add(int a, int b) {
    return a + b;
}

// Recursive function
func fibonacci(int n) {
    if (n <= 1) {
        return n;
    }
    return fibonacci(n - 1) + fibonacci(n - 2);
}

// Function call
int sum = add(5, 3);
int fib = fibonacci(10);
```

### Arrays and Objects

```tulpar
// Type-safe arrays
arrayInt numbers = [1, 2, 3, 4, 5];
arrayStr names = ["Ali", "Veli", "Ayşe"];

// Array operations
int len = length(numbers);
push(numbers, 6);
int last = pop(numbers);

// JSON objects
arrayJson user = {
    "name": "Hamza",
    "age": 25,
    "email": "hamza@example.com"
};

// Nested objects
arrayJson company = {
    "name": "Tech Corp",
    "ceo": {
        "name": "Hamza",
        "contact": {
            "email": "hamza@techcorp.com"
        }
    }
};

// Chained access
str email = company["ceo"]["contact"]["email"];
```

### Object Dot Notation and Assignment ✨ NEW

```tulpar
arrayJson person = { "name": "Ali", "age": 25, "city": "İstanbul" };

// Read with dot notation
print(person.name, person.age, person.city);

// Write with dot (nested supported)
person.name = "Veli";
arrayJson order = { "customer": { "address": { "city": "Bursa" } } };
order.customer.address.city = "Ankara";

// Mixed with bracket
person["city"] = "İzmir";
```

### Custom Types (Structs)

```tulpar
// Type definition with default values
type Person {
    str name;
    int age;
    str city = "İstanbul";
}

// Constructor with named arguments
Person p1 = Person("Ali", 25, "Ankara");
Person p2 = Person(name: "Ayşe", age: 30);  // Uses default city

// Access and modify
print(p1.name, p1.age);
p1.city = "İzmir";
```

### Comments ✨ NEW

```tulpar
// Single-line comment

/*
 Multi-line
 block comment
*/
```

### String Features

#### Escape Sequences

```tulpar
print("Line 1\nLine 2");              // Newline
print("Name:\tHamza");                // Tab
print("Path: C:\\Users\\Desktop");    // Backslash
print("JSON: {\"key\": \"value\"}");  // Quotes
```

#### String Indexing

```tulpar
str text = "Hello";
print(text[0]);  // "H"
print(text[4]);  // "o"

// With JSON objects
arrayJson data = {"name": "Alice"};
print(data["name"][0]);  // "A"
```

---

## 🛠️ Built-in Functions

### Input/Output

| Function | Description | Example |
|----------|-------------|---------|
| `print(...)` | Print values to console | `print("Hello", x, y);` |
| `input(prompt)` | Read string from user | `str name = input("Name: ");` |
| `inputInt(prompt)` | Read integer | `int age = inputInt("Age: ");` |
| `inputFloat(prompt)` | Read float | `float val = inputFloat("Value: ");` |

### Type Conversion

| Function | Description | Example |
|----------|-------------|---------|
| `toInt(value)` | Convert to integer | `int x = toInt("123");` |
| `toFloat(value)` | Convert to float | `float y = toFloat("3.14");` |
| `toString(value)` | Convert to string | `str s = toString(42);` |
| `toBool(value)` | Convert to boolean | `bool b = toBool(1);` |

### Array Operations

| Function | Description | Example |
|----------|-------------|---------|
| `length(arr)` | Get array/object length | `int len = length(arr);` |
| `push(arr, value)` | Add element | `push(arr, 10);` |
| `pop(arr)` | Remove and return last | `int x = pop(arr);` |
| `range(n)` | Create integer range | `for (i in range(10)) {...}` |

### Mathematics (27 Functions)

#### Basic Operations
```tulpar
abs(x)           // Absolute value
sqrt(x)          // Square root
cbrt(x)          // Cube root
pow(x, y)        // Power (x^y)
hypot(x, y)      // Hypotenuse
```

#### Rounding
```tulpar
floor(x)         // Round down
ceil(x)          // Round up
round(x)         // Round to nearest
trunc(x)         // Truncate decimal
```

#### Trigonometry
```tulpar
sin(x), cos(x), tan(x)           // Basic trig
asin(x), acos(x), atan(x)        // Inverse trig
atan2(y, x)                      // Two-argument arctan
sinh(x), cosh(x), tanh(x)        // Hyperbolic
```

#### Logarithms and Exponentials
```tulpar
exp(x)           // e^x
log(x)           // Natural log (ln)
log10(x)         // Base-10 log
log2(x)          // Base-2 log
```

#### Statistics and Random
```tulpar
min(a, b, ...)   // Minimum value
max(a, b, ...)   // Maximum value
random()         // Random float [0,1)
randint(a, b)    // Random int [a,b]
```

For complete documentation, see [MATH_FUNCTIONS.md](MATH_FUNCTIONS.md).

### String Operations (16 Functions)

#### Transformation
```tulpar
upper(s)              // Convert to uppercase
lower(s)              // Convert to lowercase
capitalize(s)         // Capitalize first letter
reverse(s)            // Reverse string
```

#### Search and Check
```tulpar
contains(s, sub)      // Check if contains substring
startsWith(s, pre)    // Check prefix
endsWith(s, suf)      // Check suffix
indexOf(s, sub)       // Find first occurrence
count(s, sub)         // Count occurrences
```

#### Manipulation
```tulpar
trim(s)               // Remove whitespace
replace(s, old, new)  // Replace substring
substring(s, i, j)    // Extract substring
repeat(s, n)          // Repeat string n times
```

#### Array Operations
```tulpar
split(s, delim)       // Split into array
join(sep, arr)        // Join array to string
```

#### Validation
```tulpar
isEmpty(s)            // Check if empty
isDigit(s)            // Check if all digits
isAlpha(s)            // Check if all letters
```

---

## 💡 Examples

### Example 1: Calculator

```tulpar
print("=== TulparLang Calculator ===");

int a = inputInt("First number: ");
int b = inputInt("Second number: ");

print("Sum:", a + b);
print("Difference:", a - b);
print("Product:", a * b);
print("Division:", a / b);
```

### Example 2: Fibonacci Sequence

```tulpar
func fibonacci(int n) {
    if (n <= 1) {
        return n;
    }
    return fibonacci(n - 1) + fibonacci(n - 2);
}

print("Fibonacci sequence:");
for (int i = 0; i < 10; i++) {
    print("F(" + toString(i) + ") =", fibonacci(i));
}
```

### Example 3: JSON Data Processing

```tulpar
arrayJson users = {
    "data": [
        {"name": "Alice", "age": 25, "role": "Developer"},
        {"name": "Bob", "age": 30, "role": "Designer"},
        {"name": "Charlie", "age": 35, "role": "Manager"}
    ]
};

// Process user data
for (int i = 0; i < length(users["data"]); i++) {
    arrayJson user = users["data"][i];
    str name = user["name"];
    int age = user["age"];
    str role = user["role"];
    
    print(name, "-", age, "years old -", role);
}
```

### Example 4: String Processing

```tulpar
str email = "  HAMZA@EXAMPLE.COM  ";

// Clean and parse email
str clean = lower(trim(email));
arrayStr parts = split(clean, "@");
str username = parts[0];
str domain = parts[1];

print("Username:", username);
print("Domain:", domain);
print("Valid:", contains(domain, "."));
```

### Example 5: Mathematical Computation

```tulpar
// Calculate circle properties
float radius = 5.0;
float pi = 3.14159;

float area = pi * pow(radius, 2.0);
float circumference = 2.0 * pi * radius;

print("Radius:", radius);
print("Area:", area);
print("Circumference:", circumference);

// Random point in circle
float angle = random() * 2.0 * pi;
float r = random() * radius;
float x = r * cos(angle);
float y = r * sin(angle);

print("Random point: (", x, ",", y, ")");
```

More examples available in the [examples/](examples/) directory.

---

## 🏗️ Architecture

TulparLang consists of three main components:

### 1. Lexer (Tokenization)

Converts source code into tokens:

```
int x = 5; → [INT_TYPE, IDENTIFIER, ASSIGN, INT_LITERAL, SEMICOLON]
```

**Features:**
- UTF-8 character support
- Escape sequence handling
- Object literal tokenization
- Multi-character operators

### 2. Parser (AST Generation)

Builds an Abstract Syntax Tree from tokens:

```
VAR_DECL: x
  └── INT: 5
```

**Features:**
- Object literal parsing
- Chained array access (unlimited depth)
- Type declarations
- Expression precedence

### 3. Interpreter (Execution)

Executes the AST:

**Features:**
- Symbol table for variables
- Function table with recursion support
- Scope management (global/local)
- Hash table for objects (djb2 algorithm)
- Deep copy support
- Type system with structs

---

## 📁 Project Structure

```
TulparLang/
├── src/
│   ├── lexer/
│   │   ├── lexer.c              # Tokenization with UTF-8
│   │   └── lexer.h
│   ├── parser/
│   │   ├── parser.c             # AST generation
│   │   └── parser.h
│   ├── interpreter/
│   │   ├── interpreter.c        # Runtime execution
│   │   └── interpreter.h
│   └── main.c                   # Entry point
├── build/                       # Build artifacts
├── examples/                    # 19 example programs
│   ├── 01_hello.tpr
│   ├── 02_variables.tpr
│   ├── 14_json_objects.tpr
│   ├── 15_nested_access.tpr
│   ├── 16_escape_sequences.tpr
│   ├── 17_math_demo.tpr
│   └── 18_string_indexing.tpr
├── docs/                        # Documentation
│   ├── QUICKSTART.md
│   ├── KULLANIM.md
│   ├── MATH_FUNCTIONS.md
│   └── GELECEK_OZELLIKLER.md
├── Makefile
├── CMakeLists.txt
├── build.sh                     # Build script (Unix)
├── build.bat                    # Build script (Windows)
├── README.md                    # This file
├── README_EN.md                 # English version
└── LICENSE
```

---

## 🌐 Platform Support

TulparLang runs on all major platforms:

| Platform | Status | Notes |
|----------|--------|-------|
| **Linux** | ✅ Fully Supported | Ubuntu, Fedora, Arch, etc. |
| **macOS** | ✅ Fully Supported | Intel & Apple Silicon |
| **Windows** | ✅ Fully Supported | MinGW, Visual Studio, WSL |

For detailed platform-specific instructions, see [PLATFORM_SUPPORT.md](PLATFORM_SUPPORT.md).

---

## 🗺️ Roadmap

### Completed ✅

- [x] Phase 1: Core language features
  - [x] Basic data types
  - [x] Functions and recursion
  - [x] Control flow structures
  - [x] Logical operators
  - [x] Increment/decrement
  - [x] Type conversion

- [x] Phase 2: Arrays
  - [x] Mixed-type arrays
  - [x] Type-safe arrays
  - [x] JSON arrays
  - [x] Array operations

- [x] Phase 3: Advanced Features
  - [x] UTF-8 support
  - [x] JSON object literals
  - [x] Nested objects
  - [x] Chained access
  - [x] Escape sequences
  - [x] Custom types (structs)

- [x] Phase 4: Math & String Libraries
  - [x] 27 math functions
  - [x] String indexing
  - [x] 16 string operations

### In Progress 🚧

- [ ] Dot notation for objects (`obj.key.nested`)
- [ ] Object methods (`keys()`, `values()`, `merge()`)
- [ ] Spread operator (`...obj`, `...arr`)
- [ ] Module/Import system

### Planned 📋

- [ ] File I/O operations
- [ ] Error handling (try/catch)
- [ ] Lambdas and closures
- [ ] Standard library expansion
- [ ] Optimizations and JIT compilation
- [ ] Package manager
- [ ] Documentation generator

For detailed roadmap, see [GELECEK_OZELLIKLER.md](GELECEK_OZELLIKLER.md).

---

## 🤝 Contributing

We welcome contributions! Here's how you can help:

### Getting Started

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Make your changes
4. Run tests and ensure code quality
5. Commit your changes (`git commit -m 'Add amazing feature'`)
6. Push to the branch (`git push origin feature/amazing-feature`)
7. Open a Pull Request

### Development Guidelines

- Follow existing code style and conventions
- Add tests for new features
- Update documentation as needed
- Write clear commit messages
- Comment complex logic

### Areas for Contribution

- 🐛 Bug fixes
- ✨ New language features
- 📚 Documentation improvements
- 🧪 Test coverage
- 🎨 VS Code extension features
- 🌍 Translations

---

## 📖 Documentation

- **[Quick Start Guide](QUICKSTART.md)** - Get started in 5 minutes
- **[User Manual](KULLANIM.md)** - Complete language reference (Turkish)
- **[Math Functions](MATH_FUNCTIONS.md)** - Mathematical library documentation
- **[Platform Support](PLATFORM_SUPPORT.md)** - Platform-specific guides
- **[Future Features](GELECEK_OZELLIKLER.md)** - Roadmap and planned features

---

## 🔗 Resources

- **GitHub Repository**: [github.com/hamer1818/TulparLang](https://github.com/hamer1818/TulparLang)
- **VS Code Extension**: [github.com/hamer1818/tulpar-ext](https://github.com/hamer1818/tulpar-ext)
- **Issue Tracker**: [GitHub Issues](https://github.com/hamer1818/TulparLang/issues)
- **Discussions**: [GitHub Discussions](https://github.com/hamer1818/TulparLang/discussions)

---

## 👨‍💻 Author

**Hamza Ortatepe**  
Creator and Lead Developer of TulparLang

- GitHub: [@hamer1818](https://github.com/hamer1818)
- Email: hamzaortatepe@hotmail.com

---

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

### MIT License Summary

```
Copyright (c) 2025 Hamza Ortatepe

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
```

---

## 🙏 Acknowledgments

- Inspired by C, JavaScript, and Python
- Built with passion for programming language design
- Thanks to all contributors and users

---

## 📊 Project Stats

![GitHub stars](https://img.shields.io/github/stars/hamer1818/TulparLang?style=social)
![GitHub forks](https://img.shields.io/github/forks/hamer1818/TulparLang?style=social)
![GitHub watchers](https://img.shields.io/github/watchers/hamer1818/TulparLang?style=social)

---

<div align="center">

**TulparLang v1.6.0** - Modern, UTF-8 Supported, JSON-Native Programming Language

Made with ❤️ by [Hamza Ortatepe](https://github.com/hamer1818)

⭐ **Star us on GitHub!** ⭐

</div>