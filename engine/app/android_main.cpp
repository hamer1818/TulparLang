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

#if ENGINE_SWAPPY
// AGDK Swappy (kare temposu): swapchain yaratilinca baglam kurulur (JNI env bu
// thread'e baglanir; NativeActivity jobject = activity->clazz), sunum
// SwappyVk_queuePresent ile; sonda istatistik (gec kare / bekleme histogrami).
#include <jni.h>
#include <swappy/swappyVk.h>
struct SwappyCtx {
  android_app *app = nullptr;
  VkDevice device = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  uint64_t refresh_ns = 0;
  bool ok = false;
};
static SwappyCtx g_swappy;
static void swappy_on_create(void *user, VkPhysicalDevice phys, VkDevice dev, VkQueue, VkSwapchainKHR sc) {
  SwappyCtx *c = static_cast<SwappyCtx *>(user);
  JNIEnv *env = nullptr;
  if (c->app->activity->vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) { std::printf("[android] swappy: JNI env yok\n"); return; }
  uint64_t refresh = 0;
  c->ok = SwappyVk_initAndGetRefreshCycleDuration(env, c->app->activity->clazz, phys, dev, sc, &refresh);
  c->device = dev;
  c->swapchain = sc;
  c->refresh_ns = refresh;
  if (!c->ok) { std::printf("[android] swappy: init basarisiz\n"); return; }
  SwappyVk_setWindow(dev, sc, c->app->window);
  SwappyVk_setSwapIntervalNS(dev, sc, SWAPPY_SWAP_60FPS);
  SwappyVk_enableStats(sc, true);
  std::printf("[android] swappy: yenileme %.2f ms, hedef 60 fps, istatistik acik\n", refresh / 1e6);
}
static void swappy_on_destroy(void *user, VkDevice dev, VkSwapchainKHR sc) {
  SwappyCtx *c = static_cast<SwappyCtx *>(user);
  if (c->ok) SwappyVk_destroySwapchain(dev, sc);
  c->swapchain = VK_NULL_HANDLE;
}
static VkResult swappy_present(void *user, VkQueue q, const VkPresentInfoKHR *pi) {
  SwappyCtx *c = static_cast<SwappyCtx *>(user);
  if (!c->ok) return VK_ERROR_DEVICE_LOST;
  SwappyVk_recordFrameStart(q, c->swapchain, pi->pImageIndices ? pi->pImageIndices[0] : 0);
  return SwappyVk_queuePresent(q, pi);
}
static void swappy_report() {
  SwappyCtx *c = &g_swappy;
  if (!c->ok) return;
  SwappyStats st{};
  if (c->swapchain) SwappyVk_getStats(c->swapchain, &st);
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
                        !std::strcmp(dot, ".ttf");
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
#if ENGINE_SWAPPY
      if (swappy_p[0] == '1') {
        g_swappy.app = app;
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
