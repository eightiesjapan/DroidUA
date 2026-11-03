// Decode supplied original files to EOF without a display or sound device.
#include <cstdio>
#include <cmath>
#define PL_MPEG_IMPLEMENTATION
#include "../third_party/pl_mpeg/pl_mpeg.h"
#include <cstdio>
#include <vector>
struct Counts { size_t video=0,audio=0; double lastVideo=0,lastAudio=0; };
int main(int argc,char **argv) {
    if(argc<2) return 1;
    for(int i=1;i<argc;++i) {
        auto *decoder=plm_create_with_filename(argv[i]);
        if(!decoder || !plm_has_headers(decoder) || !plm_get_width(decoder)) return 2;
        Counts count;
        plm_set_video_decode_callback(decoder,[](plm_t *,plm_frame_t *f,void *user) {
            auto &c=*static_cast<Counts *>(user);
            if(!c.video) { std::vector<uint8_t> rgb(f->width*f->height*3); plm_frame_to_rgb(f,rgb.data(),f->width*3); }
            ++c.video; c.lastVideo=f->time;
        },&count);
        plm_set_audio_decode_callback(decoder,[](plm_t *,plm_samples_t *s,void *user) {
            auto &c=*static_cast<Counts *>(user); ++c.audio; c.lastAudio=s->time;
        },&count);
        int steps=0;
        while(!plm_has_ended(decoder) && steps++<36000) plm_decode(decoder,0.05);
        const bool okay=plm_has_ended(decoder) && count.video>0 && count.audio>0 &&
            std::abs(count.lastVideo-count.lastAudio)<0.3;
        std::printf("%s: %zu video, %zu audio; end %.2f/%.2fs %s\n",argv[i],count.video,count.audio,
            count.lastVideo,count.lastAudio,okay?"PASS":"FAIL");
        plm_destroy(decoder);
        if(!okay) return 3;
    }
}
