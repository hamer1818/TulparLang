"""
============================================
TULPAR vs PYTHON vs C vs JavaScript vs PHP
       PERFORMANS KARŞILAŞTIRMASI
Her dil kendi süre ölçümünü yapar
============================================
"""
import subprocess
import time
import os
import re
import shutil
import platform

# ============================================
# Python Benchmark Fonksiyonları
# ============================================
import sys

# Increase recursion depth for deep recursion tests
sys.setrecursionlimit(5000)

def fibonacci(n):
    if n <= 1:
        return n
    return fibonacci(n - 1) + fibonacci(n - 2)

def factorial(n):
    if n <= 1:
        return 1
    return n * factorial(n - 1)

def ackermann(m, n):
    if m == 0:
        return n + 1
    if n == 0:
        return ackermann(m - 1, 1)
    return ackermann(m - 1, ackermann(m, n - 1))

def tak(x, y, z):
    if y < x:
        return tak(tak(x - 1, y, z), tak(y - 1, z, x), tak(z - 1, x, y))
    return z

def loop_test(iterations):
    sum_val = 0
    for i in range(iterations):
        sum_val += i
    return sum_val

def sieve(n):
    flags = [0] * (n + 1)
    count = 0
    for i in range(2, n + 1):
        if flags[i] == 0:
            count += 1
            for k in range(i * i, n + 1, i):
                flags[k] = 1
    return count

def bubble_sort(size):
    arr = []
    seed = 12345
    for i in range(size):
        seed = (seed * 1103515245 + 12345) % 32768
        arr.append(seed)
    
    for i in range(size):
        for j in range(size - 1):
            if arr[j] > arr[j + 1]:
                arr[j], arr[j + 1] = arr[j + 1], arr[j]
    
    # Compute checksum of ALL elements
    checksum = sum(arr)
    return checksum

def string_concat(iterations):
    result = ""
    for i in range(iterations):
        result = result + "a"
    return len(result)

def array_memory(size):
    arr = []
    for i in range(size):
        arr.append(i)
    sum_val = 0
    for i in range(size):
        sum_val = sum_val + arr[i]
    return sum_val

def string_allocation(iterations):
    result = ""
    for i in range(iterations):
        result = result + "Merhaba Dunya "
    return len(result)

def json_build_test(size):
    data = []
    for i in range(size):
        data.append({
            'id': i,
            'name': 'Item',
            'value': i * 10,
            'active': True
        })
    sum_val = 0
    for i in range(size):
        sum_val += data[i]['value']
    return sum_val

def run_python_benchmarks():
    """Python benchmark testlerini çalıştır"""
    results = {}
    
    start = time.perf_counter()
    fib_result = fibonacci(35)
    end = time.perf_counter()
    results['fibonacci'] = {'result': fib_result, 'time': (end - start) * 1000}
    
    start = time.perf_counter()
    fact_result = factorial(20)
    end = time.perf_counter()
    results['factorial'] = {'result': fact_result, 'time': (end - start) * 1000}
    
    start = time.perf_counter()
    ack_result = ackermann(3, 8)
    end = time.perf_counter()
    results['ackermann'] = {'result': ack_result, 'time': (end - start) * 1000}

    start = time.perf_counter()
    tak_result = tak(18, 12, 6)
    end = time.perf_counter()
    results['tak'] = {'result': tak_result, 'time': (end - start) * 1000}

    start = time.perf_counter()
    loop_result = loop_test(1000000)  # 1M iterations to match other languages
    end = time.perf_counter()
    results['loop'] = {'result': loop_result, 'time': (end - start) * 1000}
    
    start = time.perf_counter()
    sieve_result = sieve(50000)
    end = time.perf_counter()
    results['sieve'] = {'result': sieve_result, 'time': (end - start) * 1000}

    start = time.perf_counter()
    bubble_result = bubble_sort(5000)
    end = time.perf_counter()
    results['bubble'] = {'result': bubble_result, 'time': (end - start) * 1000}
    
    start = time.perf_counter()
    str_result = string_concat(20000)
    end = time.perf_counter()
    results['stringconcat'] = {'result': str_result, 'time': (end - start) * 1000}
    
    start = time.perf_counter()
    arr_result = array_memory(50000)
    end = time.perf_counter()
    results['arraymemory'] = {'result': arr_result, 'time': (end - start) * 1000}
    
    # string alloc removed from compiled langs mostly
    
    start = time.perf_counter()
    json_result = json_build_test(20000)
    end = time.perf_counter()
    results['jsonbuild'] = {'result': json_result, 'time': (end - start) * 1000}
    
    return results


