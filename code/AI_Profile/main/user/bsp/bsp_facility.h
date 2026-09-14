#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Declare embedded OGG symbols as: <name>_ogg_start / <name>_ogg_end
#define DECLARE_OGG_ASSET(name)                                             \
	extern const char name##_ogg_start[] asm("_binary_" #name "_ogg_start"); \
	extern const char name##_ogg_end[] asm("_binary_" #name "_ogg_end")

void speaker_play_assets(const char* ogg_start, const char* ogg_end);
void speaker_set_volume(int volume);

#ifdef __cplusplus
}

#include <string_view>

inline void speaker_play_assets(std::string_view ogg)
{
	speaker_play_assets(ogg.data(), ogg.data() + ogg.size());
}
#endif
