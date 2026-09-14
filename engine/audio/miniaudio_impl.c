/* miniaudio govdesi (vendored, MIT-0). Yalniz gereken: cihaz + WAV/FLAC/MP3 kod cozme.
   Motor karistiricisi bizim (audio/mixer.cpp): miniaudio'nun engine/node graph'i yok. */
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_RESOURCE_MANAGER
#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
#define MA_ENABLE_NULL
#if defined(__ANDROID__)
#define MA_ENABLE_AAUDIO   /* dusuk gecikme (API 26+); libaaudio dlopen */
#define MA_ENABLE_OPENSL   /* yedek */
#elif defined(__APPLE__)
#define MA_ENABLE_COREAUDIO
#else
#define MA_ENABLE_PULSEAUDIO
#define MA_ENABLE_ALSA
#endif
#include <miniaudio.h>
