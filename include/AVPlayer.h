//
// Created by 易思颖 on 2020/4/22.
//

#ifndef VIDEOPLAYER_AVPLAYER_H
#define VIDEOPLAYER_AVPLAYER_H

class AVPlayer {
public:
    explicit AVPlayer(char *s);

    int start();

private:
    char *url;


//     int video_init();

//     void vedio_close();

//     int audio_init();

//     int sdl_init();
//    static void vedio_close();
//
//    static int video_init();
//
//    static int sdl_init();
//
//    static int audio_init();
//
//    static void fill_audio(void *udata, unsigned char *stream, int len);
    static int audio_init();

    static int video_init();

    static int sdl_init();

    static void fill_audio(void *udata, unsigned char *stream, int len);

    static void vedio_close();
};


#endif //VIDEOPLAYER_AVPLAYER_H
