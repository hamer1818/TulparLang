# TulparLang

<div align="center">

[![Version](https://img.shields.io/badge/version-2.1.0-blue.svg)](https://github.com/hamer1818/OLang/releases)
[![Build](https://github.com/hamer1818/OLang/actions/workflows/build.yml/badge.svg)](https://github.com/hamer1818/OLang/actions/workflows/build.yml)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Linux%20|%20macOS%20|%20Windows-lightgrey.svg)]()
[![C Source](https://img.shields.io/badge/source-~32K%20lines-orange.svg)]()

A statically-typed programming language with LLVM backend, JIT compiler, UTF-8 support, and native JSON syntax.

</div>

---

## Overview

TulparLang is a modern programming language built in C with an LLVM-18 backend for native code generation. It combines the simplicity of dynamic languages with the performance of compiled languages.

**Key characteristics:**
- Static typing with type inference
- First-class JSON support
- UTF-8 native strings
- LLVM AOT compilation
- x64 JIT compiler
- WebAssembly (WASM) target support
- Automatic Reference Counting (ARC) memory management
- Cross-platform (Linux, macOS, Windows with native MSVC support)

## Codebase Statistics

| Metric | Value |
|--------|-------|
| Core Source (`src/`) | ~27,700 lines of C |
| Runtime (`runtime/`) | ~4,300 lines of C |
| Standard Library (`lib/`) | ~1,550 lines of Tulpar |
| Example Programs | 25 files |
| Benchmark Suites | 8 files (C, Rust, Go, JS, PHP, Python, Tulpar) |

## Performance
 
Benchmark results comparing total execution time across algorithmic tests (lower is better):
 
| Rank | Language | Total Time | Relative to Tulpar |
|------|----------|------------|---------------------|
| 🥇 | **C (gcc -O3)** | **8.65 ms** | 1.66x faster |
| 🥈 | **Rust (1.80)** | **9.30 ms** | 1.54x faster |
| 🥉 | **Tulpar (AOT)** | **14.36 ms** | **Reference** |
| 4 | JavaScript (Node) | 57.13 ms | 3.98x slower |
| 5 | PHP 8.3 | 120.83 ms | 8.41x slower |
| 6 | Python 3.12 | 3,671 ms | 255.63x slower |
 
> **Note:** Benchmarks run on Linux 6.6.87 (WSL2). Tulpar's LLVM-backed AOT compiler achieves performance in the same ballpark as C and Rust, while being dramatically faster than interpreted languages.

## Installation

### Prerequisites

- GCC or Clang
- LLVM 18+
- CMake 3.14+

### Build from Source

```bash
git clone https://github.com/hamer1818/OLang.git
cd OLang
mkdir build && cd build
cmake ..
make
```

### Quick Install (Linux/macOS)

```bash
./build.sh
```

### Windows (Native Build)

**Prerequisites:**
- Visual Studio 2019/2022 with "Desktop development with C++" workload
- CMake 3.14+ ([Download](https://cmake.org/download/))
- LLVM 18+ for Windows ([Download](https://github.com/llvm/llvm-project/releases))

**Build:**

```batch
# Using batch script
build.bat

# Or using PowerShell
.\build.ps1

# Or using CMake directly
mkdir build-windows
cd build-windows
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

**Alternative: WSL (if needed)**

Windows users can also use WSL (Windows Subsystem for Linux):

```bash
# Install WSL
wsl --install

# Inside WSL (Ubuntu):
sudo apt-get install build-essential cmake llvm-18-dev
cd /mnt/c/path/to/OLang
./build.sh
```

## Quick Start

Create `hello.tpr`:

```tulpar
str message = "Hello, World!";
print(message);

func square(int n) {
    return n * n;
}

print(square(5));  // Output: 25
```

Run:

```bash
./tulpar hello.tpr
```

## Command Line Usage

### Running Scripts (Default: Native AOT Speed)
```bash
tulpar script.tpr           # Fastest: AOT compile & run (native speed)
tulpar --vm script.tpr      # VM mode: instant start, good for development
```

By default, `tulpar` transparently compiles your code to a native binary via LLVM and runs it. This gives you **C-like speed** with **Python-like simplicity** - no extra flags needed.

### Building Standalone Executables
Compile your Tulpar code into a standalone native binary for distribution:

```bash
tulpar build script.tpr         # Creates 'script' binary
tulpar build script.tpr myapp   # Custom output name
```

### Interactive REPL
```bash
tulpar --repl
```

## Language Features

### Data Types

```tulpar
int x = 42;
float pi = 3.14159;
str name = "Tulpar";
bool active = true;

array mixed = [1, "text", 3.14];
arrayInt numbers = [1, 2, 3, 4, 5];
json config = {"host": "localhost", "port": 8080};
```

### Control Flow

```tulpar
if (x > 0) {
    print("positive");
} else {
    print("non-positive");
}

for (int i = 0; i < 10; i++) {
    print(i);
}

while (condition) {
    // loop body
}
```

### Functions

```tulpar
func fibonacci(int n) {
    if (n <= 1) {
        return n;
    }
    return fibonacci(n - 1) + fibonacci(n - 2);
}
```

### JSON Objects

```tulpar
json user = {
    "name": "Alice",
    "age": 30,
    "address": {
        "city": "Istanbul",
        "country": "Turkey"
    }
};

print(user["address"]["city"]);  // Istanbul
print(user.address.city);        // Istanbul (dot notation)
```

### Custom Types

```tulpar
type Person {
    str name;
    int age;
    str city = "Istanbul";  // default value
}

Person p = Person("Ali", 25);
print(p.name, p.age, p.city);
```

### Error Handling

```tulpar
try {
    int result = riskyOperation();
} catch (e) {
    print("Error:", e);
}
```

### Multi-Threading

```tulpar
thread_create("worker_function", arg);

mutex lock = mutex_create();
mutex_lock(lock);
// critical section
mutex_unlock(lock);
```

### Networking

```tulpar
int server = socket_server("127.0.0.1", 8080);
int client = socket_accept(server);
str data = socket_receive(client, 1024);
socket_send(client, "HTTP/1.1 200 OK\r\n\r\nHello");
socket_close(client);
```

### Database (SQLite)

```tulpar
int db = db_open("app.db");
db_execute(db, "CREATE TABLE users (id INTEGER, name TEXT)");
json rows = db_query(db, "SELECT * FROM users");
db_close(db);
```

## Standard Library

### Tulpar Wings (Web Framework)

```tulpar
import "wings";

func home() {
    return {"message": "Welcome to Tulpar Wings!"};
}

func users() {
    return [{"id": 1, "name": "Alice"}, {"id": 2, "name": "Bob"}];
}

get("/", "home");
get("/users", "users");
listen(3000);
```

### Library Modules

| Module | File | Description |
|--------|------|-------------|
| Wings | `wings.tpr` | Lightweight web framework |
| Router | `router.tpr` | URL routing & path matching |
| HTTP Utils | `http_utils.tpr` | HTTP request/response helpers |
| Middleware | `middleware.tpr` | Request middleware pipeline |
| Async | `async.tpr` | Asynchronous task support |
| Tulpar API | `tulpar_api.tpr` | High-level API framework |
| Socket | `socket.tpr` | Socket abstraction layer |
| SQLite | `sqlite3/` | Embedded SQLite3 database |

### Built-in Functions

| Category | Functions |
|----------|-----------|
| I/O | `print`, `input`, `inputInt`, `inputFloat` |
| Type Conversion | `toInt`, `toFloat`, `toString`, `toBool` |
| Math | `abs`, `sqrt`, `pow`, `sin`, `cos`, `tan`, `log`, `exp`, `floor`, `ceil`, `round`, `random`, `randint`, `min`, `max` |
| String | `length`, `upper`, `lower`, `trim`, `split`, `join`, `replace`, `substring`, `contains`, `startsWith`, `endsWith`, `indexOf` |
| Array | `push`, `pop`, `length`, `range` |
| JSON | `toJson`, `fromJson` |
| Time | `timestamp`, `time_ms`, `clock_ms`, `sleep` |
| File | `file_read`, `file_write`, `file_exists`, `file_delete` |

## Architecture

```
TulparLang/
├── src/
│   ├── lexer/          # Tokenization (lexer.c, lexer.h)
│   ├── parser/         # AST generation (parser.c, parser.h)
│   ├── typeinfer/      # Type inference engine
│   ├── aot/            # LLVM AOT backend (~175K line codegen)
│   ├── jit/            # x64 JIT compiler & optimizer
│   ├── vm/             # Bytecode VM (compiler, runtime bindings)
│   └── interpreter/    # Tree-walk interpreter (legacy)
├── lib/                # Standard libraries (Wings, Router, etc.)
├── runtime/            # Runtime support (cJSON, ARC, native FFI)
├── examples/           # 25 example programs
├── benchmarks/         # Multi-language benchmark suite
├── wasm/               # WebAssembly target support
├── cmake/              # CMake modules
└── docs/               # Documentation
```

### Execution Backends

| Backend | Status | Description |
|---------|--------|-------------|
| **AOT (LLVM)** | Primary | Compiles to native binaries via LLVM IR. Best performance. |
| **VM** | Active | Bytecode VM for fast startup and development. |
| **JIT** | Active | x64 JIT with optimization passes. |
| **Interpreter** | Legacy | Tree-walk interpreter, kept for compatibility. |

## Examples

Example programs are available in the `examples/` directory:

| File | Description |
|------|-------------|
| `01_hello_world.tpr` | Basic syntax |
| `02_basics.tpr` | Variables, loops, functions |
| `03_interactive.tpr` | User input/output |
| `04_math_logic.tpr` | Mathematical operations |
| `05_strings.tpr` | String manipulation |
| `06_data_structures.tpr` | JSON and arrays |
| `07_modules.tpr` | Import system |
| `08_file_io.tpr` | File operations |
| `09_socket_server.tpr` | Network server |
| `09_socket_client.tpr` | Network client |
| `09_socket_simple.tpr` | Simple socket communication |
| `10_try_catch.tpr` | Error handling |
| `11_router_app.tpr` | Web application |
| `12_threaded_server.tpr` | Multi-threaded HTTP |
| `13_database.tpr` | SQLite integration |
| `14_api_server.tpr` | API server |
| `15_feature_test.tpr` | Feature showcase |
| `api_wings.tpr` | REST API with Wings |
| `api_wings_crud.tpr` | CRUD API with Wings |
| `api_router_crud.tpr` | Router-based CRUD API |
| `api_simple.tpr` | Simple API example |
| `tulpar_api_demo.tpr` | Tulpar API demo |

## Documentation

- [Quick Start Guide](docs/QUICKSTART.md)
- [Language Reference](docs/KULLANIM.md)
- [Math Functions](docs/MATH_FUNCTIONS.md)
- [Platform Support](docs/PLATFORM_SUPPORT.md)
- [Build Comparison](BUILD_COMPARISON.md)

## Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/name`)
3. Commit changes (`git commit -m 'Add feature'`)
4. Push to branch (`git push origin feature/name`)
5. Open a Pull Request

## License

MIT License - see [LICENSE](LICENSE) for details.

## Author

**Hamza Ortatepe**  
GitHub: [@hamer1818](https://github.com/hamer1818)

---

<div align="center">

[Documentation](docs/) · [Examples](examples/) · [Issues](https://github.com/hamer1818/OLang/issues)

</div>
