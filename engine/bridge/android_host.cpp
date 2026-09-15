// L6 BRIDGE — Android NativeActivity host: Tulpar oyununun (libtulpargame.so)
// `main`i bu android_main'den cagrilir (raylib'in rcore_android'i gibi).
// stdout/stderr -> boru -> logcat (etiket "tulpar") + files/engine_log.txt;
// APK assets/ -> dahili dizin (cgltf fopen ister), TULPAR_ENGINE_ASSETS.
// Kopru cekirdegi teng_init'te bridge_host_open cagirir: pencere gelene kadar
// olaylar pompalanir, sonra yuzey/poll/dokunmatik geri cagrilari verilir.
#if defined(__ANDROID__)
#define VK_USE_PLATFORM_ANDROID_KHR 1
#include <android/asset_manager.h>
#include <android/input.h>
#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "bridge/bridge_host.hpp"
#include "platform/crash.hpp"
#include "platform/time.hpp"

// Tulpar tarafindan uretilen oyun giris noktasi (libtulpargame.so icinde).
extern "C" int main(int argc, char **argv);

using namespace tulpar::engine;

namespace {
struct LogPump { int fd = -1; FILE *file = nullptr; };
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

struct AndroidHost {
  android_app *app = nullptr;
  platform::TouchState touch;
  bool window_ready = false, had_window = false, window_changed = false, quit = false;
};
AndroidHost g_host;

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
  std::printf("[engine_bridge] android cmd %s\n", cmd_name(cmd));
  switch (cmd) {
  case APP_CMD_INIT_WINDOW:
    h->window_ready = app->window != nullptr;
    if (h->window_ready && h->had_window) h->window_changed = true;
    break;
  case APP_CMD_TERM_WINDOW: h->window_ready = false; break;
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
  case AMOTION_EVENT_ACTION_CANCEL: t.clear(); break;
  default: break;
  }
  return 1;
}
// --- APK varlik cikarimi (alt dizinler dahil) ------------------------------
struct AssetWalk {
  AAssetManager *mgr = nullptr;
  const char *dest_root = nullptr;
  JavaVM *vm = nullptr;
  JNIEnv *env = nullptr;
  jobject asset_mgr_obj = nullptr;
  jmethodID list_mid = nullptr;
  uint32_t files = 0, dirs = 0;
};
// Uzanti suzgeci: Huawei'de kok listesi sistem kaplamalarini da getiriyor.
bool asset_wanted(const char *name) {
  const char *dot = std::strrchr(name, '.');
  if (!dot) return false;
  static const char *kExt[] = {".gltf", ".glb", ".bin", ".png", ".jpg", ".jpeg", ".ktx2", ".ttf",
                               ".sahne", ".sahneb", ".wav", ".ogg", ".txt", ".json", ".csv"};
  for (const char *e : kExt)
    if (std::strcmp(dot, e) == 0) return true;
  return false;
}
bool write_asset(AssetWalk *w, const char *rel) {
  AAsset *as = AAssetManager_open(w->mgr, rel, AASSET_MODE_BUFFER);
  if (!as) return false;
  char path[1024];
  std::snprintf(path, sizeof path, "%s/%s", w->dest_root, rel);
  bool ok = false;
  if (FILE *f = std::fopen(path, "wb")) {
    const off_t n = AAsset_getLength(as);
    ok = std::fwrite(AAsset_getBuffer(as), 1, (size_t)n, f) == (size_t)n;
    std::fclose(f);
    if (ok) w->files++;
  }
  AAsset_close(as);
  return ok;
}
bool jni_assets_begin(android_app *app, AssetWalk *w) {
  w->vm = app->activity->vm;
  if (!w->vm || w->vm->AttachCurrentThread(&w->env, nullptr) != JNI_OK || !w->env) return false;
  jobject act = app->activity->clazz;
  jclass act_cls = w->env->GetObjectClass(act);
  jmethodID get_assets = act_cls ? w->env->GetMethodID(act_cls, "getAssets", "()Landroid/content/res/AssetManager;") : nullptr;
  if (!get_assets) { w->env->ExceptionClear(); return false; }
  jobject am = w->env->CallObjectMethod(act, get_assets);
  if (!am) { w->env->ExceptionClear(); return false; }
  jclass am_cls = w->env->GetObjectClass(am);
  w->list_mid = am_cls ? w->env->GetMethodID(am_cls, "list", "(Ljava/lang/String;)[Ljava/lang/String;") : nullptr;
  if (!w->list_mid) { w->env->ExceptionClear(); return false; }
  w->asset_mgr_obj = w->env->NewGlobalRef(am);
  return w->asset_mgr_obj != nullptr;
}
void jni_assets_end(AssetWalk *w) {
  if (w->env && w->asset_mgr_obj) w->env->DeleteGlobalRef(w->asset_mgr_obj);
  if (w->vm) w->vm->DetachCurrentThread();
  w->env = nullptr;
}
// `rel` altindaki girdileri gezer: listesi bos olmayan = dizin (ozyinele),
// digerleri dosya (suzgecten geciyorsa cikar). Derinlik siniri: sonsuz dongu yok.
void extract_dir(AssetWalk *w, const char *rel, int depth) {
  if (depth > 6) return;
  jstring jrel = w->env->NewStringUTF(rel);
  jobjectArray arr = (jobjectArray)w->env->CallObjectMethod(w->asset_mgr_obj, w->list_mid, jrel);
  w->env->DeleteLocalRef(jrel);
  if (w->env->ExceptionCheck()) { w->env->ExceptionClear(); return; }
  if (!arr) return;
  const jsize n = w->env->GetArrayLength(arr);
  for (jsize i = 0; i < n; i++) {
    jstring js = (jstring)w->env->GetObjectArrayElement(arr, i);
    if (!js) continue;
    const char *name = w->env->GetStringUTFChars(js, nullptr);
    char child[1024];
    if (rel[0]) std::snprintf(child, sizeof child, "%s/%s", rel, name);
    else std::snprintf(child, sizeof child, "%s", name);
    // Dizin mi? list() bos olmayan dizi donduruyorsa evet.
    jstring jchild = w->env->NewStringUTF(child);
    jobjectArray sub = (jobjectArray)w->env->CallObjectMethod(w->asset_mgr_obj, w->list_mid, jchild);
    w->env->DeleteLocalRef(jchild);
    if (w->env->ExceptionCheck()) w->env->ExceptionClear();
    const bool is_dir = sub && w->env->GetArrayLength(sub) > 0;
    if (sub) w->env->DeleteLocalRef(sub);
    if (is_dir) {
      char dpath[1024];
      std::snprintf(dpath, sizeof dpath, "%s/%s", w->dest_root, child);
      mkdir(dpath, 0700);
      w->dirs++;
      extract_dir(w, child, depth + 1);
    } else if (asset_wanted(child)) {
      write_asset(w, child);
    }
    w->env->ReleaseStringUTFChars(js, name);
    w->env->DeleteLocalRef(js);
  }
  w->env->DeleteLocalRef(arr);
}
// JNI yoksa (olmamali): yalniz kok dizindeki dosyalar.
void extract_flat(AssetWalk *w) {
  if (AAssetDir *ad = AAssetManager_openDir(w->mgr, "")) {
    while (const char *name = AAssetDir_getNextFileName(ad))
      if (asset_wanted(name)) write_asset(w, name);
    AAssetDir_close(ad);
  }
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
const char *const *a_exts(void *, uint32_t *n) {
  static const char *exts[] = {"VK_KHR_surface", "VK_KHR_android_surface"};
  *n = 2;
  return exts;
}
bool a_surface(void *user, rhi::VkApi &api, VkInstance inst, VkSurfaceKHR *out) {
  AndroidHost *h = static_cast<AndroidHost *>(user);
  auto fn = (PFN_vkCreateAndroidSurfaceKHR)api.vkGetInstanceProcAddr(inst, "vkCreateAndroidSurfaceKHR");
  if (!fn) { std::printf("[engine_bridge] android: vkCreateAndroidSurfaceKHR yok\n"); return false; }
  VkAndroidSurfaceCreateInfoKHR ci{};
  ci.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
  ci.window = h->app->window;
  const VkResult r = fn(inst, &ci, nullptr, out);
  if (r != VK_SUCCESS) std::printf("[engine_bridge] android: vkCreateAndroidSurfaceKHR = %d\n", (int)r);
  else h->had_window = true;
  return r == VK_SUCCESS;
}
bridge::HostPoll a_poll(void *user, uint32_t *w, uint32_t *h_) {
  AndroidHost *h = static_cast<AndroidHost *>(user);
  pump_events(h, h->window_ready ? 0 : 100);
  if (h->quit) return bridge::HostPoll::Quit;
  if (!h->window_ready) return bridge::HostPoll::NoWindow;
  *w = (uint32_t)ANativeWindow_getWidth(h->app->window);
  *h_ = (uint32_t)ANativeWindow_getHeight(h->app->window);
  if (h->window_changed) { h->window_changed = false; return bridge::HostPoll::WindowChanged; }
  return bridge::HostPoll::Run;
}
const platform::TouchState *a_touch(void *user) {
  AndroidHost *h = static_cast<AndroidHost *>(user);
  if (h->app->window) {
    h->touch.width = (float)ANativeWindow_getWidth(h->app->window);
    h->touch.height = (float)ANativeWindow_getHeight(h->app->window);
  }
  return &h->touch;
}
} // namespace

