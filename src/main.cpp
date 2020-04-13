//
// Created by 易思颖 on 2020/4/13.
//

#include<iostream>
#include <VideoPlayer.h>
#include <AudioOnlyPlayer.h>

int main() {
    char *url = "/Users/yisiying/Downloads/[Mabors-Sub][Kono Subarashii Sekai ni Shukufuku o! Kurenai Densetsu][Movie][1080P][CHT&JPN][BDrip][AVC AAC YUV420P8].mp4/[Mabors-Sub][Kono Subarashii Sekai ni Shukufuku o! Kurenai Densetsu][Movie][1080P][CHT&JPN][BDrip][AVC AAC YUV420P8].mp4";
    auto *pPlayer = new VideoPlayer(url);
    auto *pAudioPlayer = new AudioOnlyPlayer(url);
//    pPlayer->start();
    pAudioPlayer->start();
}
