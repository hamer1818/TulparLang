# Tulpar Language Reference

This document provides a detailed description of the Tulpar programming language syntax and core concepts. For a quick start, see `docs/QUICKSTART.md`. For installation and platform details, see `docs/PLATFORM_SUPPORT.md`.

---

## 1. Basic Concepts

- Tulpar source files use the `*.tpr` extension.
- A file can contain top‑level (global) variables, functions and statements.
- Statements end with a semicolon (`;`).
- Line comments start with `//` and continue to the end of the line.

```tulpar
// This is a comment line
int x = 10;  // end-of-line comment
```

---

## 2. Data Types

Tulpar is statically typed but uses type inference in many cases. Common types are:

- `int`  – integer numbers
- `float` – floating‑point numbers
- `bool` – `true` or `false`
- `str` – UTF‑8 strings
- `bigint` – very large integers
- `array` – heterogeneous arrays
- Typed arrays: `arrayInt`, `arrayFloat`, `arrayJson`, etc.
- `json` – JSON objects

### 2.1. Primitive Types

```tulpar
int age = 25;
float pi = 3.14159;
bool active = true;
str name = "Tulpar";

bigint huge = 123456789012345678901234567890;
```

`bigint` is used to work with very large numbers and avoid overflow:

```tulpar
bigint a = 123456789012345678901234567890;
bigint b = 2;
bigint c = a * b;
print(c);
```

### 2.2. Arrays and Typed Arrays

```tulpar
array mixed = [1, "text", 3.14];
arrayInt numbers = [1, 2, 3, 4, 5];

print(numbers[0]);              // 1
print("Length:", length(numbers));  // 5
```

### 2.3. JSON Type

JSON is a first‑class citizen in Tulpar:

```tulpar
json user = {
    "name": "Alice",
    "age": 30,
    "address": {
        "city": "Istanbul",
        "country": "Turkey"
    }
};

print(user["name"]);
print(user["address"]["city"]);
```

Adding / updating fields:

```tulpar
user["age"] = 31;
user["skills"] = ["C", "Tulpar"];
```

Creating an empty JSON and using it like a dictionary:

```tulpar
json config = {};
config["host"] = "localhost";
config["port"] = 8080;
config["debug"] = true;
```

Converting JSON to a string:

```tulpar
print(toJson(config));
```

### 2.4. Custom Types (`type`)

You can define struct‑like custom types:

```tulpar
type Person {
    str name;
    int age;
    str city = "Istanbul";  // default value
}

Person p = Person("Ali", 25);
print(p.name, p.age, p.city);
```

You access fields via dot notation:

```tulpar
p.city = "Ankara";
print(p.city);
```

---

## 3. Variables and Assignment

Variables are declared with a type, name and initial value:

```tulpar
int x = 10;
float y = 2.5;
str s = "Hello";
```

The assignment operator `=` changes the value of an existing variable:

```tulpar
x = x + 5;
```

---

## 4. Operators

### 4.1. Arithmetic Operators

- `+`, `-`, `*`, `/`, `%`

```tulpar
int a = 10;
int b = 3;
print(a + b);   // 13
print(a - b);   // 7
print(a * b);   // 30
print(a / b);   // integer division
print(a % b);   // remainder
```

You also use `+` for string concatenation:

```tulpar
str full = "Hello " + "Tulpar";
print(full);
```

### 4.2. Comparison Operators

- `==`, `!=`, `<`, `>`, `<=`, `>=`

```tulpar
int x = 5;
print(x == 5);  // true
print(x != 3);  // true
print(x > 2);   // true
```

### 4.3. Logical Operators

- `&&` (and), `||` (or), `!` (not)

```tulpar
bool t = true;
bool f = false;

print(t && f);  // false
print(t || f);  // true
print(!t);      // false
```

---

## 5. Control Flow

### 5.1. `if / else if / else`

```tulpar
int score = 85;

if (score >= 90) {
    print("A");
} else if (score >= 80) {
    print("B");
} else {
    print("C or lower");
}
```

### 5.2. `for` Loop

```tulpar
for (int i = 0; i < 5; i++) {
    print("i =", i);
}
```

Iterating over arrays:

```tulpar
arrayInt nums = [1, 2, 3];

for (int i = 0; i < length(nums); i++) {
    print(nums[i]);
}
```

### 5.3. `while` Loop

```tulpar
int i = 0;
while (i < 3) {
    print("i =", i);
    i = i + 1;
}
```

### 5.4. `break` and `continue`

```tulpar
for (int i = 0; i < 10; i++) {
    if (i == 3) {
        continue;  // skip 3
    }
    if (i == 7) {
        break;     // stop at 7
    }
    print(i);
}
```

---

## 6. Functions

Functions are defined with the `func` keyword:

```tulpar
func add(int a, int b) {
    return a + b;
}

int result = add(2, 3);
print(result);
```

### 6.1. Return Value

The `return` statement ends the function and returns a value:

```tulpar
func absInt(int x) {
    if (x < 0) {
        return -x;
    }
    return x;
}
```

### 6.2. Recursion

```tulpar
func fibonacci(int n) {
    if (n <= 1) {
        return n;
    }
    return fibonacci(n - 1) + fibonacci(n - 2);
}

print(fibonacci(10));
```

### 6.3. Function Overloading

You can define multiple functions with the same name but different parameter types. Example `len` helper:

```tulpar
func len(array a) { return length(a); }
func len(json a) { return length(a); }
func len(str s) { return length(s); }
```

---

## 7. Error Handling: `try / catch / finally`

Tulpar uses `try`, `catch`, and `finally` blocks for exception handling.

