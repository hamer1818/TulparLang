# Windows Support Testing Guide

This document provides guidance for testing the Windows native support implementation.

## Prerequisites for Testing

Before running tests, ensure you have:
- Windows 10 or Windows 11
- Visual Studio 2019 or 2022 with C++ Desktop Development workload
- CMake 3.14+
- LLVM 18+ for Windows (from https://github.com/llvm/llvm-project/releases)
- Git for Windows

## Test 1: Windows Build

### Objective
Verify that TulparLang builds successfully on Windows with MSVC.

### Steps

1. **Clone the repository:**
   ```batch
   git clone https://github.com/hamer1818/OLang.git
   cd OLang
   ```

2. **Build using batch script:**
   ```batch
   build.bat
   ```

3. **Verify build output:**
   - Check that `tulpar.exe` exists in the root directory
   - Check for `build-windows\Release\tulpar.exe`
   - Check for `build-windows\Release\tulpar_runtime.lib`

4. **Check for compilation warnings:**
   - Review build output for critical warnings
   - Expected: Some minor warnings are acceptable
   - Not acceptable: Errors or unresolved symbols

5. **Test executable:**
   ```batch
   tulpar.exe --version
   ```

### Expected Results
- Build completes without errors
- `tulpar.exe` runs and displays version information
- Runtime library (`tulpar_runtime.lib`) is created

### Alternative: PowerShell Build
```powershell
.\build.ps1
```

### Alternative: CMake Direct
```batch
mkdir build-windows
cd build-windows
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

## Test 2: Run Example Programs

### Objective
Verify that TulparLang can execute example programs correctly on Windows.

### Test Cases

#### Test 2.1: Hello World
```batch
tulpar.exe examples\01_hello_world.tpr
```
**Expected Output:**
```
Hello, World!
```

#### Test 2.2: Basic Operations
```batch
tulpar.exe examples\02_basics.tpr
```
**Expected Output:** Program runs without errors, shows mathematical operations

#### Test 2.3: String Operations
```batch
tulpar.exe examples\05_strings.tpr
```
**Expected Output:** Various string manipulation results

#### Test 2.4: File I/O
```batch
echo test content > test_input.txt
tulpar.exe examples\08_file_io.tpr
```
**Expected Output:** File operations complete successfully

#### Test 2.5: JSON and Arrays
```batch
tulpar.exe examples\06_data_structures.tpr
```
**Expected Output:** JSON and array operations work correctly

### Creating a Test Script

Create `test_examples.bat`:
```batch
@echo off
echo Testing TulparLang Examples on Windows
echo.

set FAILED=0

echo Test 1: Hello World
tulpar.exe examples\01_hello_world.tpr
if %errorlevel% neq 0 set FAILED=1

echo.
echo Test 2: Basics
tulpar.exe examples\02_basics.tpr
if %errorlevel% neq 0 set FAILED=1

echo.
echo Test 3: Math and Logic
tulpar.exe examples\04_math_logic.tpr
if %errorlevel% neq 0 set FAILED=1

echo.
echo Test 4: Strings
tulpar.exe examples\05_strings.tpr
if %errorlevel% neq 0 set FAILED=1

echo.
echo Test 5: Data Structures
tulpar.exe examples\06_data_structures.tpr
if %errorlevel% neq 0 set FAILED=1

echo.
if %FAILED% equ 0 (
    echo All tests PASSED!
) else (
    echo Some tests FAILED!
)
```

## Test 3: Socket Operations

### Objective
Verify that networking (Winsock2) works correctly on Windows.

### Prerequisites
- Windows Firewall may prompt for network access - allow it

### Test 3.1: Simple Socket Server

**Terminal 1 (Server):**
```batch
tulpar.exe examples\09_socket_simple.tpr
```

**Expected:** Server starts and listens on port 8080

### Test 3.2: Socket Client-Server

**Terminal 1 (Server):**
```batch
tulpar.exe examples\09_socket_server.tpr
```

**Terminal 2 (Client):**
```batch
tulpar.exe examples\09_socket_client.tpr
```

**Expected:**
- Server accepts connection
- Client sends/receives data
- No Winsock errors

### Common Socket Issues on Windows

**Issue:** `bind error: 10048`
- **Cause:** Port already in use
- **Solution:** Change port or wait for TIME_WAIT to expire

**Issue:** `WSAStartup failed`
- **Cause:** Winsock2 not initialized
- **Solution:** Already handled in `vm_create()`, check implementation

**Issue:** Firewall blocks connection
- **Solution:** Allow tulpar.exe through Windows Firewall

## Test 4: CI/CD Verification

### Objective
Verify that GitHub Actions successfully builds Windows binaries.

### Steps

1. **Push changes to GitHub:**
   ```batch
   git add .
   git commit -m "Add Windows native support"
   git push origin main
   ```

2. **Check GitHub Actions:**
   - Go to repository's "Actions" tab
   - Wait for workflow to complete
   - Verify `build-windows` job succeeds

3. **Download and test artifact:**
   - Download `tulpar-windows-x64.exe` from Actions artifacts
   - Test on a clean Windows machine (no dev tools)
   - Verify it runs: `tulpar-windows-x64.exe --version`

4. **Check release creation:**
   - If pushed to `main` branch, verify release is created
   - Confirm `tulpar-windows-x64.exe` is attached to release

### Expected CI Results
- ✅ build-linux job passes
- ✅ build-macos job passes  
- ✅ build-windows job passes
- ✅ create-release job creates release with all three executables

## Platform-Specific Testing Notes

### UTF-8 Console Testing

Test UTF-8 string handling:
```batch
REM Set console to UTF-8
chcp 65001

REM Create test file with UTF-8 content
echo str message = "Merhaba Dünya! 你好世界!"; > utf8_test.tpr
echo print(message); >> utf8_test.tpr

REM Run test
tulpar.exe utf8_test.tpr
```

**Expected:** UTF-8 characters display correctly

### Path Handling Testing

Test Windows path separators:
```batch
REM Create nested directories
mkdir test\subdir

REM Test with backslashes
tulpar.exe test\script.tpr

REM Test with forward slashes (should also work)
tulpar.exe test/script.tpr
```

### DLL Dependencies Testing

Test on a machine without Visual Studio:
```batch
REM Should work with VC++ Redistributable installed
REM If missing DLLs, download from:
REM https://aka.ms/vs/17/release/vc_redist.x64.exe
```

## Performance Testing

### Build Time Comparison
Measure build time on Windows vs Linux:
```batch
REM Clean build
build.bat clean
REM Time the build
powershell -Command "Measure-Command { .\build.bat }"
```

### Runtime Performance
Compare AOT execution speed:
```batch
REM Fibonacci benchmark
tulpar.exe benchmarks\fibonacci.tpr
```

## Troubleshooting Common Issues

### Issue: Build fails with "LLVM not found"
**Solution:**
1. Verify LLVM installation: `llvm-config --version`
2. Add LLVM to PATH
3. Or specify manually: `cmake .. -DLLVM_DIR="C:/Program Files/LLVM/lib/cmake/llvm"`

### Issue: Build fails with "Cannot open include file: 'windows.h'"
**Solution:**
- Install/repair Visual Studio with C++ Desktop Development workload
- Ensure Windows SDK is installed

### Issue: Executable crashes immediately
**Solution:**
1. Check LLVM DLLs are accessible
2. Run in debugger: `devenv tulpar.exe`
3. Check Event Viewer for crash details

### Issue: Socket examples don't work
**Solution:**
1. Check Windows Firewall settings
2. Verify Winsock2 initialization in `vm_create()`
3. Run as Administrator if needed

## Reporting Test Results

When reporting test results, include:
- Windows version (e.g., Windows 11 22H2)
- Visual Studio version (e.g., VS 2022 17.8)
- LLVM version (e.g., 18.1.8)
- CMake version
- Build output (success/failure)
- Test results for each example
- Any warnings or errors encountered

## Success Criteria

All tests pass if:
- ✅ Build completes without errors
- ✅ `tulpar.exe` runs and shows version
- ✅ At least 80% of examples run successfully
- ✅ Socket examples work (server accepts connections)
- ✅ GitHub Actions Windows build succeeds
- ✅ No critical warnings during compilation

## Next Steps After Testing

If tests pass:
1. Update `test_results.txt` with findings
2. Mark testing todos as complete
3. Document any platform-specific quirks found
4. Consider creating Windows installer package

If tests fail:
1. Document the failure in detail
2. Check if it's a known limitation
3. Fix the issue and re-test
4. Update platform documentation if needed
