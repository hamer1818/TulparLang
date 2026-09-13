// L0 PLATFORM (Android) — Kotlin host <-> motor JNI koprusu (plan Faz 1,
// REV 5/11). Saf NativeActivity yerine Kotlin Activity + SurfaceView: Play
// Asset Delivery, Firebase, ADPF hint, uygulama ici guncelleme Java/Kotlin
// tarafindan gecer. Girdi TIMESTAMP'LI callback ile gelir (MotionEvent
// eventTime, ns'ye cevrilmis): sim o zamani kullanir, kare basini degil.
//
// ⚠ DERLENMEDI (2026-09-14): bu makinede NDK yok. Ilk NDK derlemesinde
// imzalar dogrulanir; kadar tasarim kaydidir, calisan kod degil.
#if defined(__ANDROID__)
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <atomic>
#include <cstdint>

namespace {
struct TouchEvent {
  int32_t action;   // 0 down, 1 up, 2 move, 3 cancel
  int32_t pointer;
  float x, y;
  int64_t time_ns;  // MotionEvent.getEventTime() * 1e6 (uptime saati)
};
// Sabit halka (A2): host thread yazar, motor kare basinda okur. Dolarsa en
// eski dusurulur ve SAYILIR.
constexpr uint32_t kRing = 256;
TouchEvent g_ring[kRing];
std::atomic<uint32_t> g_head{0}, g_tail{0}, g_dropped{0};
ANativeWindow *g_window = nullptr;
std::atomic<int> g_state{0}; // 0 arka plan, 1 on plan
} // namespace

extern "C" {
JNIEXPORT jint JNI_OnLoad(JavaVM *, void *) { return JNI_VERSION_1_6; }

JNIEXPORT void JNICALL Java_dev_tulparlang_engine_TulparActivity_nativeSurfaceCreated(JNIEnv *env, jobject,
                                                                                        jobject surface) {
  if (g_window) ANativeWindow_release(g_window);
  g_window = ANativeWindow_fromSurface(env, surface); // Faz 1: Vulkan yuzeyi buradan
}
JNIEXPORT void JNICALL Java_dev_tulparlang_engine_TulparActivity_nativeSurfaceDestroyed(JNIEnv *, jobject) {
  if (g_window) ANativeWindow_release(g_window);
  g_window = nullptr;
}
JNIEXPORT void JNICALL Java_dev_tulparlang_engine_TulparActivity_nativeSetForeground(JNIEnv *, jobject, jboolean fg) {
  g_state.store(fg ? 1 : 0);
}
JNIEXPORT void JNICALL Java_dev_tulparlang_engine_TulparActivity_nativeTouch(JNIEnv *, jobject, jint action,
                                                                              jint pointer, jfloat x, jfloat y,
                                                                              jlong event_time_ms) {
  uint32_t h = g_head.load(std::memory_order_relaxed), t = g_tail.load(std::memory_order_acquire);
  if (h - t >= kRing) { g_dropped.fetch_add(1); g_tail.fetch_add(1); }
  g_ring[h % kRing] = TouchEvent{action, pointer, x, y, (int64_t)event_time_ms * 1000000};
  g_head.store(h + 1, std::memory_order_release);
}
// Motor tarafi: kare basinda tuketir (Faz 1 pencere dongusu).
uint32_t tulpar_android_poll_touch(TouchEvent *out, uint32_t max) {
  uint32_t n = 0, t = g_tail.load(std::memory_order_relaxed), h = g_head.load(std::memory_order_acquire);
  while (t != h && n < max) out[n++] = g_ring[t++ % kRing];
  g_tail.store(t, std::memory_order_release);
  return n;
}
ANativeWindow *tulpar_android_window() { return g_window; }
}
#endif