def parse_benchmark_output(output):
    """Benchmark çıktısından süreleri parse et"""
    results = {}
    
    # Test sonuçlarını parse et - multiline çıktıları da destekle
    # Tulpar çıktısı: "Sure:\n18.6421\n ms" şeklinde olabilir
    # Note: Windows encodes µ as "Âµ" or "µ", handle both
    # Also handle "ns" (nanoseconds) from Go output
    test_patterns = [
        ('fibonacci', r'Test 1:.*?Sure:\s*([\d.eE+-]+)\s*(ms|[µÂ]+s|us|ns)'),
        ('factorial', r'Test 2:.*?Sure:\s*([\d.eE+-]+)\s*(ms|[µÂ]+s|us|ns)'),
        ('ackermann', r'Test 3:.*?Sure:\s*([\d.eE+-]+)\s*(ms|[µÂ]+s|us|ns)'),
        ('tak', r'Test 4:.*?Sure:\s*([\d.eE+-]+)\s*(ms|[µÂ]+s|us|ns)'),
        ('loop', r'Test 5:.*?Sure:\s*([\d.eE+-]+)\s*(ms|[µÂ]+s|us|ns)'),
        ('sieve', r'Test 6:.*?Sure:\s*([\d.eE+-]+)\s*(ms|[µÂ]+s|us|ns)'),
        ('bubble', r'Test 7:.*?Sure:\s*([\d.eE+-]+)\s*(ms|[µÂ]+s|us|ns)'),
        ('stringconcat', r'Test 8:.*?Sure:\s*([\d.eE+-]+)\s*(ms|[µÂ]+s|us|ns)'),
        ('arraymemory', r'Test 9:.*?Sure:\s*([\d.eE+-]+)\s*(ms|[µÂ]+s|us|ns)'),
        ('stringalloc', r'Test 10:.*?Sure:\s*([\d.eE+-]+)\s*(ms|[µÂ]+s|us|ns)'),
        ('jsonbuild', r'Test 11:.*?Sure:\s*([\d.eE+-]+)\s*(ms|[µÂ]+s|us|ns)'),
    ]
    
    for name, pattern in test_patterns:
        match = re.search(pattern, output, re.DOTALL | re.IGNORECASE)
        if match:
            val = float(match.group(1))
            unit = match.group(2).lower()
            # Handle all microsecond variants and nanoseconds
            if 'µ' in unit or 'â' in unit or unit == 'us':
                val /= 1000.0  # µs -> ms
            elif unit == 'ns':
                val /= 1000000.0  # ns -> ms
            results[name] = val
    
    # Toplam süreyi parse et
    total_match = re.search(r'TOPLAM SURE:\s*([\d.]+)\s*ms', output, re.IGNORECASE)
    if total_match:
        results['total'] = float(total_match.group(1))
    else:
        results['total'] = sum(results.values()) if results else 0
    
    return results


# ... (rest of the file) ...

    test_names = {
        'fibonacci': 'Fibonacci(30)',
        'factorial': 'Factorial(20)',
        'ackermann': 'Ackermann(3, 8)',
        'tak': 'Tak(18, 12, 6)',
        'loop': 'Loop 1M',
        'sieve': 'Sieve(10K)',
        'bubble': 'BubbleSort(1K)',
        'stringconcat': 'StringConcat(1K)',
        'arraymemory': 'ArrayMemory(10K)',
        'stringalloc': 'StringAlloc(1K)',
        'jsonbuild': 'JSONBuild(1K)'
    }

