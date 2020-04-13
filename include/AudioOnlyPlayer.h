//
// Created by 易思颖 on 2020/4/13.
//

#ifndef VIDEOPLAYER_AUDIOONLYPLAYER_H
#define VIDEOPLAYER_AUDIOONLYPLAYER_H



class AudioOnlyPlayer {
public:
    explicit AudioOnlyPlayer(char *);
    int start();
private:
    char * url;

};


#endif //VIDEOPLAYER_AUDIOONLYPLAYER_H
