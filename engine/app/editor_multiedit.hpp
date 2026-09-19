// L6 APP — editor_multiedit: coklu secimde alan duzenlemesini yaymak.
//
// NEDEN: 5 nesne secip Ozellikler panelinde bir alani degistirmek YALNIZ ana
// seciliyi degistiriyordu (panel "alanlar ana secilide" diyordu). UE5, Unity,
// Source 2 ve CryEngine'de N nesnenin ortak alani BIRLIKTE duzenlenir; 20
// nesnenin rengini degistirmek 20 ayri tik olmamali.
//
// YAKLASIM: her bilesen paneline ayri "coklu" kod yazmak yerine, ana secilideki
// degisiklik ALAN-YAPRAGI duzeyinde fark alinir ve digerlerine uygulanir.
// Yaprak: float/int/bool/enum tek basina, Vec'in her bileseni ayri (pos.x'i
// yazmak digerlerinin y/z'sine dokunmaz -- UE5 ile ayni), char dizisi butun.
// components/flags BIT duzeyinde; name ve parent hic yayilmaz.
#pragma once
#include <cstdint>

#include "content/scene.hpp"

namespace tulpar::engine::app {

// before -> after farkini target'a uygular. Donus: target degisti mi.
bool multiedit_apply(const content::SceneEntity &before, const content::SceneEntity &after,
                     content::SceneEntity *target);
// Tablodaki yaprak sayisi (kapi ve testler icin).
uint32_t multiedit_leaf_count();

} // namespace tulpar::engine::app
