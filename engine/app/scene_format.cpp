#include "app/scene_format.hpp"

#include <cstdio>
#include <cstring>

namespace tulpar::engine::app {

namespace {
void set_error(SceneIoResult &r, const char *msg) {
  r.ok = false;
  std::snprintf(r.error, sizeof r.error, "%s", msg);
}
// Bastaki bosluk/tab'i atlar.
const char *skip_ws(const char *s) {
  while (*s == ' ' || *s == '\t') s++;
  return s;
}
} // namespace

SceneIoResult scene_save(const char *path, const EditorEntity *ents, int count) {
  SceneIoResult r;
  std::FILE *f = std::fopen(path, "wb");
  if (!f) { set_error(r, "dosya acilamadi (yazma)"); return r; }
  std::fprintf(f, "# tulpar sahne v1\n");
  for (int i = 0; i < count; i++) {
    const EditorEntity &e = ents[i];
    std::fprintf(f, "entity %s\n", e.name);
    std::fprintf(f, "  pos %.6f %.6f %.6f\n", e.pos[0], e.pos[1], e.pos[2]);
    std::fprintf(f, "  rot %.6f %.6f %.6f\n", e.rot_deg[0], e.rot_deg[1], e.rot_deg[2]);
    std::fprintf(f, "  scale %.6f %.6f %.6f\n", e.scale[0], e.scale[1], e.scale[2]);
    std::fprintf(f, "  kind %d\n", e.kind);
    std::fprintf(f, "  phase %.6f\n", e.phase);
  }
  if (std::fclose(f) != 0) { set_error(r, "dosya kapatilamadi (disk dolu olabilir)"); return r; }
  r.ok = true;
  return r;
}

SceneIoResult scene_load(const char *path, EditorEntity *ents, int max_count, int *out_count) {
  SceneIoResult r;
  std::FILE *f = std::fopen(path, "rb");
  if (!f) { set_error(r, "dosya acilamadi (okuma)"); return r; }
  int n = 0;
  bool have_entity = false;
  char line[256];
  while (std::fgets(line, sizeof line, f)) {
    const char *s = skip_ws(line);
    if (*s == '#' || *s == '\n' || *s == '\0') continue;
    char name[32];
    if (std::sscanf(s, "entity %31[^\r\n]", name) == 1) {
      if (n >= max_count) { std::fclose(f); set_error(r, "cok fazla entity (max_count asildi)"); return r; }
      EditorEntity &e = ents[n];
      std::memset(&e, 0, sizeof e);
      std::snprintf(e.name, sizeof e.name, "%s", name);
      e.scale[0] = e.scale[1] = e.scale[2] = 1.0f;
      n++;
      have_entity = true;
      continue;
    }
    if (!have_entity) continue; // ilk "entity" satirindan once gelen her sey yok sayilir
    EditorEntity &e = ents[n - 1];
    float a, b, c;
    int k;
    if (std::sscanf(s, "pos %f %f %f", &a, &b, &c) == 3) { e.pos[0] = a; e.pos[1] = b; e.pos[2] = c; }
    else if (std::sscanf(s, "rot %f %f %f", &a, &b, &c) == 3) { e.rot_deg[0] = a; e.rot_deg[1] = b; e.rot_deg[2] = c; }
    else if (std::sscanf(s, "scale %f %f %f", &a, &b, &c) == 3) { e.scale[0] = a; e.scale[1] = b; e.scale[2] = c; }
    else if (std::sscanf(s, "kind %d", &k) == 1) { e.kind = k; }
    else if (std::sscanf(s, "phase %f", &a) == 1) { e.phase = a; }
    // Taninmayan satir: sessizce atla (ileri-uyumluluk).
  }
  std::fclose(f);
  *out_count = n;
  r.ok = true;
  return r;
}

} // namespace tulpar::engine::app
