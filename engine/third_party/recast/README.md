# recastnavigation (vendored)

**v1.6.0**, zlib (LICENSE). Yalnız `Recast/` (navmesh **bake**: voxel → bölge → kontur → poly mesh) ve
`Detour/` (runtime **sorgu**: `dtNavMesh`, `dtNavMeshQuery`). `DetourCrowd`/`DetourTileCache`/demo yok
(gerekince eklenir). Derleme `engine/CMakeLists.txt` (`engine_recast`), glob ile.

Plan §1.6 "Recast/Detour: navmesh build'de bake, runtime sadece query": Recast yalnız sahne derleyicisinde
(L6) ve testte koşar, ayırması serbest; Detour sorguları düğüm havuzu init'te, sorgu içinde ayırma
**ölçülür** (`dtAllocSetCustom` kancası) ve 0 iddia edilir.
