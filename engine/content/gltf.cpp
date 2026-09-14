#include "content/gltf.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <cgltf.h>    // govdeler: content/vendored_impl.c
#define STBI_NO_STDIO
#include <stb_image.h>

namespace tulpar::engine::content {

namespace {
void set_error(Model *m, const char *what, const char *detail) {
  std::snprintf(m->error, sizeof m->error, "%s%s%s", what, detail ? ": " : "", detail ? detail : "");
}

// Dosyayi tumuyle oku (dis .bin ve dis goruntu icin). malloc: yukleme ani.
unsigned char *read_file(const char *path, size_t *size) {
  FILE *f = std::fopen(path, "rb");
  if (!f) return nullptr;
  std::fseek(f, 0, SEEK_END);
  long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (n < 0) { std::fclose(f); return nullptr; }
  unsigned char *buf = static_cast<unsigned char *>(std::malloc((size_t)n));
  if (!buf) { std::fclose(f); return nullptr; }
  size_t got = std::fread(buf, 1, (size_t)n, f);
  std::fclose(f);
  if (got != (size_t)n) { std::free(buf); return nullptr; }
  *size = got;
  return buf;
}

// "data:...;base64,XXXX" -> cozulmus bayt (cgltf'nin base64'u)
unsigned char *decode_data_uri(const char *uri, size_t *size) {
  const char *comma = std::strchr(uri, ',');
  if (!comma || std::strncmp(uri, "data:", 5) != 0) return nullptr;
  const char *b64 = comma + 1;
  size_t len = std::strlen(b64);
  size_t decoded = len / 4 * 3;
  if (len >= 1 && b64[len - 1] == '=') decoded--;
  if (len >= 2 && b64[len - 2] == '=') decoded--;
  cgltf_options opt{};
  void *out = nullptr;
  if (cgltf_load_buffer_base64(&opt, decoded, b64, &out) != cgltf_result_success) return nullptr;
  *size = decoded;
  return static_cast<unsigned char *>(out);
}

// Goruntuyu coz: data URI, dis dosya (gltf dizinine gore) ya da bufferView (GLB).
bool load_image(const cgltf_image *img, const char *gltf_dir, Arena &arena, ModelImage *out) {
  unsigned char *encoded = nullptr;
  size_t enc_size = 0;
  bool owned = false;
  if (img->buffer_view && img->buffer_view->buffer && img->buffer_view->buffer->data) {
    encoded = static_cast<unsigned char *>(img->buffer_view->buffer->data) + img->buffer_view->offset;
    enc_size = img->buffer_view->size;
  } else if (img->uri && std::strncmp(img->uri, "data:", 5) == 0) {
    encoded = decode_data_uri(img->uri, &enc_size);
    owned = encoded != nullptr;
  } else if (img->uri) {
    char path[1024];
    std::snprintf(path, sizeof path, "%s%s", gltf_dir, img->uri);
    encoded = read_file(path, &enc_size);
    owned = encoded != nullptr;
  }
  if (!encoded) return false;
  int w = 0, h = 0, comp = 0;
  unsigned char *px = stbi_load_from_memory(encoded, (int)enc_size, &w, &h, &comp, 4);
  if (owned) std::free(encoded);
  if (!px || w <= 0 || h <= 0) return false;
  out->width = (uint32_t)w;
  out->height = (uint32_t)h;
  out->rgba = arena.alloc_array<uint8_t>((size_t)w * h * 4);
  if (out->rgba) std::memcpy(out->rgba, px, (size_t)w * h * 4);
  stbi_image_free(px);
  return out->rgba != nullptr;
}

const cgltf_accessor *find_attr(const cgltf_primitive &p, cgltf_attribute_type t) {
  for (cgltf_size i = 0; i < p.attributes_count; i++)
    if (p.attributes[i].type == t) return p.attributes[i].data;
  return nullptr;
}

uint32_t count_mesh_primitives(const cgltf_data *d) {
  uint32_t n = 0;
  for (cgltf_size m = 0; m < d->meshes_count; m++)
    for (cgltf_size p = 0; p < d->meshes[m].primitives_count; p++)
      if (d->meshes[m].primitives[p].type == cgltf_primitive_type_triangles) n++;
  return n;
}

void extend_bounds(Model *out, const Mat4 &world, const renderer::Vertex *v, uint32_t n, bool *first) {
  for (uint32_t i = 0; i < n; i++) {
    Vec4 p = world * Vec4{v[i].pos.x, v[i].pos.y, v[i].pos.z, 1.0f};
    Vec3 q{p.x, p.y, p.z};
    if (*first) { out->bounds_min = out->bounds_max = q; *first = false; continue; }
    out->bounds_min = {q.x < out->bounds_min.x ? q.x : out->bounds_min.x, q.y < out->bounds_min.y ? q.y : out->bounds_min.y,
                       q.z < out->bounds_min.z ? q.z : out->bounds_min.z};
    out->bounds_max = {q.x > out->bounds_max.x ? q.x : out->bounds_max.x, q.y > out->bounds_max.y ? q.y : out->bounds_max.y,
                       q.z > out->bounds_max.z ? q.z : out->bounds_max.z};
  }
}
} // namespace

bool gltf_load(Arena &arena, const char *path, Model *out, const GltfLimits &lim) {
  *out = Model{};
  cgltf_options opt{};
  cgltf_data *data = nullptr;
  cgltf_result r = cgltf_parse_file(&opt, path, &data);
  if (r != cgltf_result_success) { set_error(out, "glTF ayristirma", path); return false; }
  r = cgltf_load_buffers(&opt, data, path);
  if (r != cgltf_result_success) { cgltf_free(data); set_error(out, "glTF tampon yukleme", path); return false; }

  // gltf dizini (dis goruntu yollari icin)
  char dir[1024];
  std::snprintf(dir, sizeof dir, "%s", path);
  char *slash = std::strrchr(dir, '/');
  if (slash) slash[1] = 0; else dir[0] = 0;

  bool ok = true;
  // Goruntuler
  uint32_t nimg = (uint32_t)data->images_count;
  if (nimg > lim.max_images) { set_error(out, "goruntu siniri asildi", nullptr); ok = false; }
  out->images = arena.alloc_array_zeroed<ModelImage>(nimg ? nimg : 1);
  for (uint32_t i = 0; ok && i < nimg; i++) {
    if (!load_image(&data->images[i], dir, arena, &out->images[i])) {
      set_error(out, "goruntu cozulemedi", data->images[i].uri && std::strncmp(data->images[i].uri, "data:", 5) ? data->images[i].uri : "(gomulu)");
      ok = false;
    }
  }
  out->image_count = ok ? nimg : 0;
  // Malzemeler
  uint32_t nmat = (uint32_t)data->materials_count;
  if (ok && nmat > lim.max_materials) { set_error(out, "malzeme siniri asildi", nullptr); ok = false; }
  out->materials = arena.alloc_array_zeroed<ModelMaterial>(nmat ? nmat : 1);
  for (uint32_t i = 0; ok && i < nmat; i++) {
    const cgltf_material &cm = data->materials[i];
    ModelMaterial &mm = out->materials[i];
    mm.base_color = {1, 1, 1};
    mm.image = -1;
    if (cm.has_pbr_metallic_roughness) {
      const cgltf_pbr_metallic_roughness &pbr = cm.pbr_metallic_roughness;
      mm.base_color = {pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2]};
      if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image)
        mm.image = (int32_t)(pbr.base_color_texture.texture->image - data->images);
    }
  }
  out->material_count = ok ? nmat : 0;
  // Mesh'ler (ucgen primitifleri; her primitif ayri ModelMesh)
  uint32_t nprim = count_mesh_primitives(data);
  if (ok && nprim > lim.max_meshes) { set_error(out, "mesh siniri asildi", nullptr); ok = false; }
  out->meshes = arena.alloc_array_zeroed<ModelMesh>(nprim ? nprim : 1);
  // cgltf mesh -> ilk ModelMesh indeksi ve sayisi (instance kurarken)
  uint32_t *mesh_first = arena.alloc_array_zeroed<uint32_t>(data->meshes_count ? data->meshes_count : 1);
  uint32_t *mesh_n = arena.alloc_array_zeroed<uint32_t>(data->meshes_count ? data->meshes_count : 1);
  uint32_t mi = 0;
  for (cgltf_size m = 0; ok && m < data->meshes_count; m++) {
    mesh_first[m] = mi;
    for (cgltf_size p = 0; p < data->meshes[m].primitives_count; p++) {
      const cgltf_primitive &prim = data->meshes[m].primitives[p];
      if (prim.type != cgltf_primitive_type_triangles) continue;
      const cgltf_accessor *pos = find_attr(prim, cgltf_attribute_type_position);
      const cgltf_accessor *nrm = find_attr(prim, cgltf_attribute_type_normal);
      const cgltf_accessor *uv = find_attr(prim, cgltf_attribute_type_texcoord);
      if (!pos) { set_error(out, "POSITION yok", data->meshes[m].name); ok = false; break; }
      ModelMesh &mm = out->meshes[mi];
      mm.vertex_count = (uint32_t)pos->count;
      mm.verts = arena.alloc_array<renderer::Vertex>(mm.vertex_count);
      if (!mm.verts) { set_error(out, "arena dolu (vertex)", nullptr); ok = false; break; }
      for (uint32_t i = 0; i < mm.vertex_count; i++) {
        float f[3] = {0, 0, 0};
        cgltf_accessor_read_float(pos, i, f, 3);
        mm.verts[i].pos = {f[0], f[1], f[2]};
        f[0] = 0; f[1] = 1; f[2] = 0;
        if (nrm) cgltf_accessor_read_float(nrm, i, f, 3);
        mm.verts[i].nrm = {f[0], f[1], f[2]};
        f[0] = f[1] = 0;
        if (uv) cgltf_accessor_read_float(uv, i, f, 2);
        mm.verts[i].uv = {f[0], f[1]};
      }
      if (prim.indices) {
        mm.index_count = (uint32_t)prim.indices->count;
        mm.indices = arena.alloc_array<uint32_t>(mm.index_count);
        if (!mm.indices) { set_error(out, "arena dolu (indeks)", nullptr); ok = false; break; }
        for (uint32_t i = 0; i < mm.index_count; i++) mm.indices[i] = (uint32_t)cgltf_accessor_read_index(prim.indices, i);
      } else {
        mm.index_count = mm.vertex_count;
        mm.indices = arena.alloc_array<uint32_t>(mm.index_count);
        if (!mm.indices) { set_error(out, "arena dolu (indeks)", nullptr); ok = false; break; }
        for (uint32_t i = 0; i < mm.index_count; i++) mm.indices[i] = i;
      }
      mm.material = prim.material ? (int32_t)(prim.material - data->materials) : -1;
      mi++;
      mesh_n[m]++;
    }
  }
  out->mesh_count = ok ? mi : 0;
  // Instance'lar: sahnedeki (ya da tum) dugumler, dunya matrisiyle
  uint32_t ninst = 0;
  out->instances = arena.alloc_array_zeroed<ModelInstance>(lim.max_instances);
  bool first_bound = true;
  for (cgltf_size n = 0; ok && n < data->nodes_count; n++) {
    const cgltf_node &node = data->nodes[n];
    if (!node.mesh) continue;
    cgltf_size m = (cgltf_size)(node.mesh - data->meshes);
    float wm[16];
    cgltf_node_transform_world(&node, wm);
    Mat4 world;
    for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) world.m[c][rr] = wm[c * 4 + rr]; // glTF sutun-major
    for (uint32_t k = 0; k < mesh_n[m]; k++) {
      if (ninst >= lim.max_instances) { set_error(out, "instance siniri asildi", nullptr); ok = false; break; }
      out->instances[ninst].mesh = mesh_first[m] + k;
      out->instances[ninst].world = world;
      extend_bounds(out, world, out->meshes[mesh_first[m] + k].verts, out->meshes[mesh_first[m] + k].vertex_count, &first_bound);
      ninst++;
    }
  }
  out->instance_count = ok ? ninst : 0;
  cgltf_free(data);
  return ok;
}

