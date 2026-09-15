// L4 SIMULATION — Analitik iki-kemik IK (500 madde listesi #452 "Ters
// Kinematik (IK)"). Kol/bacak icin her oyun motorunda kullanilan standart
// teknik (Unreal'in TwoBoneIK dugumu, Unity'nin TwoBoneIKConstraint'i ile
// AYNI algoritma: kosinus teoremi). Iteratif DEGIL, TEK adimda TAM cozum --
// FABRIK gibi yaklasik/yinelemeli genel-N-kemik yontemlerine 2 kemik icin
// GEREK yok (analitik cozum HER ZAMAN vardir).
//
// Bilincli olarak dar kapsam (interp.hpp/lag_compensation.hpp ile ayni ruh):
// yalniz POZISYON doner (kok/orta/uc), donus/quaternion TASIMAZ -- cagiran
// (animasyon sistemi) bu pozisyonlardan gerekirse look-at ile donus turetir.
// Iskelet/kemik hiyerarsisi TASIMAZ, saf geometri.
#pragma once
#include "core/math/vec.hpp"

namespace tulpar::engine::sim {

struct TwoBoneIkResult {
  Vec3 mid{0, 0, 0};    // orta eklem (dirsek/diz) pozisyonu
  Vec3 end{0, 0, 0};    // uc efektor (el/ayak) pozisyonu
  bool reached = false; // true: end == target (hedef erisim araliginda [|l1-l2|, l1+l2])
};

// root: kok eklem (omuz/kalca). target: ulasilmak istenen nokta. pole: "dirsegin/
// dizin hangi yone dogru buklecegini" belirleyen ipucu noktasi (kendisi
// ERISILEBILIR olmak ZORUNDA degil -- yalniz yon bilgisi kullanilir); kok-hedef
// ekseni etrafindaki, aksi halde belirsiz olan donme serbestligini cozer.
// len1/len2 > 0 varsayilir (cagiran kontrol eder).
TwoBoneIkResult solve_two_bone_ik(Vec3 root, Vec3 target, Vec3 pole, float len1, float len2);

} // namespace tulpar::engine::sim