```tulpar
try {
    print("In try block");
    throw "An error occurred!";
} catch (e) {
    print("Caught error:", e);
} finally {
    print("Finally always runs");
}
```

You can also throw JSON objects:

```tulpar
try {
    json err = {
        "message": "Something went wrong",
        "code": 500
    };
    throw err;
} catch (e) {
    print("Error code:", e["code"]);
    print("Message:", e["message"]);
}
```

---

## 8. Data Structures and JSON Examples

### 8.1. JSON Objects

For a detailed example, see `examples/06_data_structures.tpr`:

```tulpar
json person = {
    "name": "Hamza",
    "age": 25,
    "skills": ["C", "Python", "Tulpar"],
    "address": {
        "city": "Istanbul",
        "zip": "34000"
    }
};

print("Person:", toJson(person));
print("Name:", person["name"]);
print("City:", person["address"]["city"]);
```

### 8.2. JSON Arrays

```tulpar
arrayJson users = [
    {"id": 1, "name": "Alice"},
    {"id": 2, "name": "Bob"}
];

for (int i = 0; i < length(users); i++) {
    print("User", users[i]["id"], ":", users[i]["name"]);
}
```

### 8.3. Conversions

```tulpar
json cfg = {"debug": true, "port": 8080};
str s = toJson(cfg);
print(s);

json parsed = fromJson(s);
print(parsed["port"]);
```

---

## 9. Modules and the Import System

Tulpar can import other `.tpr` files or library files using `import`:

```tulpar
import "wings";
import "lib/router.tpr";
```

These modules provide features like HTTP routing, middleware, JSON response helpers, etc. For example, a simple API based on `lib/tulpar_api.tpr`:

```tulpar
import "tulpar_api";

func home(json req) {
    return api_json_response({"message": "Welcome to TulparAPI"});
}

api_init("My API", "1.0.0");
api_get("/", "home");
api_run(3000);
```

The module search path and the role of the `lib/` directory are defined by the Tulpar runtime; standard library modules live in this directory.

---

## 10. Standard Library (Summary)

For a full function list, see the \"Built-in Functions\" table in the README and `docs/MATH_FUNCTIONS.md`. This section summarizes the main categories.

- **I/O**
  - `print`, `input`, `inputInt`, `inputFloat`
- **Type Conversion**
  - `toInt`, `toFloat`, `toString`, `toBool`
- **String**
  - `length`, `upper`, `lower`, `trim`, `split`, `join`, `replace`, `substring`, `contains`, `startsWith`, `endsWith`, `indexOf`
- **Array**
  - `push`, `pop`, `length`, `range`
- **JSON**
  - `toJson`, `fromJson`
- **Time**
  - `timestamp`, `time_ms`, `clock_ms`, `sleep`
- **File**
  - `file_read`, `file_write`, `file_exists`, `file_delete`

For math functions in detail, see `docs/MATH_FUNCTIONS.md`.

---

## 11. Multithreading

Tulpar provides basic primitives for multithreaded programming:

- `thread_create` – creates a new thread.
- `mutex_create`, `mutex_lock`, `mutex_unlock` – used to protect shared resources.

A simple (schematic) example:

```tulpar
mutex lock = mutex_create();
int counter = 0;

func worker() {
    mutex_lock(lock);
    counter = counter + 1;
    mutex_unlock(lock);
}

thread_create("worker", null);
thread_create("worker", null);
```

---

## 12. Networking and HTTP

### 12.1. Low‑Level Sockets

```tulpar
int server = socket_server("127.0.0.1", 8080);
int client = socket_accept(server);
str data = socket_receive(client, 1024);
socket_send(client, "HTTP/1.1 200 OK\r\n\r\nHello");
socket_close(client);
```

### 12.2. Web Applications with Wings

With `lib/wings.tpr` you can use a simple HTTP router:

```tulpar
import "wings";

func home() {
    return {"message": "Welcome to Tulpar Wings!"};
}

get("/", "home");
listen(3000);
```

See `examples/11_router_app.tpr` and `examples/api_wings.tpr` for more advanced examples.

---

## 13. Database (SQLite) Integration

Tulpar supports simple database operations via embedded SQLite:

```tulpar
int db = db_open("app.db");
db_execute(db, "CREATE TABLE users (id INTEGER, name TEXT)");
db_execute(db, "INSERT INTO users (id, name) VALUES (1, 'Alice')");

json rows = db_query(db, "SELECT * FROM users");
print(toJson(rows));

db_close(db);
```

For a more complete example, see `examples/13_database.tpr`.

---

## 14. Execution Modes and Command-Line Options

Tulpar’s command-line interface can be summarized as follows:

- **Run with VM (default)**  
  ```bash
  ./tulpar program.tpr
  ```

- **AOT compilation (to produce a native binary)**  
  ```bash
  ./tulpar --aot program.tpr program_bin
  ```

- **Force VM execution**  
  ```bash
  ./tulpar --vm program.tpr
  ./tulpar --run program.tpr   # alias for --vm
  ```

- **Legacy interpreter**  
  ```bash
  ./tulpar --legacy program.tpr
  ```

- **REPL (interactive mode)**  
  ```bash
  ./tulpar --repl
  ```

These options correspond to the execution modes defined in `src/main.c`.

---

This reference covers the core building blocks of the Tulpar language. For deeper, practical examples, inspect the `.tpr` files in the `examples/` directory, especially:

- `04_math_logic.tpr`
- `06_data_structures.tpr`
- `10_try_catch.tpr`
- `11_router_app.tpr`
- `12_threaded_server.tpr`
- `13_database.tpr`