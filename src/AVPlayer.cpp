//
// Created by 易思颖 on 2020/4/22.
//

#include "AVPlayer.h"


#include<iostream>
#include<cmath>
#include <mutex>
#include <thread>
#include <chrono>

using std::cout;
using std::endl;

#define __STDC_CONSTANT_MACROS

#ifdef _WIN32
//Windows
extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
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
#include <libswscale/swscale.h>
#include <SDL2/SDL.h>
#include <libavutil/imgutils.h>

#include <libswresample/swresample.h>  //audio


#ifdef __cplusplus
};
#endif
#endif

#define MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio

bool thread_exit;
bool thread_pause;
const Uint32 REFRESH_EVENT = SDL_USEREVENT + 1;
const Uint32 BREAK_EVENT = SDL_USEREVENT + 2;
std::mutex lock;

AVFormatContext *pFormatCtx;

//视频相关
AVCodecParameters *pParameters_v = nullptr;
AVCodecContext *pCodecCtx_v = nullptr;
AVCodec *pCodec_v = nullptr;
int videoStreamIndex = -1;
SwsContext *pSwsContext_v = nullptr;
AVPacket *packet = nullptr;
AVFrame *pFrame_v = nullptr;
AVFrame *pFrameYUV = nullptr;

AVPacket *packet_v = nullptr;

//SDL------------------
SDL_Window *pWindow = nullptr;
SDL_Renderer *pRenderer = nullptr;
SDL_Texture *pTexture = nullptr;
SDL_Event event;

SDL_Rect rect;

//音频相关
AVCodecParameters *pParameters_a = nullptr;
AVCodecContext *pCodecCtx_a = nullptr;
AVCodec *pCodec_a = nullptr;
int audioStreamIndex = -1;
SwsContext *pSwsContext_a = nullptr;
AVFrame *pFrame_a;
SwrContext *au_convert_ctx = nullptr;
uint64_t out_channel_layout;

int out_nb_samples;

int out_sample_rate;

int out_channels;
uint8_t *out_buffer;

int out_buffer_size;

AVSampleFormat out_sample_fmt;
AVFrame wanted_frame;


static Uint8 *audio_chunk;
static Uint32 audio_len;
static Uint8 *audio_pos;

AVPlayer::AVPlayer(char *s) { url = s; }


void AVPlayer::vedio_close() {
    //free
    sws_freeContext(pSwsContext_v);
    SDL_DestroyRenderer(pRenderer);
    SDL_DestroyWindow(pWindow);

    SDL_Quit();

    if (pFrame_v != nullptr) {
        av_frame_free(&pFrame_v);
    }
    if (pFrameYUV) {
        av_frame_free(&pFrameYUV);
    }
    avcodec_close(pCodecCtx_v);
    if (pFormatCtx) {
        avformat_close_input(&pFormatCtx);
    }
}

int AVPlayer::video_init() {
    // 找到视频流
    for (int i = 0; i < pFormatCtx->nb_streams; i++)
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
            break;
        }

    if (videoStreamIndex == -1) {
        cout << ("Didn't find a video stream.") << endl;
        return -1;
    }

    // 从流中获取流参数
    pParameters_v = pFormatCtx->streams[videoStreamIndex]->codecpar;  //流参数

    // 找到解码器
    pCodec_v = avcodec_find_decoder(pParameters_v->codec_id);
    if (pCodec_v == nullptr) {
        cout << ("Codec not found.") << endl;
        return -1;
    }

    pCodecCtx_v = avcodec_alloc_context3(pCodec_v);
    if (avcodec_parameters_to_context(pCodecCtx_v, pParameters_v) < 0) {
        cout << ("CodecCtx not found.") << endl;
        return -1;
    }


    // 打开解码器
    if (avcodec_open2(pCodecCtx_v, pCodec_v, nullptr) < 0) {
        cout << ("Could not open codec.") << endl;
        return -1;
    }


    packet = (AVPacket *) av_malloc(sizeof(AVPacket));
    av_init_packet(packet);

    pFrame_v = av_frame_alloc();
    pFrameYUV = av_frame_alloc();

    unsigned char *out_buffer = (unsigned char *) av_malloc(
            av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx_v->width, pCodecCtx_v->height, 1));

    av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, out_buffer,
                         AV_PIX_FMT_YUV420P, pCodecCtx_v->width, pCodecCtx_v->height, 1);


    pSwsContext_v = sws_getContext(pCodecCtx_v->width, pCodecCtx_v->height, pCodecCtx_v->pix_fmt,
                                   pCodecCtx_v->width, pCodecCtx_v->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL,
                                   NULL, NULL);
    return 0;
}

