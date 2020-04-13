#include "VideoPlayer.h"

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

#ifdef __cplusplus
};
#endif
#endif

bool thread_exit;
bool thread_pause;
const Uint32 REFRESH_EVENT = SDL_USEREVENT + 1;
const Uint32 BREAK_EVENT = SDL_USEREVENT + 2;
int fps;

VideoPlayer::VideoPlayer(char *s) {
    url = s;
}

int VideoPlayer::start() {
    AVFormatContext *pFormatCtx;
    AVCodecContext *pCodecCtx;
    AVCodec *pCodec;
    AVPacket *packet;
    AVFrame *pFrame;
    AVFrame *pFrameYUV;
    int videoStreamIndex;

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

    // 找到视频流
    videoStreamIndex = -1;
    for (int i = 0; i < pFormatCtx->nb_streams; i++)
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
            break;
        }

    if (videoStreamIndex == -1) {
        cout << ("Didn't find a video stream.\n") << endl;
        return -1;
    }

    fps = pFormatCtx->streams[videoStreamIndex]->avg_frame_rate.num /
          pFormatCtx->streams[videoStreamIndex]->avg_frame_rate.den;

    // 从流中获取流参数
    AVCodecParameters *pParameters = pFormatCtx->streams[videoStreamIndex]->codecpar;  //流参数

    // 找到解码器
    pCodec = avcodec_find_decoder(pParameters->codec_id);
    if (pCodec == nullptr) {
        cout << ("Codec not found.") << endl;
        return -1;
    }

    pCodecCtx = avcodec_alloc_context3(pCodec);
    if (avcodec_parameters_to_context(pCodecCtx, pParameters) < 0) {
        cout << ("CodecCtx not found.") << endl;
        return -1;
    }


    // 打开解码器
    if (avcodec_open2(pCodecCtx, pCodec, nullptr) < 0) {
        cout << ("Could not open codec.") << endl;
        return -1;
    }


    packet = (AVPacket *) av_malloc(sizeof(AVPacket));
    av_init_packet(packet);

    pFrame = av_frame_alloc();
    pFrameYUV = av_frame_alloc();

    unsigned char *out_buffer = (unsigned char *) av_malloc(
            av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1));

    av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, out_buffer,
                         AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);


    SwsContext *pSwsContext = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
                                             pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL,
                                             NULL, NULL);

    //SDL------------------
    //Init
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        cout << "初始化SDL失败" << SDL_GetError() << endl;
        return -1;
    }

    int width = pParameters->width;
    int height = pParameters->height;
    SDL_Window *pWindow = SDL_CreateWindow("播放器", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width,
                                           height,
                                           SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!pWindow) {
        cout << "创建窗口失败" << endl;
        return -1;
    }

    int dw, dh;
    SDL_GL_GetDrawableSize(pWindow, &dw, &dh);

    SDL_Renderer *pRenderer = SDL_CreateRenderer(pWindow, -1, 0);
    if (!pRenderer) {
        cout << "打开渲染器失败" << endl;
        return -1;
    }

    SDL_Texture *pTexture = SDL_CreateTexture(pRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,
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
                SDL_Delay(1000 / fps);
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

    SDL_Event event;

    SDL_Rect rect;
    rect.x = 0;
    rect.y = 0;
    rect.w = dw;
    rect.h = dw * height / width;

    if (rect.h > dh) {
        rect.w = rect.w * dh / rect.h;
        rect.x = (dw - rect.w) / 2;
        rect.h = dh;
    }

    while (true) {
        SDL_WaitEvent(&event);
        if (event.type == REFRESH_EVENT) {
            while (true) {
                if (av_read_frame(pFormatCtx, packet) < 0) {
                    thread_exit = true;
                }
                if (packet->stream_index == videoStreamIndex) {
                    break;
                }
            }

            if (packet->stream_index == videoStreamIndex) {
                avcodec_send_packet(pCodecCtx, packet);
                if (avcodec_receive_frame(pCodecCtx, pFrame) == 0) {

                    sws_scale(pSwsContext, (const unsigned char *const *) pFrame->data, pFrame->linesize, 0,
                              pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);

                    //显示到屏幕
                    SDL_UpdateTexture(pTexture, nullptr, pFrameYUV->data[0], pFrameYUV->linesize[0]);
                    SDL_RenderClear(pRenderer);
                    SDL_RenderCopy(pRenderer, pTexture, nullptr, &rect);
                    SDL_RenderPresent(pRenderer);
                }
                av_packet_unref(packet);
            }
        } else if (event.type == BREAK_EVENT) {
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

//free
    sws_freeContext(pSwsContext);
    SDL_DestroyRenderer(pRenderer);
    SDL_DestroyWindow(pWindow);

    SDL_Quit();

    if (pFrame) {
        av_frame_free(&pFrame);
    }
    if (pFrameYUV) {
        av_frame_free(&pFrameYUV);
    }
    avcodec_close(pCodecCtx);
    if (pFormatCtx) {
        avformat_close_input(&pFormatCtx);
    }

    return 0;
}
