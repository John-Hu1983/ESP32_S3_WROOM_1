#include "user/common/user_app_notify.h"

#include <string_view>

#include "application.h"
#include "audio/audio_codec.h"
#include "boards/common/board.h"

namespace {

extern const char paper_out_ogg_start[] asm("_binary_paper_out_ogg_start");
extern const char paper_out_ogg_end[] asm("_binary_paper_out_ogg_end");

static const std::string_view s_paper_out_sound(
    static_cast<const char*>(paper_out_ogg_start),
    static_cast<size_t>(paper_out_ogg_end - paper_out_ogg_start)
);

}  // namespace

void speaker_alarm_no_paper(void)
{
    Application::GetInstance().Schedule([]() {
        Application::GetInstance().PlaySound(s_paper_out_sound);
    });
}
