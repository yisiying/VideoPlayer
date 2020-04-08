#include<iostream>
#include<math.h>

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
#ifdef __cplusplus
};
#endif
#endif

int fps = 25; //帧率
char url[] = "/Users/yisiying/Downloads/[Mabors-Sub][Kono Subarashii Sekai ni Shukufuku o! Kurenai Densetsu][Movie][1080P][CHT&JPN][BDrip][AVC AAC YUV420P8].mp4/[Mabors-Sub][Kono Subarashii Sekai ni Shukufuku o! Kurenai Densetsu][Movie][1080P][CHT&JPN][BDrip][AVC AAC YUV420P8].mp4";

bool thread_exit = false;
bool thread_pause = false;
const Uint32 REFRESH_EVENT = SDL_USEREVENT + 1;
const Uint32 BREAK_EVENT = SDL_USEREVENT + 2;

int main() {
    AVFormatContext *pFormatCtx;
    AVCodecContext *pCodecCtx;
    AVCodec *pCodec;
    AVPacket *packet;
    AVFrame *pFrame;
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
                                           SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!pWindow) {
        cout << "创建窗口失败" << endl;
        return -1;
    }

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
    SDL_GetDisplayUsableBounds(0, &rect);
    rect.x = 0;
    rect.y = 0;
    //把视频按原比例缩小到1680*960以内
    if (width > rect.w) {
        rect.h = height * rect.w / width;
    }
    if (height > rect.h) {
        rect.w = width * rect.h / height;
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
                    //显示到屏幕
                    SDL_UpdateYUVTexture(pTexture, nullptr, pFrame->data[0], pFrame->linesize[0], pFrame->data[1],
                                         pFrame->linesize[1], pFrame->data[2], pFrame->linesize[2]);
//                    rect.x = 0;
//                    rect.y = 0;
//                    rect.w = width;
//                    rect.h = height;

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

/*
 //没有考虑解码的时间，不是严格的40ms一帧
while (av_read_frame(pFormatCtx, packet) >= 0) {
    if (packet->stream_index == videoStreamIndex) {
        //解码
        avcodec_send_packet(pCodecCtx, packet);
        while (avcodec_receive_frame(pCodecCtx, pFrame) == 0) {
            //显示到屏幕
            SDL_UpdateYUVTexture(pTexture, NULL, pFrame->data[0], pFrame->linesize[0], pFrame->data[1],
                                 pFrame->linesize[1], pFrame->data[2], pFrame->linesize[2]);
            rect.x = 0;
            rect.y = 0;
            rect.w = pCodecCtx->width;
            rect.h = pCodecCtx->height;

            SDL_RenderClear(pRenderer);
            SDL_RenderCopy(pRenderer, pTexture, NULL, &rect);
            SDL_RenderPresent(pRenderer);

            SDL_Delay(40);//没有考虑解码的时间，不是严格的40ms一帧
        }
    }

    av_packet_unref(packet);

    // 事件处理
    SDL_Event event;
    SDL_PollEvent(&event);
    if (event.type == SDL_QUIT) {
        break;
    }
}
 */

//free
    SDL_DestroyRenderer(pRenderer);
    SDL_DestroyWindow(pWindow);

    SDL_Quit();

    if (pFrame) {
        av_frame_free(&pFrame);
    }
    avcodec_close(pCodecCtx);
    if (pFormatCtx) {
        avformat_close_input(&pFormatCtx);
    }

    return 0;
}