bool upload_model(renderer::Renderer &r, Arena &arena, const Model &m, UploadedModel *out) {
  *out = UploadedModel{};
  out->textures = arena.alloc_array_zeroed<renderer::TextureHandle>(m.image_count ? m.image_count : 1);
  out->materials = arena.alloc_array_zeroed<renderer::MaterialHandle>(m.material_count ? m.material_count : 1);
  out->meshes = arena.alloc_array_zeroed<renderer::MeshHandle>(m.mesh_count ? m.mesh_count : 1);
  if (!out->textures || !out->materials || !out->meshes) return false;
  for (uint32_t i = 0; i < m.image_count; i++) {
    out->textures[i] = r.create_texture(m.images[i].rgba, m.images[i].width, m.images[i].height, true);
    if (!out->textures[i].valid()) return false;
  }
  out->texture_count = m.image_count;
  for (uint32_t i = 0; i < m.material_count; i++) {
    renderer::TextureHandle t = m.materials[i].image >= 0 ? out->textures[m.materials[i].image] : r.default_texture();
    out->materials[i] = r.create_material(t, m.materials[i].base_color);
    if (!out->materials[i].valid()) return false;
  }
  out->material_count = m.material_count;
  for (uint32_t i = 0; i < m.mesh_count; i++) {
    out->meshes[i] = r.create_mesh(m.meshes[i].verts, m.meshes[i].vertex_count, m.meshes[i].indices, m.meshes[i].index_count);
    if (!out->meshes[i].valid()) return false;
  }
  out->mesh_count = m.mesh_count;
  return true;
}

void draw_model(renderer::Renderer &r, const Model &m, const UploadedModel &u, const Mat4 &model, Vec3 tint) {
  for (uint32_t i = 0; i < m.instance_count; i++) {
    const ModelInstance &inst = m.instances[i];
    if (inst.mesh >= u.mesh_count) continue;
    int32_t mat = m.meshes[inst.mesh].material;
    renderer::MaterialHandle mh = (mat >= 0 && (uint32_t)mat < u.material_count) ? u.materials[mat] : r.default_material();
    r.draw(u.meshes[inst.mesh], mh, model * inst.world, tint);
  }
}

} // namespace tulpar::engine::content