void AVPlayer::fill_audio(void *udata, Uint8 *stream, int len) {
    SDL_memset(stream, 0, len);
    if (audio_len == 0)
        return;

    len = (len > audio_len ? audio_len : len);    /*  Mix  as  much  data  as  possible  */

    SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);
    audio_pos += len;
    audio_len -= len;
}

int AVPlayer::sdl_init() {//Init
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        cout << "初始化SDL失败" << SDL_GetError() << endl;
        return -1;
    }

    int width = pParameters_v->width;
    int height = pParameters_v->height;
    pWindow = SDL_CreateWindow("播放器", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width,
                               height,
                               SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!pWindow) {
        cout << "创建窗口失败" << endl;
        return -1;
    }

    int dw, dh;
    SDL_GL_GetDrawableSize(pWindow, &dw, &dh);

    pRenderer = SDL_CreateRenderer(pWindow, -1, 0);
    if (!pRenderer) {
        cout << "打开渲染器失败" << endl;
        return -1;
    }

    pTexture = SDL_CreateTexture(pRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,
                                 width, height);
    if (!pTexture) {
        cout << "创建纹理失败" << endl;
        return -1;
    }

    SDL_ThreadFunction fn = [](void *arg) -> int {
        thread_exit = false;
        thread_pause = false;

        while (!thread_exit) {
            if (!thread_pause) {
                SDL_Event event;
                event.type = REFRESH_EVENT;
                SDL_PushEvent(&event);
                SDL_Delay(1);
            }
        }
        thread_exit = false;
        thread_pause = false;

        SDL_Event event;
        event.type = BREAK_EVENT;
        SDL_PushEvent(&event);

        return 0;
    };
    SDL_CreateThread(fn, "video thread", nullptr);


    rect.x = 0;
    rect.y = 0;
    rect.w = dw;
    rect.h = dw * height / width;

    if (rect.h > dh) {
        rect.w = rect.w * dh / rect.h;
        rect.x = (dw - rect.w) / 2;
        rect.h = dh;
    }

    //音频
    //SDL_AudioSpec
    SDL_AudioSpec wanted_spec;
    wanted_spec.freq = pCodecCtx_a->sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = pCodecCtx_a->channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = out_nb_samples;
    wanted_spec.callback = fill_audio;
    wanted_spec.userdata = pCodecCtx_a;

    if (SDL_OpenAudio(&wanted_spec, NULL) < 0) {
        printf("can't open audio.\n");
        return -1;
    }


    wanted_frame.format = AV_SAMPLE_FMT_S16;
    wanted_frame.sample_rate = wanted_spec.freq;
    wanted_frame.channel_layout = av_get_default_channel_layout(wanted_spec.channels);
    wanted_frame.channels = wanted_spec.channels;


    int64_t in_channel_layout = av_get_default_channel_layout(pCodecCtx_a->channels);

    //Swr
    au_convert_ctx = swr_alloc();
    au_convert_ctx = swr_alloc_set_opts(au_convert_ctx, out_channel_layout, out_sample_fmt, out_sample_rate,
                                        in_channel_layout, pCodecCtx_a->sample_fmt, pCodecCtx_a->sample_rate, 0, NULL);
    swr_init(au_convert_ctx);

    //Play
    SDL_PauseAudio(0);

    return 0;
}


int AVPlayer::audio_init() {
    // 找到音频流
    audioStreamIndex = -1;
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
    pParameters_a = pFormatCtx->streams[audioStreamIndex]->codecpar;  //流参数

    // 找到解码器
    pCodec_a = avcodec_find_decoder(pParameters_a->codec_id);
    if (pCodec_a == nullptr) {
        cout << ("Codec not found.") << endl;
        return -1;
    }

    pCodecCtx_a = avcodec_alloc_context3(pCodec_a);
    if (avcodec_parameters_to_context(pCodecCtx_a, pParameters_a) < 0) {
        cout << ("CodecCtx not found.") << endl;
        return -1;
    }


    // 打开解码器
    if (avcodec_open2(pCodecCtx_a, pCodec_a, nullptr) < 0) {
        cout << ("Could not open codec.") << endl;
        return -1;
    }

    packet = (AVPacket *) av_malloc(sizeof(AVPacket));
    av_init_packet(packet);

    //Out Audio Param
    out_channel_layout = AV_CH_LAYOUT_STEREO;
    //nb_samples: AAC-1024 MP3-1152
    out_nb_samples = pCodecCtx_a->frame_size;
    out_sample_fmt = AV_SAMPLE_FMT_S16;
    out_sample_rate = 44100;
    out_channels = av_get_channel_layout_nb_channels(out_channel_layout);
    //Out Buffer Size
    out_buffer_size = av_samples_get_buffer_size(NULL, out_channels, out_nb_samples, out_sample_fmt, 1);

    out_buffer = (uint8_t *) av_malloc(MAX_AUDIO_FRAME_SIZE * 2);
    pFrame_a = av_frame_alloc();

    return 0;
}

