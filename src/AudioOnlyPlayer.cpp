//
// Created by 易思颖 on 2020/4/13.
//

#include "AudioOnlyPlayer.h"

#include<iostream>
#include<cmath>

using std::cout;
using std::endl;

#define __STDC_CONSTANT_MACROS

#ifdef _WIN32
//Windows
extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include <libswresample/swresample.h>  //audio
#include "SDL2/SDL.h"
#include <libavutil/imgutils.h>

};
#else
//Linux...
#ifdef __cplusplus
extern "C"
{
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h> //audio
#include <SDL2/SDL.h>
#include <libavutil/samplefmt.h>
#include <libavutil/imgutils.h>

#ifdef __cplusplus
};
#endif
#endif

#define MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio

static Uint8 *audio_chunk;
static Uint32 audio_len;
static Uint8 *audio_pos;

AudioOnlyPlayer::AudioOnlyPlayer(char *arg) {
    url = arg;
}

void fill_audio(void *udata, Uint8 *stream, int len) {
    SDL_memset(stream, 0, len);
    if (audio_len == 0)
        return;

    len = (len > audio_len ? audio_len : len);    /*  Mix  as  much  data  as  possible  */

    SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);
    audio_pos += len;
    audio_len -= len;
}

int AudioOnlyPlayer::start() {
    avformat_network_init();

    AVFormatContext *pFormatCtx = avformat_alloc_context();
    if (avformat_open_input(&pFormatCtx, url, nullptr, nullptr) != 0) {
        cout << ("Couldn't open input stream.\n") << endl;
        return -1;
    }
    // Retrieve stream information
    if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
        cout << ("Couldn't find stream information.\n") << endl;
        return -1;
    }
    // 输出视频的信息
    av_dump_format(pFormatCtx, 0, url, false);

    // 找到音频流
    int audioStreamIndex = -1;
    for (int i = 0; i < pFormatCtx->nb_streams; i++)
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioStreamIndex = i;
            break;
        }
    if (audioStreamIndex == -1) {
        cout << ("Didn't find a video stream.\n") << endl;
        return -1;
    }

    // 从流中获取流参数
    AVCodecParameters *pParameters = pFormatCtx->streams[audioStreamIndex]->codecpar;  //流参数

    // 找到解码器
    AVCodec *pCodec = avcodec_find_decoder(pParameters->codec_id);
    if (pCodec == nullptr) {
        cout << ("Codec not found.") << endl;
        return -1;
    }

    AVCodecContext *pCodecCtx = avcodec_alloc_context3(pCodec);
    if (avcodec_parameters_to_context(pCodecCtx, pParameters) < 0) {
        cout << ("CodecCtx not found.") << endl;
        return -1;
    }


    // 打开解码器
    if (avcodec_open2(pCodecCtx, pCodec, nullptr) < 0) {
        cout << ("Could not open codec.") << endl;
        return -1;
    }

    AVPacket *pPacket = (AVPacket *) av_malloc(sizeof(AVPacket));
    av_init_packet(pPacket);

    //Out Audio Param
    uint64_t out_channel_layout = AV_CH_LAYOUT_STEREO;
    //nb_samples: AAC-1024 MP3-1152
    int out_nb_samples = pCodecCtx->frame_size;
    AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
    int out_sample_rate = 44100;
    int out_channels = av_get_channel_layout_nb_channels(out_channel_layout);
    //Out Buffer Size
    int out_buffer_size = av_samples_get_buffer_size(NULL, out_channels, out_nb_samples, out_sample_fmt, 1);

    uint8_t *out_buffer = (uint8_t *) av_malloc(MAX_AUDIO_FRAME_SIZE * 2);
    AVFrame *pFrame = av_frame_alloc();

    //SDL
    //Init
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        cout << "Could not initialize SDL" << SDL_GetError() << endl;
        return -1;
    }

    //SDL_AudioSpec
    SDL_AudioSpec wanted_spec;
    wanted_spec.freq = pCodecCtx->sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = pCodecCtx->channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = out_nb_samples;
    wanted_spec.callback = fill_audio;
    wanted_spec.userdata = pCodecCtx;

    if (SDL_OpenAudio(&wanted_spec, NULL) < 0) {
        printf("can't open audio.\n");
        return -1;
    }

    AVFrame wanted_frame;

    wanted_frame.format = AV_SAMPLE_FMT_S16;
    wanted_frame.sample_rate = wanted_spec.freq;
    wanted_frame.channel_layout = av_get_default_channel_layout(wanted_spec.channels);
    wanted_frame.channels = wanted_spec.channels;


    int64_t in_channel_layout = av_get_default_channel_layout(pCodecCtx->channels);

    //Swr
    SwrContext *au_convert_ctx = swr_alloc();
    au_convert_ctx = swr_alloc_set_opts(au_convert_ctx, out_channel_layout, out_sample_fmt, out_sample_rate,
                                        in_channel_layout, pCodecCtx->sample_fmt, pCodecCtx->sample_rate, 0, NULL);
    swr_init(au_convert_ctx);

    //Play
    SDL_PauseAudio(0);

    while (av_read_frame(pFormatCtx, pPacket) >= 0) {
        if (pPacket->stream_index == audioStreamIndex) {
            avcodec_send_packet(pCodecCtx, pPacket);
            if (avcodec_receive_frame(pCodecCtx, pFrame) == 0) {
                if (pFrame->format != AUDIO_S16SYS
                    || pFrame->channel_layout != pCodecCtx->channel_layout
                    || pFrame->sample_rate != pCodecCtx->sample_rate
                    || pFrame->nb_samples != out_nb_samples) {
                    if (au_convert_ctx != nullptr) {
                        swr_free(&au_convert_ctx);
                        au_convert_ctx = nullptr;
                    }
                    au_convert_ctx = swr_alloc_set_opts(NULL, wanted_frame.channel_layout,
                                                        (enum AVSampleFormat) wanted_frame.format,
                                                        wanted_frame.sample_rate,
                                                        pFrame->channel_layout, (enum AVSampleFormat) pFrame->format,
                                                        pFrame->sample_rate, 0, NULL);

                    if (au_convert_ctx == NULL || swr_init(au_convert_ctx) < 0) {
                        cout << "swr_init failed" << endl;
                        break;
                    }
                }
                int dst_nb_samples = av_rescale_rnd(swr_get_delay(au_convert_ctx, pFrame->sample_rate) + pFrame->nb_samples,
                                                    wanted_frame.sample_rate, wanted_frame.format, AV_ROUND_INF);
                swr_convert(au_convert_ctx, &out_buffer, dst_nb_samples, (const uint8_t **) pFrame->data,
                            pFrame->nb_samples);
            }
            while (audio_len > 0)//Wait until finish
                SDL_Delay(1);

            //Set audio buffer (PCM data)
            audio_chunk = (Uint8 *) out_buffer;
            //Audio buffer length
            audio_len = out_buffer_size;
            audio_pos = audio_chunk;
        }
        av_free_packet(pPacket);
    }
    swr_free(&au_convert_ctx);

    SDL_CloseAudio();//Close SDL
    SDL_Quit();

    av_free(out_buffer);
    avcodec_close(pCodecCtx);
    avformat_close_input(&pFormatCtx);

    return 0;

}