def run_tulpar_benchmark():
    """Tulpar benchmark testini çalıştır"""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.join(script_dir, '..')
    
    # Try multiple possible Tulpar executable locations
    # Prefer root binary first (test scripts in repo use this path successfully)
    if platform.system() == 'Windows':
        possible_paths = [
            os.path.join(project_dir, 'tulpar.exe'),
            os.path.join(project_dir, 'build-win', 'tulpar.exe'),
            os.path.join(project_dir, 'build', 'tulpar.exe'),
        ]
    else:
        possible_paths = [
            os.path.join(project_dir, 'tulpar'),
            os.path.join(project_dir, 'build-linux', 'tulpar'),
            os.path.join(project_dir, 'build', 'tulpar'),
        ]
    
    tulpar_exe = None
    for path in possible_paths:
        if os.path.exists(path):
            tulpar_exe = path
            break
        
    benchmark_file = os.path.join(script_dir, 'benchmark.tpr')
    output_bin = os.path.join(script_dir, 'benchmark_tulpar')
    if platform.system() == 'Windows':
        output_bin += '.exe'
    
    if not tulpar_exe:
        tried_paths = '\n    '.join(possible_paths)
        return {'error': f'Tulpar bulunamadı. Aranan yollar:\n    {tried_paths}'}
    
    try:
        # 1) Prefer direct execution of AOT-compatible benchmark script.
        #    This path proved more stable than "--aot <file> <outbin>" in current tree.
        aot_benchmark = os.path.join(script_dir, 'benchmark_aot.tpr')
        if os.path.exists(aot_benchmark):
            aot_run = subprocess.run(
                [tulpar_exe, aot_benchmark],
                capture_output=True,
                text=True,
                timeout=300,
                cwd=project_dir
            )
            aot_output = (aot_run.stdout or "") + "\n" + (aot_run.stderr or "")
            aot_parsed = parse_benchmark_output(aot_output)
            has_measurements = any(k != 'total' and v > 0 for k, v in aot_parsed.items())
            if has_measurements:
                return {'results': aot_parsed, 'output': aot_output}

        # 2) Fallback to legacy benchmark file (VM/full compatibility path)
        run_cmd = [tulpar_exe, benchmark_file]
        result = subprocess.run(run_cmd, capture_output=True, text=True, timeout=300, cwd=project_dir)
        output = (result.stdout or "") + "\n" + (result.stderr or "")
        return {'results': parse_benchmark_output(output), 'output': output}

    except subprocess.TimeoutExpired:
        return {'error': 'Timeout'}
    except Exception as e:
        return {'error': str(e)}


def run_c_benchmark():
    """C benchmark testini çalıştır"""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    c_source = os.path.join(script_dir, 'benchmark.c')
    c_exe = os.path.join(script_dir, 'benchmark_c.exe' if platform.system() == 'Windows' else 'benchmark_c')
    
    if not os.path.exists(c_source):
        return {'error': 'benchmark.c bulunamadı'}
    
    # GCC kontrolü
    gcc = shutil.which('gcc')
    if not gcc:
        return {'error': 'GCC bulunamadı. C derleyicisi yüklü değil.'}
    
    try:
        # Derle
        compile_result = subprocess.run(
            ['gcc', '-O2', c_source, '-o', c_exe],
            capture_output=True, text=True, timeout=60
        )
        if compile_result.returncode != 0:
            return {'error': f'Derleme hatası: {compile_result.stderr}'}
        
        # Çalıştır
        result = subprocess.run([c_exe], capture_output=True, text=True, timeout=300)
        
        # Temizle
        if os.path.exists(c_exe):
            os.remove(c_exe)
        
        return {'results': parse_benchmark_output(result.stdout), 'output': result.stdout}
    except subprocess.TimeoutExpired:
        return {'error': 'Timeout'}
    except Exception as e:
        return {'error': str(e)}


def run_javascript_benchmark():
    """JavaScript (Node.js) benchmark testini çalıştır"""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    js_file = os.path.join(script_dir, 'benchmark.js')
    
    if not os.path.exists(js_file):
        return {'error': 'benchmark.js bulunamadı'}
    
    # Node.js kontrolü
    node = shutil.which('node')
    if not node:
        return {'error': 'Node.js bulunamadı. Node.js yüklü değil.'}
    
    try:
        result = subprocess.run(['node', js_file], capture_output=True, text=True, timeout=300)
        return {'results': parse_benchmark_output(result.stdout), 'output': result.stdout}
    except subprocess.TimeoutExpired:
        return {'error': 'Timeout'}
    except Exception as e:
        return {'error': str(e)}


