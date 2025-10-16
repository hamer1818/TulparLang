# TulparLang - Quick Start 🚀

**TulparLang** is a C-based, simple and powerful programming language with full Lexer, Parser, and Interpreter implementation.

## 🌍 Cross-Platform Support

TulparLang works on **all major platforms**:

- ✅ **Linux** (Ubuntu, Fedora, Arch, etc.)
- ✅ **macOS** (Intel & Apple Silicon)
- ✅ **Windows** (MinGW, Visual Studio, WSL)

**Quick Install**: See `QUICK_INSTALL.md` | **Platform Details**: See `PLATFORM_SUPPORT.md`

## 🎯 Features

### Data Types

- `int` - Integers
- `float` - Floating point numbers
- `str` - Strings - **UTF-8 supported** ✨
- `bool` - Boolean (true/false)
- `array` - Mixed type arrays (PHP-style) ✨
- `arrayInt` - Type-safe integer arrays ✨
- `arrayFloat` - Type-safe float arrays ✨
- `arrayStr` - Type-safe string arrays ✨
- `arrayBool` - Type-safe boolean arrays ✨
- `arrayJson` - JSON-like mixed arrays (nested support, **object literal support** ✨) ✨

### Operators

#### Arithmetic

- `+`, `-`, `*`, `/` - Basic operations

#### Comparison

- `==`, `!=`, `<`, `>`, `<=`, `>=`

#### Logical (Phase 1) ✨

- `&&` - AND
- `||` - OR
- `!` - NOT

#### Increment/Decrement (Phase 1) ✨

- `++` - Increment
- `--` - Decrement

#### Compound Assignment (Phase 1) ✨

- `+=`, `-=`, `*=`, `/=`

### Control Flow

- `if` / `else` - Conditional statements
- `while` - While loop
- `for` - C-style for loop
- `for..in` - JavaScript-style foreach loop
- `break` - Break loop ✨
- `continue` - Skip iteration ✨

### Functions

```TulparLang
func add(int a, int b) {
    return a + b;
}

int result = add(5, 3);  // result = 8

// UTF-8 support for identifiers! ✨
func topla(int sayı1, int sayı2) {
    return sayı1 + sayı2;
}

// Escape sequences in strings ✨ NEW!
str message = "Line 1\nLine 2";           // Newline
str path = "C:\\Users\\Desktop";          // Backslash
str json = "{\"name\": \"John\"}";        // Quote
str tabs = "Name:\tJohn\nAge:\t25";       // Tab
```

### Built-in Functions

#### I/O

- `print(...)` - Print (multiple arguments)
- `input(prompt)` - String input
- `inputInt(prompt)` - Integer input
- `inputFloat(prompt)` - Float input

#### Type Conversion (Phase 1) ✨

- `toInt(value)` - Convert to int
- `toFloat(value)` - Convert to float
- `toString(value)` - Convert to string
- `toBool(value)` - Convert to bool

#### Array Functions (Phase 2) ✨

- `length(arr)` - Array/string length
- `push(arr, val)` - Add element
- `pop(arr)` - Remove last element

#### Utilities

- `range(n)` - Generate range for for..in

### Arrays (Phase 2) ✨

```TulparLang
// 1. Mixed type arrays
array mixed = [1, "Ali", 3.14, true];

// 2. Type-safe arrays
arrayInt numbers = [1, 2, 3, 4, 5];
arrayStr names = ["Ali", "Veli", "Ayşe"];

// 3. JSON-like arrays (nested support) ✨ NEW!
arrayJson user = ["Ali", 25, true, "Engineer"];
arrayJson apiResponse = [200, "Success", true];
arrayJson nested = [["user1", 25], ["user2", 30]];

// 4. Type safety
push(numbers, 6);      // ✅ OK
push(numbers, "err");  // ❌ ERROR! Only int allowed

// 5. Operations
int len = length(numbers);
push(numbers, 7);
int last = pop(numbers);
```

## 🔧 Build & Run

### 1. Build

```bash
# Automatic (CMake or Makefile)
./build.sh          # Linux/macOS/WSL

# or
build.bat           # Windows

# or manual
make                # Unix-like
```

### 2. Run Examples

```bash
# Linux/macOS/WSL
./TulparLang examples/01_hello_world.tpr

# Windows
TulparLang.exe examples\01_hello_world.tpr
```

## 📚 Examples

13 comprehensive examples in `examples/` folder:

### Basic (01-04)

- 01: Hello World - All data types & operators
- 02: Control Flow - if/else, conditions
- 03: Loops - while, for, for..in
- 04: Functions - Basic & recursive

### Intermediate (05-08)

- 05: Arrays - All array operations
- 06: Interactive - User input
- 07: Number Game - Game with loops
- 08: Calculator - Advanced calculator

### Advanced (09-16) ✨

- 09: Advanced Functions - Fibonacci, Prime, etc.
- 10: Phase 1 Test - All Phase 1 features
- 11: Phase 2 Test - Type-safe arrays
- 12: Interactive Calculator
- 13: JSON Arrays - JSON-like data
- 14: JSON Objects - Object literals ✨ **NEW!**
- 15: Nested Access - Chained access ✨ **NEW!**
- 16: Escape Sequences - String formatting ✨ **NEW!**

