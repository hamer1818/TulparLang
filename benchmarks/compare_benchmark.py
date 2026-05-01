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

def _measure_avg(func, iterations):
    """Run func() `iterations` times after a warmup, return avg ms per call.

    Inner-loop averaging matches the methodology used by C/Rust/Go/Tulpar AOT
    benchmarks so per-call times are directly comparable across languages.
    """
    warmup = max(1, iterations // 10)
    for _ in range(warmup):
        func()
    start = time.perf_counter()
    for _ in range(iterations):
        result = func()
    end = time.perf_counter()
    avg_ms = ((end - start) / iterations) * 1000.0
    return result, avg_ms


# Standard benchmark workload. Every language runs these exact parameters
# with these inner iteration counts. Expected results are recorded for
# correctness verification.
PYTHON_WORKLOAD = [
    # (key,            iters,  callable,                       expected_result)
    ('fibonacci',      100,    lambda: fibonacci(30),          832040),
    ('factorial',      100000, lambda: factorial(20),          2432902008176640000),
    ('ackermann',      50,     lambda: ackermann(3, 8),        2045),
    ('tak',            200,    lambda: tak(18, 12, 6),         7),
    ('loop',           100,    lambda: loop_test(1000000),     499999500000),
    ('sieve',          1000,   lambda: sieve(10000),           1229),
    ('bubble',         50,     lambda: bubble_sort(1000),      None),  # checksum is seed-dependent
    ('stringconcat',   2000,   lambda: string_concat(1000),    1000),
    ('arraymemory',    1000,   lambda: array_memory(10000),    49995000),
    ('stringalloc',    2000,   lambda: string_allocation(1000), 14000),
    ('jsonbuild',      500,    lambda: json_build_test(1000),  4995000),
]


def run_python_benchmarks():
    """Python benchmark testlerini çalıştır (inner-loop averaging)."""
    results = {}
    for key, iters, fn, expected in PYTHON_WORKLOAD:
        last_result, avg_ms = _measure_avg(fn, iters)
        ok = (expected is None) or (last_result == expected)
        results[key] = {'result': last_result, 'time': avg_ms, 'expected': expected, 'ok': ok}
    return results


def parse_benchmark_output(output):
    """Benchmark çıktısından süreleri parse et.

    Her test bloğunun ilk 'Sure: <num> <unit>' satırını alır. Önce çıktıyı
    'Test N:' başlıklarına bölerek izole eder; bu sayede 'Sure: N/A' gören
    bir test, sonraki testin değerini greedy biçimde çekemez.
    """
    results = {}

    test_keys = {
        1: 'fibonacci', 2: 'factorial', 3: 'ackermann', 4: 'tak',
        5: 'loop', 6: 'sieve', 7: 'bubble', 8: 'stringconcat',
        9: 'arraymemory', 10: 'stringalloc', 11: 'jsonbuild',
    }

    # Her testin gövdesini izole et. (?=Test N:|sonu) ile bir sonraki bloğa
    # taşmayı engelliyoruz; aksi halde 'N/A' içeren testler komşu Sure'yi yer.
    # Note: Windows encodes µ as "Âµ" or "µ"; Go outputs "ns" for very small times.
    sure_re = re.compile(
        r'Sure:\s*([\d.eE+-]+)\s*(ms|[µÂ]+s|us|ns)',
        re.IGNORECASE,
    )

    for num, name in test_keys.items():
        # Test N başlığından, sonraki Test başlığına (veya çıktının sonuna)
        # kadarki bloğu yakala.
        block_re = re.compile(
            rf'Test {num}\s*:(.*?)(?=Test {num + 1}\s*:|TOPLAM\s*SURE|\Z)',
            re.DOTALL | re.IGNORECASE,
        )
        block_match = block_re.search(output)
        if not block_match:
            continue
        block = block_match.group(1)
        # Bloğun ilk geçerli 'Sure:' satırını al — N/A satırı içermez (regex
        # numara bekliyor, N/A eşleşmez).
        m = sure_re.search(block)
        if not m:
            continue
        val = float(m.group(1))
        unit = m.group(2).lower()
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
    python_total = sum(r['time'] for r in python_results.values())
    all_results['Python'] = {k: v['time'] for k, v in python_results.items()}
    all_results['Python']['total'] = python_total
    # Correctness check on Python (the reference implementation in this script).
    bad = [(k, v['result'], v['expected']) for k, v in python_results.items() if not v['ok']]
    if bad:
        print(f"⚠️  Python sonuç hatası: {bad}")
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
    
    # Standard workload labels (must match parameters in every benchmark.* file).
    test_names = {
        'fibonacci':    'Fibonacci(30)',
        'factorial':    'Factorial(20)',
        'ackermann':    'Ackermann(3, 8)',
        'tak':          'Tak(18, 12, 6)',
        'loop':         'Loop 1M',
        'sieve':        'Sieve(10K)',
        'bubble':       'BubbleSort(1K)',
        'stringconcat': 'StringConcat(1K)',
        'arraymemory': 'ArrayMemory(10K)',
        'stringalloc': 'StringAlloc(1K)',
        'jsonbuild':    'JSONBuild(1K)',
    }

    languages = ['C', 'Rust', 'Go', 'JavaScript', 'Python', 'PHP', 'Tulpar']
    
    # Header
    header = f"  {'Test':<20}"
    for lang in languages:
        header += f" {lang:<14}"
    print(header)
    print("  " + "-" * (20 + 15 * len(languages)))
    
    # Test Sonuçları. Eksik anahtarlar (parser bir sayı bulamamış =
    # test skip edilmiş veya çıktı parse edilememiş) None olarak akıyor;
    # format_time bunu "N/A" olarak basıyor — sıfırla karıştırılmıyor.
    for key, name in test_names.items():
        row = f"  {name:<20}"
        for lang in languages:
            val = all_results.get(lang, {}).get(key)
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
    
    # Adil karşılaştırma için toplamı, *tüm* dillerin ölçtüğü ortak test
    # alt-kümesi üzerinden hesapla. Bu olmadan, bazı testleri skip eden bir
    # dil yapay olarak hızlı görünür (toplam = daha az testin sürelerinin
    # toplamı).
    test_keys = list(test_names.keys())
    measured_keys_per_lang = {
        lang: {k for k in test_keys if all_results.get(lang, {}).get(k) is not None}
        for lang in languages
    }
    common_keys = set(test_keys)
    for lang in languages:
        if measured_keys_per_lang[lang]:  # En az bir ölçümü olan dilleri dikkate al
            common_keys &= measured_keys_per_lang[lang]

    if common_keys:
        print(f"  (Ortak alt küme: {len(common_keys)} test — {sorted(common_keys)})")
        print()

    # Toplamı yalnızca ortak testler üzerinden hesapla.
    totals = []
    for lang in languages:
        lang_results = all_results.get(lang, {})
        if not lang_results:
            continue
        # Ortak testlerin hepsini ölçtü mü?
        if not common_keys.issubset(measured_keys_per_lang[lang]):
            continue
        total = sum(lang_results[k] for k in common_keys)
        if total > 0:
            totals.append((lang, total))

    # Sırala (en hızlıdan en yavaşa)
    totals.sort(key=lambda x: x[1])
    
    if totals:
        # Tulpar'ın ortak-alt-küme toplamı
        tulpar_total = next((t for l, t in totals if l == 'Tulpar'), 0)

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
        
        # Python karşılaştırması (ortak alt küme üzerinden)
        python_total = next((t for l, t in totals if l == 'Python'), 0)
        if python_total > 0:
            ratio = python_total / tulpar_total
            if ratio >= 1:
                print(f"  🎉 Python'dan {ratio:.2f}x daha HIZLI!")
            else:
                print(f"  📉 Python'dan {1.0/ratio:.2f}x daha YAVAS")
    
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