namespace tulpar::engine::bridge {
bool bridge_host_open(BridgeHost *out, const char *title, uint32_t w, uint32_t h, char *err, size_t err_cap) {
  (void)title; (void)w; (void)h;
  if (!g_host.app) { std::snprintf(err, err_cap, "android_app yok (android_main calismadi)"); return false; }
  std::printf("[engine_bridge] android host: pencere bekleniyor\n");
  uint32_t waited = 0;
  while (!g_host.window_ready && !g_host.quit && waited < 100) { pump_events(&g_host, 100); waited++; }
  if (!g_host.window_ready) { std::snprintf(err, err_cap, "ANativeWindow gelmedi (%s)", g_host.quit ? "cikis istendi" : "10 s zaman asimi"); return false; }
  out->user = &g_host;
  out->instance_extensions = a_exts;
  out->create_surface = a_surface;
  out->poll = a_poll;
  out->touch = a_touch;
  out->input = nullptr;
  out->close = nullptr;
  std::printf("[engine_bridge] android host: pencere %dx%d\n", ANativeWindow_getWidth(g_host.app->window), ANativeWindow_getHeight(g_host.app->window));
  return true;
}
void bridge_host_close(BridgeHost *) { std::printf("[engine_bridge] android host: kapatildi\n"); }
} // namespace tulpar::engine::bridge

