# Language Reference

This document provides a comprehensive reference for the Tulpar programming language.

## Data Types

Tulpar supports the following data types:
- `int`: Integer numbers
- `float`: Floating-point numbers
- `str`: Strings
- `bool`: Boolean values
- `array`: Arrays
- `json`: JSON objects

## Control Flow

Tulpar provides standard control flow constructs:

```tulpar
if (condition) {
    // code
} else {
    // code
}

for (int i = 0; i < 10; i++) {
    print(i);
}

while (condition) {
    // code
}
```

## Functions

Define reusable code blocks using functions:

```tulpar
func add(int a, int b) {
    return a + b;
}

print(add(2, 3));  // Output: 5
```