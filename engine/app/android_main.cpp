// L5 APP — Android NativeActivity host (native_app_glue). Iki kip, sistem
// ozelligi ile secilir (adb shell setprop debug.tulpar.mode tests|demo):
//   tests: engine_tests_main() ayni surecte (GPU'ya APP surec baglami erisir;
//          adb shell'den vkEnumeratePhysicalDevices 0 dondu — SELinux/HAL).
//   demo : Faz 2 sahnesi ANativeWindow'a (VK_KHR_android_surface).
// stdout/stderr -> boru -> logcat (etiket "tulpar") + files/engine_log.txt.
#if defined(__ANDROID__)
#define VK_USE_PLATFORM_ANDROID_KHR 1
#include <android/asset_manager.h>
#include <android/input.h>
#include <android/log.h>
#include <android/native_window.h>
#include <sys/stat.h>
#include <android_native_app_glue.h>
#include <pthread.h>
#include <sys/system_properties.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <vulkan/vulkan.h>

#include "app/demo_app.hpp"
#include "platform/crash.hpp"
#include "platform/time.hpp"
#include "platform/touch.hpp"
#include "rhi/vk_api.hpp"

int engine_tests_main(int argc, char **argv);

using namespace tulpar::engine;

namespace {
struct LogPump {
  int fd = -1;
  FILE *file = nullptr;
};
void *log_pump(void *p) {
  LogPump *lp = static_cast<LogPump *>(p);
  char buf[1024];
  size_t n = 0;
  for (;;) {
    ssize_t r = read(lp->fd, buf + n, sizeof buf - 1 - n);
    if (r <= 0) break;
    n += (size_t)r;
    size_t start = 0;
    for (size_t i = 0; i < n; i++) {
      if (buf[i] == '\n') {
        buf[i] = 0;
        __android_log_write(ANDROID_LOG_INFO, "tulpar", buf + start);
        if (lp->file) { std::fputs(buf + start, lp->file); std::fputc('\n', lp->file); std::fflush(lp->file); }
        start = i + 1;
      }
    }
    if (start < n) std::memmove(buf, buf + start, n - start);
    n -= start;
    if (n == sizeof buf - 1) { buf[n] = 0; __android_log_write(ANDROID_LOG_INFO, "tulpar", buf); n = 0; }
  }
  return nullptr;
}
void redirect_output(const char *dir) {
  static LogPump lp;
  char path[512];
  std::snprintf(path, sizeof path, "%s/engine_log.txt", dir);
  lp.file = std::fopen(path, "w");
  int fds[2];
  if (pipe(fds) != 0) return;
  dup2(fds[1], 1);
  dup2(fds[1], 2);
  close(fds[1]);
  lp.fd = fds[0];
  setvbuf(stdout, nullptr, _IOLBF, 0);
  setvbuf(stderr, nullptr, _IONBF, 0);
  pthread_t t;
  pthread_create(&t, nullptr, log_pump, &lp);
  pthread_detach(t);
}
void prop(const char *name, char *out, size_t n, const char *def) {
  char v[PROP_VALUE_MAX] = {0};
  int len = __system_property_get(name, v);
  std::snprintf(out, n, "%s", len > 0 ? v : def);
}

struct AndroidHost {
  android_app *app = nullptr;
  platform::TouchState touch;
  bool window_ready = false;
  bool had_window = false;     // bir kez yuzey yaratildi mi (sonraki INIT = degisim)
  bool window_changed = false;
  bool quit = false;
};
const char *cmd_name(int32_t c) {
  switch (c) {
  case APP_CMD_INIT_WINDOW: return "INIT_WINDOW";
  case APP_CMD_TERM_WINDOW: return "TERM_WINDOW";
  case APP_CMD_WINDOW_RESIZED: return "WINDOW_RESIZED";
  case APP_CMD_GAINED_FOCUS: return "GAINED_FOCUS";
  case APP_CMD_LOST_FOCUS: return "LOST_FOCUS";
  case APP_CMD_CONFIG_CHANGED: return "CONFIG_CHANGED";
  case APP_CMD_START: return "START";
  case APP_CMD_RESUME: return "RESUME";
  case APP_CMD_PAUSE: return "PAUSE";
  case APP_CMD_STOP: return "STOP";
  case APP_CMD_DESTROY: return "DESTROY";
  case APP_CMD_LOW_MEMORY: return "LOW_MEMORY";
  default: return "?";
  }
}
void on_cmd(android_app *app, int32_t cmd) {
  AndroidHost *h = static_cast<AndroidHost *>(app->userData);
  std::printf("[android] cmd %s\n", cmd_name(cmd));
  switch (cmd) {
  case APP_CMD_INIT_WINDOW:
    h->window_ready = app->window != nullptr;
    if (h->window_ready && h->had_window) h->window_changed = true;
    break;
  case APP_CMD_TERM_WINDOW: h->window_ready = false; break; // arka plan: sim surer, cizim durur
  case APP_CMD_DESTROY: h->quit = true; break;
  default: break;
  }
}
int32_t on_input(android_app *app, AInputEvent *ev) {
  AndroidHost *h = static_cast<AndroidHost *>(app->userData);
  if (AInputEvent_getType(ev) != AINPUT_EVENT_TYPE_MOTION) return 0;
  const int32_t action = AMotionEvent_getAction(ev);
  const int32_t kind = action & AMOTION_EVENT_ACTION_MASK;
  const size_t idx = (size_t)((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
  platform::TouchState &t = h->touch;
  t.time_ns = platform::now_ns();
  switch (kind) {
  case AMOTION_EVENT_ACTION_DOWN:
  case AMOTION_EVENT_ACTION_POINTER_DOWN:
    t.begin(AMotionEvent_getPointerId(ev, idx), AMotionEvent_getX(ev, idx), AMotionEvent_getY(ev, idx));
    break;
  case AMOTION_EVENT_ACTION_MOVE:
    for (size_t i = 0; i < AMotionEvent_getPointerCount(ev); i++)
      t.move(AMotionEvent_getPointerId(ev, i), AMotionEvent_getX(ev, i), AMotionEvent_getY(ev, i));
    break;
  case AMOTION_EVENT_ACTION_UP:
  case AMOTION_EVENT_ACTION_POINTER_UP:
    t.end(AMotionEvent_getPointerId(ev, idx));
    break;
  case AMOTION_EVENT_ACTION_CANCEL:
    t.clear();
    break;
  default: break;
  }
  return 1;
}
const platform::TouchState *android_touch(void *user) {
  AndroidHost *h = static_cast<AndroidHost *>(user);
  if (h->app->window) {
    h->touch.width = (float)ANativeWindow_getWidth(h->app->window);
    h->touch.height = (float)ANativeWindow_getHeight(h->app->window);
  }
  return &h->touch;
}
void pump_events(AndroidHost *h, int timeout_ms) {
  int events;
  android_poll_source *src;
  while (ALooper_pollOnce(timeout_ms, nullptr, &events, (void **)&src) >= 0) {
    if (src) src->process(h->app, src);
    if (h->app->destroyRequested) h->quit = true;
    timeout_ms = 0;
  }
}
const char *const *android_exts(void *, uint32_t *n) {
  static const char *exts[] = {"VK_KHR_surface", "VK_KHR_android_surface"};
  *n = 2;
  return exts;
}
bool android_surface(void *user, rhi::VkApi &api, VkInstance inst, VkSurfaceKHR *out) {
  AndroidHost *h = static_cast<AndroidHost *>(user);
  auto fn = (PFN_vkCreateAndroidSurfaceKHR)api.vkGetInstanceProcAddr(inst, "vkCreateAndroidSurfaceKHR");
  if (!fn) { std::printf("[android] vkCreateAndroidSurfaceKHR yok\n"); return false; }
  VkAndroidSurfaceCreateInfoKHR ci{};
  ci.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
  ci.window = h->app->window;
  VkResult r = fn(inst, &ci, nullptr, out);
  if (r != VK_SUCCESS) std::printf("[android] vkCreateAndroidSurfaceKHR = %d\n", (int)r);
  else h->had_window = true;
  return r == VK_SUCCESS;
}
app::HostPoll android_poll(void *user, uint32_t *w, uint32_t *h_) {
  AndroidHost *h = static_cast<AndroidHost *>(user);
  // Pencere yokken (arka plan) donme: olaylari 100 ms bekleyerek al.
  pump_events(h, h->window_ready ? 0 : 100);
  if (h->quit) return app::HostPoll::Quit;
  if (!h->window_ready) return app::HostPoll::NoWindow;
  *w = (uint32_t)ANativeWindow_getWidth(h->app->window);
  *h_ = (uint32_t)ANativeWindow_getHeight(h->app->window);
  if (h->window_changed) { h->window_changed = false; return app::HostPoll::WindowChanged; }
  return app::HostPoll::Run;
}
} // namespace

// --- VK_GOOGLE_display_timing sondasi (Swappy KAPALI, debug.tulpar.dtprobe=1):
// her sunuma presentID ekler ve gecmis sunum zamanlamasini okur. Swappy'nin
// kare istatistigi bu veriye dayanir; 0 kayit = surucu vermiyor (Swappy sucu degil).
#include <dlfcn.h>
struct DtProbe {
  VkDevice device = VK_NULL_HANDLE;
  VkSwapchainKHR sc = VK_NULL_HANDLE;
  PFN_vkQueuePresentKHR present = nullptr;
  PFN_vkGetPastPresentationTimingGOOGLE past = nullptr;
  PFN_vkGetRefreshCycleDurationGOOGLE refresh = nullptr;
  uint32_t id = 0, records = 0, frames = 0, max_batch = 0, past_calls_ok = 0;
  uint64_t last_actual = 0, last_desired = 0, refresh_ns = 0;
  int64_t last_margin = 0;
  bool ok = false;
  // Sunum araligi histogrami (gercek sunum zamanlari arasi / yenileme): 1, 2, 3, >3
  // periyot; >1 = gec kare (dusen vsync). Marj (hazir olma payi) kovalari ms.
  uint32_t interval_hist[4] = {};
  uint32_t margin_hist[5] = {}; // <0, 0-4, 4-8, 8-12, >12 ms
  uint64_t prev_actual = 0;
};
static DtProbe g_dt;
static void dt_on_create(void *user, VkPhysicalDevice, VkDevice dev, VkQueue, uint32_t, VkSwapchainKHR sc) {
  DtProbe *p = static_cast<DtProbe *>(user);
  void *lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
  auto gdpa = lib ? (PFN_vkGetDeviceProcAddr)dlsym(lib, "vkGetDeviceProcAddr") : nullptr;
  if (!gdpa) { std::printf("[android] dt sondasi: vkGetDeviceProcAddr yok\n"); return; }
  p->device = dev; p->sc = sc;
  p->present = (PFN_vkQueuePresentKHR)gdpa(dev, "vkQueuePresentKHR");
  p->past = (PFN_vkGetPastPresentationTimingGOOGLE)gdpa(dev, "vkGetPastPresentationTimingGOOGLE");
  p->refresh = (PFN_vkGetRefreshCycleDurationGOOGLE)gdpa(dev, "vkGetRefreshCycleDurationGOOGLE");
  p->ok = p->present && p->past;
  if (p->refresh) { VkRefreshCycleDurationGOOGLE r{}; if (p->refresh(dev, sc, &r) == VK_SUCCESS) p->refresh_ns = r.refreshDuration; }
  std::printf("[android] dt sondasi: vkQueuePresentKHR %d, vkGetPastPresentationTimingGOOGLE %d, vkGetRefreshCycleDurationGOOGLE %d (yenileme %.3f ms)\n",
              p->present != nullptr, p->past != nullptr, p->refresh != nullptr, p->refresh_ns / 1e6);
}
static VkResult dt_present(void *user, VkQueue q, const VkPresentInfoKHR *pi) {
  DtProbe *p = static_cast<DtProbe *>(user);
  if (!p->ok) return VK_ERROR_DEVICE_LOST;
  VkPresentTimeGOOGLE t{++p->id, 0};
  VkPresentTimesInfoGOOGLE ti{VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE, pi->pNext, 1, &t};
  VkPresentInfoKHR pi2 = *pi;
  pi2.pNext = &ti;
  const VkResult r = p->present(q, &pi2);
  uint32_t n = 0;
  if (p->past(p->device, p->sc, &n, nullptr) == VK_SUCCESS) {
    p->past_calls_ok++;
    if (n) {
      VkPastPresentationTimingGOOGLE arr[16];
      if (n > 16) n = 16;
      const VkResult pr = p->past(p->device, p->sc, &n, arr);
      if ((pr == VK_SUCCESS || pr == VK_INCOMPLETE) && n) {
        p->records += n;
        if (n > p->max_batch) p->max_batch = n;
        for (uint32_t k = 0; k < n; k++) {
          const uint64_t a = arr[k].actualPresentTime;
          if (p->prev_actual && p->refresh_ns && a > p->prev_actual) {
            const uint64_t periods = (a - p->prev_actual + p->refresh_ns / 2) / p->refresh_ns;
            p->interval_hist[periods <= 1 ? 0 : periods == 2 ? 1 : periods == 3 ? 2 : 3]++;
          }
          p->prev_actual = a;
          const double m = (double)(int64_t)arr[k].presentMargin / 1e6;
          p->margin_hist[m < 0 ? 0 : m < 4 ? 1 : m < 8 ? 2 : m < 12 ? 3 : 4]++;
        }
        p->last_actual = arr[n - 1].actualPresentTime;
        p->last_desired = arr[n - 1].desiredPresentTime;
        p->last_margin = (int64_t)arr[n - 1].presentMargin;
      }
    }
  }
  p->frames++;
  return r;
}
static void dt_on_destroy(void *, VkDevice, VkSwapchainKHR) {}
static void dt_report() {
  DtProbe *p = &g_dt;
  std::printf("[android] dt sondasi: %u sunum (presentID ile), gecmis zamanlama sorgusu %u basarili, %u kayit (en cok %u/kare); son gercek sunum %llu ns, istenen %llu, marj %lld ns\n",
              p->frames, p->past_calls_ok, p->records, p->max_batch, (unsigned long long)p->last_actual, (unsigned long long)p->last_desired,
              (long long)p->last_margin);
  std::printf("[android] dt sondasi: sunum araligi [1 2 3 >3 periyot]: %u %u %u %u (gec kare = 1 disindakiler) | marj [<0 0-4 4-8 8-12 >12 ms]: %u %u %u %u %u\n",
              p->interval_hist[0], p->interval_hist[1], p->interval_hist[2], p->interval_hist[3], p->margin_hist[0], p->margin_hist[1],
              p->margin_hist[2], p->margin_hist[3], p->margin_hist[4]);
}

#if ENGINE_SWAPPY
// AGDK Swappy (kare temposu): swapchain yaratilinca baglam kurulur (JNI env bu
// thread'e baglanir; NativeActivity jobject = activity->clazz), sunum
// SwappyVk_queuePresent ile; sonda istatistik (gec kare / bekleme histogrami).
#include <atomic>
#include <csignal>
#include <jni.h>
#include <pthread.h>
#include <unistd.h>
#include <dlfcn.h>
#include <swappy/swappyVk.h>
#include "platform/crash.hpp"
#include "platform/time.hpp"
struct SwappyCtx {
  android_app *app = nullptr;
  VkDevice device = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  uint64_t refresh_ns = 0;
  bool ok = false;
  // Bekci: sunum cagrisi 15 s icinde donmezse (ya da hic gelmezse) surec
  // abort() ile dusurulur; debuggerd TUM thread'lerin yiginini logcat crash
  // tamponuna yazar (android_run.sh basar). Takilma sessiz siyah ekran olmasin.
  std::atomic<uint32_t> present_calls{0}, present_done{0};
  uint64_t created_ns = 0;
  bool watchdog = false;
  pthread_t present_thread{};       // ilk sunumu cagiran (ana) thread — takilinca yigini basilir
  pid_t present_tid = 0;            // /proc/self/task/<tid>/{stat,wchan,syscall} icin
  std::atomic<bool> have_thread{false}, dumped{false};
  bool set_family = true; // deney: debug.tulpar.swappy_family=0 -> SwappyVk_setQueueFamilyIndex atlanir
  SwappyStats stats{};    // swapchain yok edilmeden once alinir (sonra 0 doner)
};
static SwappyCtx g_swappy;
// SIGUSR1: takilan thread kendi yiginini basar (dladdr ile lib+offset; masaustunde
// llvm-symbolizer --obj=build-android/libtulparengine.so ile cozulur). Huawei'de
// debuggerd tombstone'u logcat'e DUSMUYOR (olculdu 2026-09-15), bu yol kaldi.
static void swappy_dump_handler(int) {
  void *pcs[48];
  const int n = platform::crash_capture_frames(pcs, 48);
  char tname[32] = "?";
  pthread_getname_np(pthread_self(), tname, sizeof tname);
  std::printf("[android] swappy BEKCI yigin (thread %s, %d kare):\n", tname, n);
  for (int i = 0; i < n; i++) {
    Dl_info info{};
    if (dladdr(pcs[i], &info) && info.dli_fname) {
      const char *base = std::strrchr(info.dli_fname, '/');
      std::printf("  #%02d %s+0x%lx %s\n", i, base ? base + 1 : info.dli_fname, (unsigned long)((uintptr_t)pcs[i] - (uintptr_t)info.dli_fbase),
                  info.dli_sname ? info.dli_sname : "");
    } else std::printf("  #%02d %p\n", i, pcs[i]);
  }
  std::fflush(stdout);
  g_swappy.dumped = true;
}
static void *swappy_watchdog(void *user) {
  SwappyCtx *c = static_cast<SwappyCtx *>(user);
  uint32_t last_done = 0;
  uint64_t last_progress_ns = c->created_ns;
  for (;;) {
    usleep(250 * 1000);
    const uint32_t calls = c->present_calls.load(), done = c->present_done.load();
    const uint64_t now = platform::now_ns();
    if (done != last_done) { last_done = done; last_progress_ns = now; }
    if (now - last_progress_ns > 15ull * 1000000000ull) {
      std::printf("[android] swappy BEKCI: %u sunum cagrisi, %u tamamlandi; 15 s ilerleme yok -> abort (thread yiginlari logcat crash)\n", calls, done);
      std::fflush(stdout);
      if (c->have_thread.load()) {
        // Cekirdek durumu: sinyal yanitsiz kalirsa (kesilemez uyku, surucu ioctl) bu kalir.
        const char *files[] = {"stat", "wchan", "syscall"};
        for (const char *f : files) {
          char path[64], buf[512];
          std::snprintf(path, sizeof path, "/proc/self/task/%d/%s", (int)c->present_tid, f);
          FILE *fp = std::fopen(path, "r");
          size_t n = fp ? std::fread(buf, 1, sizeof buf - 1, fp) : 0;
          if (fp) std::fclose(fp);
          buf[n] = 0;
          for (size_t k = 0; k < n; k++) if (buf[k] == '\n') buf[k] = ' ';
          std::printf("[android] swappy BEKCI %s(tid %d): %s\n", f, (int)c->present_tid, n ? buf : "(okunamadi)");
        }
        std::fflush(stdout);
        struct sigaction sa{};
        sa.sa_handler = swappy_dump_handler;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGUSR1, &sa, nullptr);
        pthread_kill(c->present_thread, SIGUSR1);
        for (int i = 0; i < 40 && !c->dumped.load(); i++) usleep(50 * 1000);
        if (!c->dumped.load()) std::printf("[android] swappy BEKCI: yigin alinamadi (thread sinyale yanit vermedi)\n");
      } else std::printf("[android] swappy BEKCI: sunum hic cagrilmadi (takilma init/acquire oncesi)\n");
      std::fflush(stdout);
      usleep(300 * 1000); // boru okuyucusu yazsin
      _exit(3);
    }
  }
  return nullptr;
}
static void swappy_on_create(void *user, VkPhysicalDevice phys, VkDevice dev, VkQueue q, uint32_t queue_family, VkSwapchainKHR sc) {
  SwappyCtx *c = static_cast<SwappyCtx *>(user);
  JNIEnv *env = nullptr;
  if (c->app->activity->vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) { std::printf("[android] swappy: JNI env yok\n"); return; }
  // Bekci init'ten ONCE: init/setWindow icinde takilma da yakalansin.
  c->created_ns = platform::now_ns();
  if (!c->watchdog) {
    pthread_t th;
    if (pthread_create(&th, nullptr, swappy_watchdog, c) == 0) { pthread_detach(th); c->watchdog = true; }
  }
  if (c->set_family) SwappyVk_setQueueFamilyIndex(dev, q, queue_family);
  else std::printf("[android] swappy DENEY: kuyruk ailesi bildirilmedi\n");
  std::fflush(stdout);
  uint64_t refresh = 0;
  c->ok = SwappyVk_initAndGetRefreshCycleDuration(env, c->app->activity->clazz, phys, dev, sc, &refresh);
  c->device = dev;
  c->swapchain = sc;
  c->refresh_ns = refresh;
  if (!c->ok) { std::printf("[android] swappy: init basarisiz\n"); return; }
  std::printf("[android] swappy: init tamam\n"); std::fflush(stdout);
  SwappyVk_setWindow(dev, sc, c->app->window);
  std::printf("[android] swappy: setWindow tamam\n"); std::fflush(stdout);
  SwappyVk_setSwapIntervalNS(dev, sc, SWAPPY_SWAP_60FPS);
  SwappyVk_enableStats(sc, true);
  bool enabled = false;
  SwappyVk_isEnabled(sc, &enabled);
  std::printf("[android] swappy: yenileme %.2f ms, hedef 60 fps, istatistik acik, kuyruk ailesi %u, etkin %d, surum %u\n", refresh / 1e6, queue_family,
              (int)enabled, Swappy_version());
  std::fflush(stdout);
}
static void swappy_on_destroy(void *user, VkDevice dev, VkSwapchainKHR sc) {
  SwappyCtx *c = static_cast<SwappyCtx *>(user);
  if (c->ok) { SwappyVk_getStats(sc, &c->stats); SwappyVk_destroySwapchain(dev, sc); }
  c->swapchain = VK_NULL_HANDLE;
}
static VkResult swappy_present(void *user, VkQueue q, const VkPresentInfoKHR *pi) {
  SwappyCtx *c = static_cast<SwappyCtx *>(user);
  if (!c->ok) return VK_ERROR_DEVICE_LOST;
  if (!c->have_thread.load()) { c->present_thread = pthread_self(); c->present_tid = gettid(); c->have_thread = true; }
  c->present_calls++;
  SwappyVk_recordFrameStart(q, c->swapchain, pi->pImageIndices ? pi->pImageIndices[0] : 0);
  const VkResult r = SwappyVk_queuePresent(q, pi);
  c->present_done++;
  return r;
}
static void swappy_report() {
  SwappyCtx *c = &g_swappy;
  if (!c->ok) return;
  const SwappyStats &st = c->stats;
  std::printf("[android] swappy istatistik: %llu kare | gec kare [0..5]: %llu %llu %llu %llu %llu %llu | onceki kareden kayma [0..5]: %llu %llu %llu %llu %llu %llu | bekleme [0..5]: %llu %llu %llu %llu %llu %llu\n",
              (unsigned long long)st.totalFrames, (unsigned long long)st.lateFrames[0], (unsigned long long)st.lateFrames[1],
              (unsigned long long)st.lateFrames[2], (unsigned long long)st.lateFrames[3], (unsigned long long)st.lateFrames[4],
              (unsigned long long)st.lateFrames[5], (unsigned long long)st.offsetFromPreviousFrame[0],
              (unsigned long long)st.offsetFromPreviousFrame[1], (unsigned long long)st.offsetFromPreviousFrame[2],
              (unsigned long long)st.offsetFromPreviousFrame[3], (unsigned long long)st.offsetFromPreviousFrame[4],
              (unsigned long long)st.offsetFromPreviousFrame[5], (unsigned long long)st.idleFrames[0], (unsigned long long)st.idleFrames[1],
              (unsigned long long)st.idleFrames[2], (unsigned long long)st.idleFrames[3], (unsigned long long)st.idleFrames[4],
              (unsigned long long)st.idleFrames[5]);
  if (c->device) SwappyVk_destroyDevice(c->device);
  c->ok = false;
}
#endif

extern "C" void android_main(android_app *app) {
  AndroidHost host;
  host.app = app;
  app->userData = &host;
  app->onAppCmd = on_cmd;
  app->onInputEvent = on_input;
  // Cikti dizini: harici app dizini (/sdcard/Android/data/<pkg>/files) — adb pull ile
  // okunur; run-as Huawei'de calismiyor ("/data has wrong owner"). Yoksa dahili.
  const char *dir = app->activity->externalDataPath ? app->activity->externalDataPath : app->activity->internalDataPath;
  redirect_output(dir);
  setenv("TMPDIR", dir, 1);
  // Varliklar APK'nin assets/ dizininden DAHILI dizine cikarilir (cgltf fopen
  // ister; adb push ile dis dizine konan dosyayi kapsamli depolama olan
  // surumler uygulamaya OKUTMUYOR — emulator API 37'de olculdu).
  static char assets[512];
  std::snprintf(assets, sizeof assets, "%s/assets", app->activity->internalDataPath);
  mkdir(assets, 0700);
  uint32_t extracted = 0;
  if (AAssetDir *ad = AAssetManager_openDir(app->activity->assetManager, "")) {
    while (const char *name = AAssetDir_getNextFileName(ad)) {
      // Yalniz bizim icerik turlerimiz: Huawei'de kok dizin listesi sistem
      // kaplamalarini da getiriyor (APK'da 1 dosya, listede 16).
      const char *dot = std::strrchr(name, '.');
      if (!dot) continue;
      const bool ours = !std::strcmp(dot, ".gltf") || !std::strcmp(dot, ".glb") || !std::strcmp(dot, ".bin") ||
                        !std::strcmp(dot, ".png") || !std::strcmp(dot, ".jpg") || !std::strcmp(dot, ".ktx2") ||
                        !std::strcmp(dot, ".ttf") || !std::strcmp(dot, ".sahne");
      if (!ours) continue;
      AAsset *as = AAssetManager_open(app->activity->assetManager, name, AASSET_MODE_BUFFER);
      if (!as) continue;
      char path[1024];
      std::snprintf(path, sizeof path, "%s/%s", assets, name);
      if (FILE *f = std::fopen(path, "wb")) {
        std::fwrite(AAsset_getBuffer(as), 1, (size_t)AAsset_getLength(as), f);
        std::fclose(f);
        extracted++;
      }
      AAsset_close(as);
    }
    AAssetDir_close(ad);
  }
  setenv("TULPAR_ENGINE_ASSETS", assets, 1);
  std::printf("[android] varlik: %u dosya -> %s\n", extracted, assets);
  platform::CrashConfig cc;
  cc.report_dir = dir;
  cc.build_id = "engine_android";
  platform::crash_reporter_install(cc);

  char mode[PROP_VALUE_MAX], filter[PROP_VALUE_MAX], frames[PROP_VALUE_MAX];
  char present[PROP_VALUE_MAX], prerot[PROP_VALUE_MAX], size[PROP_VALUE_MAX], validation[PROP_VALUE_MAX];
  prop("debug.tulpar.mode", mode, sizeof mode, "demo");
  prop("debug.tulpar.filter", filter, sizeof filter, "");
  prop("debug.tulpar.frames", frames, sizeof frames, "600");
  prop("debug.tulpar.present", present, sizeof present, "fifo");
  prop("debug.tulpar.prerotate", prerot, sizeof prerot, "1");
  prop("debug.tulpar.size", size, sizeof size, "2159x1080");
  prop("debug.tulpar.validation", validation, sizeof validation, "0"); // 1: katman + Mali linter (APK'da katman varsa)
  char audio_p[PROP_VALUE_MAX], swappy_p[PROP_VALUE_MAX];
  prop("debug.tulpar.audio", audio_p, sizeof audio_p, "0"); // 1: 440 Hz ton (AAudio yolu + callback sayimi)
  prop("debug.tulpar.swappy", swappy_p, sizeof swappy_p, "0"); // 1: AGDK Swappy kare temposu (ENGINE_SWAPPY derlenmisse)
  char dtprobe_p[8];
  prop("debug.tulpar.dtprobe", dtprobe_p, sizeof dtprobe_p, "0"); // 1: VK_GOOGLE_display_timing sondasi (Swappy kapaliyken)
  std::printf("[android] kip=%s dizin=%s\n", mode, dir);

  int rc = 0;
  if (std::strcmp(mode, "headless") == 0) {
    // Vsync YOK, sunum yok: cihazin gercek kare maliyeti (offscreen render pass).
    app::DemoOptions o;
    o.headless_frames = (uint32_t)std::atoi(frames);
    unsigned w = 0, h = 0;
    if (std::sscanf(size, "%ux%u", &w, &h) == 2 && w && h) { o.width = w; o.height = h; }
    static char out[512];
    std::snprintf(out, sizeof out, "%s/headless.ppm", dir);
    o.out_path = out;
    o.validation = validation[0] == '1';
    rc = app::demo_run(o, nullptr);
    std::printf("[android] headless demo_run = %d\n", rc);
  } else if (std::strcmp(mode, "tests") == 0) {
    char *argv[2] = {nullptr, filter[0] ? filter : nullptr};
    rc = engine_tests_main(filter[0] ? 2 : 1, argv);
    std::printf("[android] engine_tests_main = %d\n", rc);
  } else {
    // Pencereyi bekle.
    while (!host.window_ready && !host.quit) pump_events(&host, 100);
    if (host.window_ready) {
      app::DemoOptions o;
      o.max_frames = (uint32_t)std::atoi(frames);
      o.present_mode = present;
      o.prerotate = prerot[0] != '0';
      o.validation = validation[0] == '1';
      o.audio = audio_p[0] == '1';
      static const char *const kDtExts[] = {"VK_GOOGLE_display_timing"};
      if (dtprobe_p[0] == '1' && swappy_p[0] != '1') {
        o.device_extensions = kDtExts;
        o.device_extension_count = 1;
        o.swap_hooks.user = &g_dt;
        o.swap_hooks.on_create = dt_on_create;
        o.swap_hooks.on_destroy = dt_on_destroy;
        o.swap_hooks.present = dt_present;
        std::printf("[android] dt sondasi: acik (VK_GOOGLE_display_timing, presentID)\n");
      }
#if ENGINE_SWAPPY
      if (swappy_p[0] == '1') {
        char fam_p[8];
        prop("debug.tulpar.swappy_family", fam_p, sizeof fam_p, "1");
        g_swappy.set_family = fam_p[0] != '0';
        g_swappy.app = app;
        // Kare istatistigi ve hassas zamanlama yalniz VK_GOOGLE_display_timing ile (varsa acilir).
        static const char *const kSwappyExts[] = {"VK_GOOGLE_display_timing"};
        o.device_extensions = kSwappyExts;
        o.device_extension_count = 1;
        o.swap_hooks.user = &g_swappy;
        o.swap_hooks.on_create = swappy_on_create;
        o.swap_hooks.on_destroy = swappy_on_destroy;
        o.swap_hooks.present = swappy_present;
        std::printf("[android] swappy: acik (games-frame-pacing)\n");
      }
#else
      if (swappy_p[0] == '1') std::printf("[android] swappy istendi ama ENGINE_SWAPPY derlenmedi (TULPAR_SWAPPY=ON)\n");
#endif
      app::DemoHost dh;
      dh.user = &host;
      dh.instance_extensions = android_exts;
      dh.create_surface = android_surface;
      dh.poll = android_poll;
      dh.touch = android_touch;
      rc = app::demo_run(o, &dh);
      std::printf("[android] demo_run = %d\n", rc);
      if (dtprobe_p[0] == '1' && swappy_p[0] != '1') dt_report();
#if ENGINE_SWAPPY
      if (swappy_p[0] == '1') swappy_report();
#endif
    }
  }
  std::printf("[android] bitti rc=%d\n", rc);
  ANativeActivity_finish(app->activity);
  while (!app->destroyRequested) pump_events(&host, 100);
}
#endif