## 🚀 Quick Example

```TulparLang
// Variables (UTF-8 support! ✨)
int x = 10;
str name = "TulparLang";
str şehir = "İstanbul";  // Turkish characters work!

// Escape sequences ✨
print("Line 1\nLine 2");
print("Path: C:\\Users\\Desktop");
print("JSON: {\"name\": \"TulparLang\"}");

// Function
func greet(str name) {
    print("Hello,", name, "!");
}

greet(name);

// JSON Objects ✨ NEW!
arrayJson user = {
    "name": "Alice",
    "age": 25,
    "contact": {
        "email": "alice@example.com"
    }
};

// Chained access ✨ NEW!
str email = user["contact"]["email"];
print("Email:", email);  // alice@example.com

// Arrays
arrayJson data = ["Alice", 25, true];
print("User:", data);

// Loops
for (i in range(5)) {
    print("Count:", i);
}
```

## 📖 Documentation

- `README.md` - Main documentation (Turkish)
- `README_EN.md` - This file (English)
- `QUICKSTART.md` - Quick start guide (Turkish)
- `QUICK_INSTALL.md` - Installation guide
- `PLATFORM_SUPPORT.md` - Platform details
- `CHANGELOG.md` - Version history

## 🎓 Learning Path

1. Start with `examples/01_hello_world.tpr`
2. Follow examples 02-04 for basics
3. Try 05-08 for intermediate features
4. Explore 09-13 for advanced topics

## 🌟 Features Summary

| Feature | Status | Phase |
|---------|--------|-------|
| Variables (int, float, str, bool) | ✅ | Core |
| Functions | ✅ | Core |
| Control Flow (if/else, loops) | ✅ | Core |
| Logical Operators (&&, \|\|, !) | ✅ | Phase 1 |
| Increment/Decrement (++, --) | ✅ | Phase 1 |
| Compound Assignment (+=, -=, etc) | ✅ | Phase 1 |
| Break & Continue | ✅ | Phase 1 |
| Type Conversion | ✅ | Phase 1 |
| Arrays (mixed) | ✅ | Phase 2 |
| Type-safe Arrays | ✅ | Phase 2 |
| JSON-like Arrays | ✅ | Phase 2 |
| **UTF-8 Support** | ✅ | **Phase 3** ✨ |
| **JSON Objects (Hash Table)** | ✅ | **Phase 3** ✨ |
| **Nested Objects** | ✅ | **Phase 3** ✨ |
| **Chained Access** | ✅ | **Phase 3** ✨ |
| **Escape Sequences** | ✅ | **Phase 3** ✨ |

## 📊 Statistics

- **Total Lines**: ~4000+ (without comments)
- **Example Files**: 16
- **Data Types**: 9 (int, float, str, bool, array, arrayInt, arrayFloat, arrayStr, arrayBool, arrayJson)
- **Built-in Functions**: 12+
- **Supported Platforms**: Linux, macOS, Windows
- **Encoding**: UTF-8 (Turkish and international character support)
- **Hash Table Buckets**: 16 (djb2 algorithm)

## 🎯 Highlighted Features

1. **UTF-8 Support** 🌍 - Turkish and international characters
2. **JSON Objects** � - Fast key-value access with hash tables
3. **Nested Structures** 🔗 - Unlimited nesting depth
4. **Chained Access** ⛓️ - Access like `data["users"][0]["profile"]["email"]`
5. **Escape Sequences** 🔤 - Professional string formatting (`\n`, `\t`, `\"`, etc.)
6. **Type Safety** 🛡️ - Type-safe arrays for secure code
7. **Cross-Platform** 💻 - Linux, macOS, Windows support

## �🔮 Future Features

- [ ] Dot notation - `obj.key.nested` syntax
- [ ] Object methods - `keys()`, `values()`, `merge()`
- [ ] Spread operator - `...obj`, `...arr`
- [ ] String methods (split, join, substring)
- [ ] Class/Struct support
- [ ] Import/Module system
- [ ] Better error messages
- [ ] Optimization and JIT compilation

See `GELECEK_OZELLIKLER.md` for roadmap (Turkish)

## 📝 License

This project is developed for educational purposes. Feel free to use, modify, and distribute.

## 👨‍💻 Developer

**Hamza Ortatepe** - TulparLang Creator  
GitHub: [@hamer1818](https://github.com/hamer1818)

## 🔗 Links

- **GitHub Repository**: <https://github.com/hamer1818/TulparLang>
- **VS Code Extension**: <https://github.com/hamer1818/olan-ext>
- **Documentation**: `README.md` (Turkish), `README_EN.md` (English), `QUICKSTART.md`

---

**TulparLang Version**: 1.3.0 (UTF-8 + JSON Objects + Escape Sequences)  
**Last Update**: October 13, 2025  
**Platform Support**: Linux, macOS, Windows

**Happy Coding on All Platforms!** 🚀🌍