int AVPlayer::start() {


    av_register_all();
    pFormatCtx = avformat_alloc_context();
    //Open
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


    if (video_init() != 0) {
        cout << "video init fail" << endl;
        return -1;
    }

    if (audio_init() != 0) {
        cout << "audio init fail" << endl;
        return -1;
    }

    if (sdl_init() != 0) {
        cout << "sdl init fail" << endl;
        return -1;
    }

    while (av_read_frame(pFormatCtx, packet) >= 0) {
        if (packet->stream_index == videoStreamIndex) {
            avcodec_send_packet(pCodecCtx_v, packet);
            if (avcodec_receive_frame(pCodecCtx_v, pFrame_v) == 0) {

                sws_scale(pSwsContext_v, (const unsigned char *const *) pFrame_v->data, pFrame_v->linesize, 0,
                          pCodecCtx_v->height, pFrameYUV->data, pFrameYUV->linesize);

                SDL_WaitEvent(&event);
                if(event.type == REFRESH_EVENT){
                    //显示到屏幕
                    SDL_UpdateTexture(pTexture, nullptr, pFrameYUV->data[0], pFrameYUV->linesize[0]);
                    SDL_RenderClear(pRenderer);
                    SDL_RenderCopy(pRenderer, pTexture, nullptr, &rect);
                    SDL_RenderPresent(pRenderer);
                }else if (event.type == BREAK_EVENT) {
                    break;
                } else if (event.type == SDL_KEYDOWN) {
                    if (event.key.keysym.sym == SDLK_SPACE) { //空格键暂停
                        thread_pause = !thread_pause;
                    }
                    if (event.key.keysym.sym == SDLK_ESCAPE) { // ESC键退出
                        thread_exit = 1;
                    }
                } else {
                    continue;
                }
            }
        } else if (packet->stream_index == audioStreamIndex) {
            avcodec_send_packet(pCodecCtx_a, packet);
            if (avcodec_receive_frame(pCodecCtx_a, pFrame_a) == 0) {
                /**
                * 接下来判断我们之前设置SDL时设置的声音格式(AV_SAMPLE_FMT_S16)，声道布局，
                * 采样频率，每个AVFrame的每个声道采样数与
                * 得到的该AVFrame分别是否相同，如有任意不同，我们就需要swr_convert该AvFrame，
                * 然后才能符合之前设置好的SDL的需要，才能播放
                */
                if (pFrame_a->format != AUDIO_S16SYS
                    || pFrame_a->channel_layout != pCodecCtx_a->channel_layout
                    || pFrame_a->sample_rate != pCodecCtx_a->sample_rate
                    || pFrame_a->nb_samples != out_nb_samples) {
                    if (au_convert_ctx != nullptr) {
                        swr_free(&au_convert_ctx);
                        au_convert_ctx = nullptr;
                    }
                    au_convert_ctx = swr_alloc_set_opts(NULL, wanted_frame.channel_layout,
                                                        (enum AVSampleFormat) wanted_frame.format,
                                                        wanted_frame.sample_rate,
                                                        pFrame_a->channel_layout,
                                                        (enum AVSampleFormat) pFrame_a->format,
                                                        pFrame_a->sample_rate, 0, NULL);

                    if (au_convert_ctx == NULL || swr_init(au_convert_ctx) < 0) {
                        cout << "swr_init failed" << endl;
                        break;
                    }
                }
                int dst_nb_samples = av_rescale_rnd(
                        swr_get_delay(au_convert_ctx, pFrame_a->sample_rate) + pFrame_a->nb_samples,
                        wanted_frame.sample_rate, wanted_frame.format, AV_ROUND_INF);
                swr_convert(au_convert_ctx, &out_buffer, dst_nb_samples, (const uint8_t **) pFrame_a->data,
                            pFrame_a->nb_samples);
            }
            while (audio_len > 0)//Wait until finish
                SDL_Delay(1);

            //Set audio buffer (PCM data)
            audio_chunk = (Uint8 *) out_buffer;
            //Audio buffer length
            audio_len = out_buffer_size;
            audio_pos = audio_chunk;
        }
        av_packet_unref(packet);
    }
    vedio_close();

    return 0;
}

