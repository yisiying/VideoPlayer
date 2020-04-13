//
// Created by 易思颖 on 2020/4/13.
//
#ifndef VIDEOPLAYER_VIDEOPLAYER_H
#define VIDEOPLAYER_VIDEOPLAYER_H

class VideoPlayer {
public:
    explicit VideoPlayer(char *s);
    int start();

private:
    char *url;
};

#endif //VIDEOPLAYER_VIDEOPLAYER_H