def run_php_benchmark():
    """PHP benchmark testini çalıştır"""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    php_file = os.path.join(script_dir, 'benchmark.php')
    
    if not os.path.exists(php_file):
        return {'error': 'benchmark.php bulunamadı'}
    
    # PHP kontrolü
    php = shutil.which('php')
    if not php:
        return {'error': 'PHP bulunamadı. PHP CLI yüklü değil.'}
    
    try:
        result = subprocess.run(['php', php_file], capture_output=True, text=True, timeout=300)
        return {'results': parse_benchmark_output(result.stdout), 'output': result.stdout}
    except subprocess.TimeoutExpired:
        return {'error': 'Timeout'}
    except Exception as e:
        return {'error': str(e)}


def run_go_benchmark():
    """Go benchmark testini çalıştır"""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    go_source = os.path.join(script_dir, 'benchmark.go')
    go_exe = os.path.join(script_dir, 'benchmark_go.exe' if platform.system() == 'Windows' else 'benchmark_go')
    
    if not os.path.exists(go_source):
        return {'error': 'benchmark.go bulunamadı'}
    
    # Go kontrolü
    go_cmd = shutil.which('go')
    if not go_cmd:
        return {'error': 'Go bulunamadı. Go compiler yüklü değil.'}
    
    try:
        # Derle
        # go build -o benchmark_go.exe benchmark.go
        compile_result = subprocess.run(
            ['go', 'build', '-o', go_exe, go_source],
            capture_output=True, text=True, timeout=60, cwd=script_dir
        )
        if compile_result.returncode != 0:
            return {'error': f'Derleme hatası: {compile_result.stderr}'}
        
        # Çalıştır
        result = subprocess.run([go_exe], capture_output=True, text=True, timeout=300, cwd=script_dir)
        
        # Temizle
        if os.path.exists(go_exe):
            os.remove(go_exe)
        
        return {'results': parse_benchmark_output(result.stdout), 'output': result.stdout}
    except subprocess.TimeoutExpired:
        return {'error': 'Timeout'}
    except Exception as e:
        return {'error': str(e)}


def run_rust_benchmark():
    """Rust benchmark testini çalıştır"""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    rust_source = os.path.join(script_dir, 'benchmark.rs')
    rust_exe = os.path.join(script_dir, 'benchmark_rs.exe' if platform.system() == 'Windows' else 'benchmark_rs')
    
    if not os.path.exists(rust_source):
        return {'error': 'benchmark.rs bulunamadı'}
    
    # Rustc kontrolü
    rustc = shutil.which('rustc')
    if not rustc:
        # Fallback to common Windows path
        common_path = os.path.expanduser('~/.cargo/bin/rustc.exe')
        if os.path.exists(common_path):
            rustc = common_path
        else:
            return {'error': 'Rust (rustc) bulunamadı.'}
    
    try:
        # Derle: rustc -O benchmark.rs -o benchmark_rs
        compile_result = subprocess.run(
            [rustc, '-O', rust_source, '-o', rust_exe],
            capture_output=True, text=True, timeout=60, cwd=script_dir
        )
        if compile_result.returncode != 0:
            return {'error': f'Derleme hatası: {compile_result.stderr}'}
        
        # Çalıştır
        result = subprocess.run([rust_exe], capture_output=True, text=True, timeout=300, cwd=script_dir)
        
        # Temizle
        if os.path.exists(rust_exe):
            os.remove(rust_exe)
        # remove .pdb on windows if exists
        pdb_file = rust_exe.replace('.exe', '.pdb') if platform.system() == 'Windows' else ''
        if pdb_file and os.path.exists(pdb_file):
             os.remove(pdb_file)
        
        return {'results': parse_benchmark_output(result.stdout), 'output': result.stdout}
    except subprocess.TimeoutExpired:
        return {'error': 'Timeout'}
    except Exception as e:
        return {'error': str(e)}


