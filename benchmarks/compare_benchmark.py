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
def fibonacci(n):
    if n <= 1:
        return n
    return fibonacci(n - 1) + fibonacci(n - 2)

def factorial(n):
    if n <= 1:
        return 1
    return n * factorial(n - 1)

def loop_test(iterations):
    sum_val = 0
    for i in range(iterations):
        sum_val += i
    return sum_val

def nested_loop(size):
    count = 0
    for i in range(size):
        for j in range(size):
            count += 1
    return count

def string_test(iterations):
    result = ""
    for i in range(iterations):
        result += "a"
    return len(result)


def run_python_benchmarks():
    """Python benchmark testlerini çalıştır"""
    results = {}
    
    start = time.perf_counter()
    fib_result = fibonacci(30)
    end = time.perf_counter()
    results['fibonacci'] = {'result': fib_result, 'time': (end - start) * 1000}
    
    start = time.perf_counter()
    fact_result = factorial(20)
    end = time.perf_counter()
    results['factorial'] = {'result': fact_result, 'time': (end - start) * 1000}
    
    start = time.perf_counter()
    loop_result = loop_test(1000000)
    end = time.perf_counter()
    results['loop'] = {'result': loop_result, 'time': (end - start) * 1000}
    
    start = time.perf_counter()
    nested_result = nested_loop(1000)
    end = time.perf_counter()
    results['nested'] = {'result': nested_result, 'time': (end - start) * 1000}
    
    start = time.perf_counter()
    str_result = string_test(100000)
    end = time.perf_counter()
    results['string'] = {'result': str_result, 'time': (end - start) * 1000}
    
    return results


def parse_benchmark_output(output):
    """Benchmark çıktısından süreleri parse et"""
    results = {}
    
    # Test sonuçlarını parse et
    test_patterns = [
        ('fibonacci', r'Test 1:.*?Sure:\s*([\d.]+)\s*(ms|µs|us)'),
        ('factorial', r'Test 2:.*?Sure:\s*([\d.]+)\s*(ms|µs|us)'),
        ('loop', r'Test 3:.*?Sure:\s*([\d.]+)\s*(ms|µs|us)'),
        ('nested', r'Test 4:.*?Sure:\s*([\d.]+)\s*(ms|µs|us)'),
        ('string', r'Test 5:.*?Sure:\s*([\d.]+)\s*(ms|µs|us)'),
    ]
    
    for name, pattern in test_patterns:
        match = re.search(pattern, output, re.DOTALL | re.IGNORECASE)
        if match:
            val = float(match.group(1))
            unit = match.group(2).lower()
            if unit in ['µs', 'us']:
                val /= 1000.0  # µs -> ms
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
    tulpar_exe = os.path.join(script_dir, '..', 'tulpar.exe')
    if not os.path.exists(tulpar_exe):
        tulpar_exe = os.path.join(script_dir, '..', 'tulpar')
    benchmark_file = os.path.join(script_dir, 'benchmark.tpr')
    
    if not os.path.exists(tulpar_exe):
        return {'error': f'Tulpar bulunamadı: {tulpar_exe}'}
    
    try:
        result = subprocess.run([tulpar_exe, benchmark_file], capture_output=True, text=True, timeout=300)
        return {'results': parse_benchmark_output(result.stdout), 'output': result.stdout}
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


def format_time(ms):
    if ms is None or ms == 0:
        return "N/A"
    elif ms < 1:
        return f"{ms * 1000:.2f} µs"
    elif ms < 1000:
        return f"{ms:.3f} ms"
    else:
        return f"{ms / 1000:.3f} s"


def main():
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
    
    # 5. Tulpar
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
        'loop': 'Loop 1M',
        'nested': 'Nested 1000x1000',
        'string': 'String 100K'
    }
    
    languages = ['C', 'JavaScript', 'Python', 'PHP', 'Tulpar']
    
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
        fastest_lang, fastest_time = totals[0]
        
        print(f"  {'Sıra':<6} {'Dil':<15} {'Toplam Süre':<18} {'Karşılaştırma':<25}")
        print("  " + "-" * 70)
        
        for i, (lang, total) in enumerate(totals, 1):
            if i == 1:
                comparison = "🏆 EN HIZLI"
            else:
                ratio = total / fastest_time
                comparison = f"📉 {ratio:.2f}x yavaş"
            
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
    
    if totals:
        fastest_lang, fastest_time = totals[0]
        
        # Tulpar'ın konumu
        tulpar_total = all_results.get('Tulpar', {}).get('total', 0)
        if tulpar_total > 0:
            tulpar_rank = next((i for i, (l, _) in enumerate(totals, 1) if l == 'Tulpar'), 0)
            ratio_to_fastest = tulpar_total / fastest_time if fastest_time > 0 else 0
            
            print(f"  🐎 Tulpar Sıralaması: {tulpar_rank}/{len(totals)}")
            print(f"  ⏱️  Tulpar Toplam Süre: {format_time(tulpar_total)}")
            print(f"  📈 En hızlıya ({fastest_lang}) göre: {ratio_to_fastest:.2f}x yavaş")
            
            # Python ile karşılaştır
            python_total = all_results.get('Python', {}).get('total', 0)
            if python_total > 0:
                if tulpar_total < python_total:
                    print(f"  🎉 Python'dan {python_total/tulpar_total:.2f}x HIZLI!")
                else:
                    print(f"  📉 Python'dan {tulpar_total/python_total:.2f}x yavaş")
    
    print()
    print("=" * 80)
    
    # ============================================
    # Dosyaya Kaydet
    # ============================================
    output_file = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'benchmark_results.txt')
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write("=" * 80 + "\n")
        f.write("MULTI-LANGUAGE PERFORMANS KARŞILAŞTIRMASI\n")
        f.write("Tulpar vs Python vs C vs JavaScript vs PHP\n")
        f.write("=" * 80 + "\n")
        f.write(f"Tarih: {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write(f"Platform: {platform.system()} {platform.release()}\n\n")
        
        f.write("-" * 80 + "\n")
        f.write("TOPLAM SÜRELER (Sıralı)\n")
        f.write("-" * 80 + "\n\n")
        
        for i, (lang, total) in enumerate(totals, 1):
            f.write(f"{i}. {lang}: {format_time(total)}\n")
        
        f.write("\n" + "=" * 80 + "\n")
    
    print(f"\n  📁 Sonuçlar kaydedildi: {output_file}")


if __name__ == "__main__":
    main()
