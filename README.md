# TulparLang

<div align="center">

[![Version](https://img.shields.io/badge/version-2.1.0-blue.svg)](https://github.com/hamer1818/TulparLang/releases)
[![Build](https://github.com/hamer1818/TulparLang/actions/workflows/build.yml/badge.svg)](https://github.com/hamer1818/TulparLang/actions/workflows/build.yml)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Linux%20|%20macOS%20|%20Windows-lightgrey.svg)]()

A statically-typed programming language with LLVM backend, UTF-8 support, and native JSON syntax.

</div>

---

## Overview

TulparLang is a modern programming language built in C with an LLVM-18 backend for native code generation. It combines the simplicity of dynamic languages with the performance of compiled languages.

**Key characteristics:**
- Static typing with type inference
- First-class JSON support
- UTF-8 native strings
- LLVM AOT compilation
- Cross-platform (Linux, macOS, Windows)

## Performance

Benchmark results comparing total execution time across 11 algorithmic tests:

| Language | Total Time | Relative to C |
|----------|------------|---------------|
| C (gcc -O3) | 2.28 ms | 1.0x |
| **Tulpar (AOT)** | 8.77 ms | 3.9x |
| JavaScript (Node) | 22.69 ms | 10x |
| PHP 8.3 | 173.67 ms | 76x |
| Python 3.11 | 350.74 ms | 154x |

Tulpar achieves near-C performance while maintaining high-level language ergonomics.

## Installation

### Prerequisites

- GCC or Clang
- LLVM 18+
- CMake 3.14+

### Build from Source

```bash
git clone https://github.com/hamer1818/TulparLang.git
cd TulparLang
mkdir build && cd build
cmake ..
make
```

### Quick Install (Linux/macOS)

```bash
./build.sh
```

### Windows

```cmd
build.bat
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

## Project Structure

```
TulparLang/
├── src/
│   ├── lexer/          # Tokenization
│   ├── parser/         # AST generation
│   ├── interpreter/    # Runtime execution
│   ├── vm/             # Virtual machine
│   └── aot/            # LLVM backend
├── lib/                # Standard libraries
├── examples/           # Example programs
├── cmake/              # CMake modules
└── runtime/            # Runtime support
```

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
| `10_try_catch.tpr` | Error handling |
| `11_router_app.tpr` | Web application |
| `12_threaded_server.tpr` | Multi-threaded HTTP |
| `13_database.tpr` | SQLite integration |
| `api_wings.tpr` | REST API with Wings |

## Documentation

- [Quick Start Guide](docs/QUICKSTART.md)
- [Language Reference](docs/KULLANIM.md)
- [Math Functions](docs/MATH_FUNCTIONS.md)
- [Platform Support](docs/PLATFORM_SUPPORT.md)

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

[Documentation](docs/) · [Examples](examples/) · [Issues](https://github.com/hamer1818/TulparLang/issues)

</div>
