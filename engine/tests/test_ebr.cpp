#include "tests/test.hpp"
#include "core/memory/ebr.hpp"

#include <atomic>

using namespace tulpar::engine::core;

namespace {

struct TestNode {
  int value;
};

std::atomic<int> g_destroyed_count{0};

void test_destructor(void* ptr) {
  TestNode* node = static_cast<TestNode*>(ptr);
  // Burada memory delete islemi de yapilabilir, test oldugu icin sadece sayaci artiriyoruz
  delete node;
  g_destroyed_count++;
}

} // namespace

ENGINE_TEST(engine_test_ebr) {
  ebr_init();
  g_destroyed_count = 0;
  
  // Ana thread'i kaydet
  ebr_register_thread();

  TestNode* n1 = new TestNode{42};
  TestNode* n2 = new TestNode{43};

  // ebr_enter cagirmadan retire edelim (aktif kimse yoksa flush'ta silinmeli)
  ebr_retire(n1, test_destructor);
  
  ebr_flush(); // Epoch = 1, Safe = 2 (Bos)
  ebr_flush(); // Epoch = 2, Safe = 0 (n1 burada)
  ebr_flush(); // Epoch = 3 (0), Safe = 1 (Bos)
  
  // kEpochs kadar flush yapildiktan sonra n1 silinmis olmali
  CHECK(g_destroyed_count == 1);

  // Simdi okuma senaryosu
  ebr_enter();
  
  ebr_retire(n2, test_destructor);
  
  // Biz enter oldugumuz surece flush gelse bile epoch ilerlemez, silinmez!
  ebr_flush();
  ebr_flush();
  
  // Hala 1 olmali, cunku aktif thread var ve ayni epoch'ta bekliyor.
  CHECK(g_destroyed_count == 1);

  // Okumayi bitir
  ebr_exit();
  
  // Artik epoch ilerleyebilir
  ebr_flush(); // Epoch artar
  ebr_flush(); 
  ebr_flush(); // Safe epoch n2'ye gelir
  
  CHECK(g_destroyed_count == 2);

  ebr_unregister_thread();
  ebr_shutdown();
}