extern "C" void android_main(android_app *app) {
  g_host = AndroidHost{};
  g_host.app = app;
  app->userData = &g_host;
  app->onAppCmd = on_cmd;
  app->onInputEvent = on_input;
  const char *dir = app->activity->externalDataPath ? app->activity->externalDataPath : app->activity->internalDataPath;
  redirect_output(dir);
  setenv("TMPDIR", dir, 1);
  static char assets[512];
  std::snprintf(assets, sizeof assets, "%s/assets", app->activity->internalDataPath);
  mkdir(assets, 0700);
  // Varliklari APK'dan DAHILI dizine cikar (cgltf/stb fopen ister). ALT DIZINLER
  // dahil: `tulpar build --target=android` varlik dizinini KAYNAK YOLUYLA montaj
  // ediyor (TULPAR_ANDROID_ASSETS=examples/assets -> APK assets/examples/assets/),
  // oysa AAssetManager_openDir yalniz VERILEN dizindeki DOSYALARI listeler, alt
  // dizin adlarini vermez. Dizin adlarini Java tarafindaki AssetManager.list()
  // veriyor; asagidaki ozyineleme onu kullanir.
  AssetWalk walk;
  walk.mgr = app->activity->assetManager;
  walk.dest_root = assets;
  if (jni_assets_begin(app, &walk)) {
    extract_dir(&walk, "", 0);
    jni_assets_end(&walk);
  } else {
    std::printf("[engine_bridge] android: JNI yok, yalniz kok dizin cikarilir\n");
    extract_flat(&walk);
  }
  setenv("TULPAR_ENGINE_ASSETS", assets, 1);
  // Oyun dosya yollarini "examples/assets/x" gibi GORECELI yazar: calisma dizini
  // cikarma kokunun ustu olsun ki ayni yol masaustunde de cihazda da tutsun.
  if (chdir(assets) != 0) std::printf("[engine_bridge] android: chdir basarisiz\n");
  std::printf("[engine_bridge] android: varlik %u dosya, %u dizin -> %s (cwd), cikti dizini %s\n", walk.files, walk.dirs, assets, dir);
  platform::CrashConfig cc;
  cc.report_dir = dir;
  cc.build_id = "tulpar_engine_android";
  platform::crash_reporter_install(cc);
  char lv[PROP_VALUE_MAX] = {0};
  if (__system_property_get("debug.tulpar.log", lv) > 0) setenv("TULPAR_ENGINE_LOG", lv, 1);

  // Oyunun giris noktasi: Tulpar objesinin `main`i (raylib'in rcore_android'i
  // ile ayni desen). Dogrudan bildirim -> sembol yoksa LINK'te patlar
  // (-Wl,--no-undefined), cihazda sessiz "hicbir sey olmadi" yerine.
  std::printf("[engine_bridge] android: oyun main() basliyor\n");
  static char arg0[] = "tulpargame";
  char *argv[2] = {arg0, nullptr};
  const int rc = main(1, argv);
  std::printf("[engine_bridge] android: oyun main() = %d\n", rc);
  std::printf("[engine_bridge] android: bitti rc=%d\n", rc);
  ANativeActivity_finish(app->activity);
  while (!app->destroyRequested) pump_events(&g_host, 100);
}
#endif
