#include "user/common/user_facility.h"

#include "application.h"
#include "audio/audio_codec.h"
#include "boards/common/board.h"

void speaker_play_assets(const char* ogg_start, const char* ogg_end)
{
    if ((ogg_start == nullptr) || (ogg_end == nullptr) || (ogg_end <= ogg_start)) {
        return;
    }

    const std::string_view ogg(ogg_start, static_cast<size_t>(ogg_end - ogg_start));

    Application::GetInstance().Schedule([ogg]() {
        Application::GetInstance().PlaySound(ogg);
    });
}

void speaker_set_volume(int volume)
{
    int target_volume = volume;
    if (target_volume < 0) {
        target_volume = 0;
    }
    if (target_volume > 100) {
        target_volume = 100;
    }

    Application::GetInstance().Schedule([target_volume]() {
        auto* codec = Board::GetInstance().GetAudioCodec();
        if (codec != nullptr) {
            codec->SetOutputVolume(target_volume);
        }
    });
}
