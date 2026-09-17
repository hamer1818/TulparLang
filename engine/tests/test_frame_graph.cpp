// rhi/frame_graph.hpp: kucuk ama GERCEKCI bir mobil render zinciri
// (depth-prepass -> gbuffer -> lighting -> bloom -> tonemap, arti
// KULLANILMAYAN bir hata-ayiklama gecisi) uzerinde ELLE iz surulerek
// budama, topolojik sira ve kaynak yasam araliklari kanitlanir; ayrica
// dongu tespiti ve coklu-yazim reddi ayri testlerle dogrulanir.
#include "rhi/frame_graph.hpp"

#include "tests/test.hpp"

using namespace tulpar::engine::rhi;

ENGINE_TEST(frame_graph_culls_unused_pass_and_orders_topologically) {
  FrameGraph fg;
  const FgResourceId r_depth = fg.create_resource("depth");
  const FgResourceId r_albedo = fg.create_resource("albedo");
  const FgResourceId r_normal = fg.create_resource("normal");
  const FgResourceId r_hdr = fg.create_resource("hdr");
  const FgResourceId r_bloom = fg.create_resource("bloom");
  const FgResourceId r_final = fg.create_resource("final");
  const FgResourceId r_debug = fg.create_resource("debug");

  const FgPassId depth_prepass = fg.add_pass("depth_prepass");
  const FgPassId gbuffer = fg.add_pass("gbuffer");
  const FgPassId lighting = fg.add_pass("lighting");
  const FgPassId bloom = fg.add_pass("bloom_threshold");
  const FgPassId tonemap = fg.add_pass("tonemap", /*is_output=*/true);
  const FgPassId unused_debug = fg.add_pass("unused_debug_pass");

  CHECK(fg.pass_writes(depth_prepass, r_depth));

  CHECK(fg.pass_reads(gbuffer, r_depth));
  CHECK(fg.pass_writes(gbuffer, r_albedo));
  CHECK(fg.pass_writes(gbuffer, r_normal));

  CHECK(fg.pass_reads(lighting, r_depth));
  CHECK(fg.pass_reads(lighting, r_albedo));
  CHECK(fg.pass_reads(lighting, r_normal));
  CHECK(fg.pass_writes(lighting, r_hdr));

  CHECK(fg.pass_reads(bloom, r_hdr));
  CHECK(fg.pass_writes(bloom, r_bloom));

  CHECK(fg.pass_reads(tonemap, r_hdr));
  CHECK(fg.pass_reads(tonemap, r_bloom));
  CHECK(fg.pass_writes(tonemap, r_final));

  // hicbir canli pass'in okumadigi bir kaynak yazan, is_output OLMAYAN bir
  // pass -- BUDANMASI gereken tam durum.
  CHECK(fg.pass_reads(unused_debug, r_depth));
  CHECK(fg.pass_writes(unused_debug, r_debug));

  CHECK(fg.compile());

  CHECK(fg.was_culled(unused_debug));
  CHECK(!fg.was_culled(depth_prepass));
  CHECK(!fg.was_culled(gbuffer));
  CHECK(!fg.was_culled(lighting));
  CHECK(!fg.was_culled(bloom));
  CHECK(!fg.was_culled(tonemap));

  // budanan pass CALISMA SIRASINDA hic gorunmemeli.
  CHECK(fg.executed_count() == 5);
  CHECK(fg.executed_pass(0) == depth_prepass);
  CHECK(fg.executed_pass(1) == gbuffer);
  CHECK(fg.executed_pass(2) == lighting);
  CHECK(fg.executed_pass(3) == bloom);
  CHECK(fg.executed_pass(4) == tonemap);

  uint32_t start = 0, end = 0;
  CHECK(fg.resource_lifetime(r_depth, &start, &end));
  CHECK(start == 0 && end == 2); // depth_prepass'ta dogar, lighting'e kadar okunur

  CHECK(fg.resource_lifetime(r_albedo, &start, &end));
  CHECK(start == 1 && end == 2);
  CHECK(fg.resource_lifetime(r_normal, &start, &end));
  CHECK(start == 1 && end == 2);

  CHECK(fg.resource_lifetime(r_hdr, &start, &end));
  CHECK(start == 2 && end == 4); // lighting'te dogar, tonemap'e kadar yasar

  CHECK(fg.resource_lifetime(r_bloom, &start, &end));
  CHECK(start == 3 && end == 4);

  CHECK(fg.resource_lifetime(r_final, &start, &end));
  CHECK(start == 4 && end == 4); // hic okunmuyor -- dogumla ayni anda "olur" (grafik disina cikar)

  // uretici pass BUDANDIGI icin kaynak hic uretilmiyor -- bellek ayirmaya GEREK YOK.
  CHECK(!fg.resource_lifetime(r_debug, &start, &end));
}

ENGINE_TEST(frame_graph_compile_rejects_cyclic_dependency) {
  FrameGraph fg;
  const FgResourceId r_x = fg.create_resource("x");
  const FgResourceId r_y = fg.create_resource("y");

  const FgPassId pass_a = fg.add_pass("a"); // r_x okur, r_y yazar
  const FgPassId pass_b = fg.add_pass("b"); // r_y okur, r_x yazar -- DONGU
  const FgPassId pass_c = fg.add_pass("c", /*is_output=*/true); // r_y okur (a/b'yi CANLI yapar)

  CHECK(fg.pass_writes(pass_a, r_y));
  CHECK(fg.pass_reads(pass_a, r_x));
  CHECK(fg.pass_writes(pass_b, r_x));
  CHECK(fg.pass_reads(pass_b, r_y));
  CHECK(fg.pass_reads(pass_c, r_y));

  CHECK(!fg.compile());
}

ENGINE_TEST(frame_graph_rejects_double_write_to_same_resource) {
  FrameGraph fg;
  const FgResourceId r = fg.create_resource("r");
  const FgPassId p1 = fg.add_pass("p1");
  const FgPassId p2 = fg.add_pass("p2");

  CHECK(fg.pass_writes(p1, r));
  CHECK(!fg.pass_writes(p2, r)); // coklu-yazim REDDEDILDI -- ikinci write basarisiz
}

ENGINE_TEST(frame_graph_rejects_out_of_range_ids_and_full_capacity) {
  FrameGraph fg;
  const FgPassId p = fg.add_pass("p");
  CHECK(!fg.pass_reads(p, 999));                 // gecersiz kaynak ID
  CHECK(!fg.pass_writes(999, fg.create_resource("r"))); // gecersiz pass ID

  FrameGraph small;
  const FgPassId only_pass = small.add_pass("only");
  const FgResourceId only_res = small.create_resource("only_res");
  for (uint32_t i = 0; i < FrameGraph::kMaxReadsPerPass; i++) {
    CHECK(small.pass_reads(only_pass, only_res));
  }
  CHECK(!small.pass_reads(only_pass, only_res)); // read kapasitesi doldu
}

ENGINE_TEST(frame_graph_pass_with_no_reads_is_culled_when_not_output) {
  FrameGraph fg;
  const FgResourceId r = fg.create_resource("r");
  const FgPassId isolated = fg.add_pass("isolated"); // hic okumuyor, is_output degil, hicbir sey onu okumuyor
  const FgPassId out = fg.add_pass("out", /*is_output=*/true);
  CHECK(fg.pass_writes(isolated, r));
  // out, r'yi OKUMUYOR -- isolated'a giden hicbir yol yok.
  CHECK(fg.compile());
  CHECK(fg.was_culled(isolated));
  CHECK(fg.executed_count() == 1);
  CHECK(fg.executed_pass(0) == out);
}
