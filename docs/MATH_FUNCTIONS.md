# Math Functions

Tulpar provides a rich set of built-in math functions for numerical computations. This document explains what these functions do, which types they work with, and how to use them.

Core numeric types:

- `int` – integer
- `float` – floating‑point number
- `bigint` – very large integers

---

## General Notes

- Trigonometric functions (`sin`, `cos`, `tan`) take their arguments in **radians**.
- Most functions operate on `float`; when you pass `int`, it may be promoted to `float` as needed.
- Invalid inputs (for example, the square root of a negative number) may cause runtime errors; these can be caught using `try / catch`.

```tulpar
try {
    float v = sqrt(-1.0);
} catch (e) {
    print("Error:", e);
}
```

---

## Function Catalog

### 1. Basic Functions

| Function | Description |
|----------|-------------|
| `abs(x)` | Returns the absolute value of `x`. |
| `sqrt(x)` | Returns the square root of `x`. |
| `pow(x, y)` | Returns `x` raised to the power `y`. |

**Signatures (example types):**

- `float abs(float x)`
- `int abs(int x)`
- `float sqrt(float x)`
- `float pow(float x, float y)`

**Examples:**

```tulpar
print(abs(-5));          // 5
print(abs(-3.2));       // 3.2

print(sqrt(16));        // 4
print(pow(2, 3));       // 8
```

---

### 2. Trigonometric Functions

| Function | Description |
|----------|-------------|
| `sin(x)` | Returns the sine of `x` (in radians). |
| `cos(x)` | Returns the cosine of `x` (in radians). |
| `tan(x)` | Returns the tangent of `x` (in radians). |

**Signatures:**

- `float sin(float x)`
+- `float cos(float x)`
- `float tan(float x)`

**Examples:**

```tulpar
float pi = 3.14159265;

print(sin(pi / 2));   // approximately 1
print(cos(0));        // 1
print(tan(pi / 4));   // approximately 1
```

---

### 3. Logarithmic and Exponential Functions

| Function | Description |
|----------|-------------|
| `log(x)` | Returns the natural logarithm (ln) of `x`. |
| `exp(x)` | Returns `e` raised to the power `x`. |

**Signatures:**

- `float log(float x)`
- `float exp(float x)`

**Examples:**

```tulpar
print(log(1));     // 0
print(exp(0));     // 1

float v = log(exp(2.0)); // approximately 2.0
print(v);
```

---

### 4. Rounding Functions

Rounding functions listed in the README:

| Function | Description |
|----------|-------------|
| `floor(x)` | Rounds `x` down to the nearest integer (largest integer <= x). |
| `ceil(x)`  | Rounds `x` up to the nearest integer (smallest integer >= x). |
| `round(x)` | Rounds `x` to the nearest integer. |

**Signatures:**

- `float floor(float x)` (may be implemented to return an integer internally)
- `float ceil(float x)`
- `float round(float x)`

**Examples:**

```tulpar
print(floor(3.7));   // 3
print(ceil(3.1));    // 4
print(round(3.5));   // 4 (nearest integer)
print(round(3.4));   // 3
```

---

### 5. Random Functions

Tulpar provides functions to generate random numbers (aligned with the README table):

| Function | Description |
|----------|-------------|
| `random()` | Returns a random `float` in the range [0, 1). |
| `randint(a, b)` | Returns a random integer in the inclusive range `[a, b]`. |

**Examples:**

```tulpar
float r = random();
print("0-1 random:", r);

int dice = randint(1, 6);  // dice simulation
print("Dice:", dice);
```

Note: Whether random numbers are deterministic, seed support, and other advanced details depend on the runtime implementation; the generator is designed to be sufficient for typical applications.

---

### 6. Minimum / Maximum Functions

| Function | Description |
|----------|-------------|
| `min(a, b)` | Returns the smaller of `a` and `b`. |
| `max(a, b)` | Returns the larger of `a` and `b`. |

**Signatures:**

- `int min(int a, int b)`
- `float min(float a, float b)`
- `int max(int a, int b)`
- `float max(float a, float b)`

**Examples:**

```tulpar
print(min(3, 7));        // 3
print(max(3, 7));        // 7

print(min(3.5, 2.1));    // 2.1
print(max(-1.0, 0.0));   // 0.0
```

---

## Combined Usage Examples

```tulpar
float x = -3.7;

print("abs:", abs(x));          // 3.7
print("floor:", floor(x));      // -4
print("ceil:", ceil(x));        // -3
print("round:", round(x));      // -4

float angle = 3.14159265 / 4;   // 45 degrees
print("sin:", sin(angle));
print("cos:", cos(angle));

float v = sqrt(2.0);
print("sqrt(2):", v);
```

To see how these math functions can be used in more complex algorithms, check `examples/04_math_logic.tpr`.