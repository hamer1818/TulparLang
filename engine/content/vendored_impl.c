/* Uçüncü parti yükleyicilerin tek TU'daki gövdeleri (C, uyarılar kapalı:
   -w). Motor kodu yalnız başlıkları görür. Yalnız YÜKLEME anında malloc. */
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_FAILURE_STRINGS
#include <stb_image.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
