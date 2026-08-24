#include "codecs/no_audio_codec.h"
#include "config.h"
#include "wifi_board.h"

class Esp32S3Wroom1N16r8Board : public WifiBoard {
public:
    AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplexPdm audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                                                  I2S_BCK_IO, I2S_WS_IO, I2S_DO_IO, PDM_CLK_IO,
                                                  PDM_DATA_IO);
        return &audio_codec;
    }
};

DECLARE_BOARD(Esp32S3Wroom1N16r8Board);
