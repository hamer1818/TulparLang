# Wine ortami — `source windows/wine_env.sh` ile kullanilir.
#
# Ne yapar:
#   * AYRI bir WINEPREFIX kullanir (~/.tulpar-wine): testler kullanicinin
#     normal Wine kurulumunu kirletmez, sifirlamak `rm -rf` kadar kolaydir.
#   * Sysroot'un bin/ dizinini WINDOWS PATH'ine ekler. AOT link adimi
#     Windows tarafinda `g++` cagirir; DLL'ler (libstdc++-6, libwinpthread-1,
#     zlib1, libzstd, libssl-3-x64 ...) de oradan bulunur.
#   * WINEDEBUG=-all: Wine'in fixme/err gurultusu test ciktisini bogmasin.
#
# DIKKAT — Wine, Windows DEGILDIR. Burada yesil olmak "Windows'ta calisir"
# demek degil: konsol kod sayfasi, cmd.exe tirnaklama, winsock kenar
# durumlari ve GPU surucusu Wine'da baska davranir. Wine HIZLI DONGU icindir;
# kabul testi gercek Windows'ta yapilir.

_TULPAR_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
_TULPAR_SYSROOT="$_TULPAR_ROOT/windows/dist/mingw64"

export WINEPREFIX="${WINEPREFIX:-$HOME/.tulpar-wine}"
export WINEDEBUG="${WINEDEBUG:--all}"
# Wine'in PATH'i ';' ile ayrilir ve Z: sürücüsü Linux kökünü gösterir.
export WINEPATH="$(printf '%s' "$_TULPAR_SYSROOT/bin" | sed 's|/|\\|g')"
export WINEPATH="Z:$WINEPATH"

# AOT link surucusu: Windows'ta clang++ yerine sysroot'taki g++ kullanilir
# (ayni GCC 16.2.0 / MSVCRT ikilisi, yani uretilen .exe ile libtulpar_runtime.a
# ayni ABI'de bulusur). Bu degiskeni aot_pipeline.cpp okur.
export TULPAR_CC="${TULPAR_CC:-g++}"

echo "[wine_env] WINEPREFIX=$WINEPREFIX"
echo "[wine_env] WINEPATH=$WINEPATH"
echo "[wine_env] TULPAR_CC=$TULPAR_CC"