def format_time(ms):
    if ms is None:
        return "N/A"
    elif ms == 0:
        return "<0.01 µs"  # Too fast to measure
    elif ms < 1:
        return f"{ms * 1000:.2f} µs"
    elif ms < 1000:
        return f"{ms:.3f} ms"
    else:
        return f"{ms / 1000:.3f} s"


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    print("=" * 80)
    print("       🏁 MULTI-LANGUAGE PERFORMANS KARŞILAŞTIRMASI 🏁")
    print("            Tulpar vs Python vs C vs JavaScript vs PHP")
    print("=" * 80)
    print(f"Tarih: {time.strftime('%Y-%m-%d %H:%M:%S')}")
    print(f"Platform: {platform.system()} {platform.release()}")
    print()
    
    all_results = {}
    
    # ============================================
    # Benchmark'ları Çalıştır
    # ============================================
    
    # 1. C
    print("⚡ C Benchmark Çalıştırılıyor...")
    c_data = run_c_benchmark()
    if 'results' in c_data:
        all_results['C'] = c_data['results']
        print("✅ C Benchmark Tamamlandı!")
    else:
        print(f"⚠️  C Benchmark Atlandı: {c_data.get('error', 'Bilinmeyen hata')}")
        all_results['C'] = {}
    
    # 2. JavaScript
    print("🟨 JavaScript Benchmark Çalıştırılıyor...")
    js_data = run_javascript_benchmark()
    if 'results' in js_data:
        all_results['JavaScript'] = js_data['results']
        print("✅ JavaScript Benchmark Tamamlandı!")
    else:
        print(f"⚠️  JavaScript Benchmark Atlandı: {js_data.get('error', 'Bilinmeyen hata')}")
        all_results['JavaScript'] = {}
    
    # 3. Python
    print("🐍 Python Benchmark Çalıştırılıyor...")
    python_results = run_python_benchmarks()
    python_total = sum([r['time'] for r in python_results.values()])
    all_results['Python'] = {k: v['time'] for k, v in python_results.items()}
    all_results['Python']['total'] = python_total
    print("✅ Python Benchmark Tamamlandı!")
    
    # 4. PHP
    print("🐘 PHP Benchmark Çalıştırılıyor...")
    php_data = run_php_benchmark()
    if 'results' in php_data:
        all_results['PHP'] = php_data['results']
        print("✅ PHP Benchmark Tamamlandı!")
    else:
        print(f"⚠️  PHP Benchmark Atlandı: {php_data.get('error', 'Bilinmeyen hata')}")
        all_results['PHP'] = {}
    
    # 5. Go
    print("🔵 Go Benchmark Çalıştırılıyor...")
    go_data = run_go_benchmark()
    if 'results' in go_data:
        all_results['Go'] = go_data['results']
        print("✅ Go Benchmark Tamamlandı!")
    else:
        print(f"⚠️  Go Benchmark Atlandı: {go_data.get('error', 'Bilinmeyen hata')}")
        all_results['Go'] = {}
    
    # 6. Rust
    print("🦀 Rust Benchmark Çalıştırılıyor...")
    rust_data = run_rust_benchmark()
    if 'results' in rust_data:
        all_results['Rust'] = rust_data['results']
        print("✅ Rust Benchmark Tamamlandı!")
    else:
        print(f"⚠️  Rust Benchmark Atlandı: {rust_data.get('error', 'Bilinmeyen hata')}")
        all_results['Rust'] = {}

    # 7. Tulpar

    print("🐎 Tulpar Benchmark Çalıştırılıyor...")
    tulpar_data = run_tulpar_benchmark()
    if 'results' in tulpar_data:
        all_results['Tulpar'] = tulpar_data['results']
        print("✅ Tulpar Benchmark Tamamlandı!")
    else:
        print(f"⚠️  Tulpar Benchmark Atlandı: {tulpar_data.get('error', 'Bilinmeyen hata')}")
        all_results['Tulpar'] = {}
    
    print()
    
    # ============================================
    # Detaylı Sonuçlar
    # ============================================
    print("=" * 80)
    print("                           DETAYLI SONUÇLAR")
    print("=" * 80)
    print()
    
    test_names = {
        'fibonacci': 'Fibonacci(30)',
        'factorial': 'Factorial(20)',
        'ackermann': 'Ackermann(3, 8)',
        'tak': 'Tak(18, 12, 6)',
        'loop': 'Loop 1M',
        'sieve': 'Sieve(10K)',
        'bubble': 'BubbleSort(1K)',
        'stringconcat': 'StringConcat(1K)',
        'arraymemory': 'ArrayMemory(10K)',
        'stringalloc': 'StringAlloc(1K)',
        'jsonbuild': 'JSONBuild(1K)'
    }
    
    languages = ['C', 'Rust', 'Go', 'JavaScript', 'Python', 'PHP', 'Tulpar']
    
    # Header
    header = f"  {'Test':<20}"
    for lang in languages:
        header += f" {lang:<14}"
    print(header)
    print("  " + "-" * (20 + 15 * len(languages)))
    
    # Test Sonuçları
    for key, name in test_names.items():
        row = f"  {name:<20}"
        for lang in languages:
            val = all_results.get(lang, {}).get(key, 0)
            row += f" {format_time(val):<14}"
        print(row)
    
    print()
    
    # ============================================
    # Toplam Karşılaştırma
    # ============================================
    print("=" * 80)
    print("                         TOPLAM SÜRE KARŞILAŞTIRMASI")
    print("=" * 80)
    print()
    
    # Sıralama için toplam süreleri al
    totals = []
    for lang in languages:
        total = all_results.get(lang, {}).get('total', 0)
        if total > 0:
            totals.append((lang, total))
    
    # Sırala (en hızlıdan en yavaşa)
    totals.sort(key=lambda x: x[1])
    
    if totals:
        # Tulpar'ın süresini bul
        tulpar_total = all_results.get('Tulpar', {}).get('total', 0)
        
        print(f"  {'Sıra':<6} {'Dil':<15} {'Toplam Süre':<18} {'Karşılaştırma (Ref: Tulpar)':<25}")
        print("  " + "-" * 80)
        
        for i, (lang, total) in enumerate(totals, 1):
            comparison = ""
            if lang == "Tulpar":
                comparison = "🔵 REFERANS"
            elif tulpar_total > 0:
                if total < tulpar_total:
                    ratio = tulpar_total / total
                    comparison = f"🚀 {ratio:.2f}x daha HIZLI"
                else:
                    ratio = total / tulpar_total
                    comparison = f"📉 {ratio:.2f}x daha YAVAŞ"
            else:
                 comparison = "N/A"
            
            medal = "🥇" if i == 1 else "🥈" if i == 2 else "🥉" if i == 3 else "  "
            print(f"  {medal} {i:<3} {lang:<15} {format_time(total):<18} {comparison:<25}")
    
    print()
    
    # ============================================
    # Özet
    # ============================================
    print("=" * 80)
    print("                              📊 ÖZET")
    print("=" * 80)
    print()
    
    if totals and tulpar_total > 0:
        tulpar_rank = next((i for i, (l, _) in enumerate(totals, 1) if l == 'Tulpar'), 0)
        
        print(f"  🐎 Tulpar Sıralaması: {tulpar_rank}/{len(totals)}")
        print(f"  ⏱️  Tulpar Toplam Süre: {format_time(tulpar_total)}")
        
        fastest_lang, fastest_time = totals[0]
        if fastest_lang != 'Tulpar':
             print(f"  🏎️  En Hızlı ({fastest_lang}): {format_time(fastest_time)}")
        
        # Python karşılaştırması
        python_total = all_results.get('Python', {}).get('total', 0)
        if python_total > 0:
             ratio = python_total / tulpar_total
             print(f"  🎉 Python'dan {ratio:.2f}x daha HIZLI!")
    
    print()
    print("=" * 80)
    
    # ============================================
    # Dosyaya Kaydet
    # ============================================
    output_file = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'benchmark_results.txt')
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write("=" * 80 + "\n")
        f.write("MULTI-LANGUAGE PERFORMANS KARŞILAŞTIRMASI\n")
        f.write("Tulpar vs Python vs C vs JavaScript vs PHP vs Go vs Rust\n")
        f.write("=" * 80 + "\n")
        f.write(f"Tarih: {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write(f"Platform: {platform.system()} {platform.release()}\n\n")
        
        f.write("-" * 80 + "\n")
        f.write("TOPLAM SÜRELER (Sıralı) - Referans: Tulpar\n")
        f.write("-" * 80 + "\n\n")
        
        for i, (lang, total) in enumerate(totals, 1):
             comp = ""
             if lang == "Tulpar":
                 comp = "(REFERANS)"
             elif tulpar_total > 0:
                if total < tulpar_total:
                    comp = f"({tulpar_total/total:.2f}x HIZLI)"
                else:
                    comp = f"({total/tulpar_total:.2f}x YAVAS)"
             
             f.write(f"{i}. {lang}: {format_time(total)} {comp}\n")
        
        f.write("\n" + "=" * 80 + "\n")
    
    print(f"\n  📁 Sonuçlar kaydedildi: {output_file}")


if __name__ == "__main__":
    main()
