// Original MPEG-1/MP2 cinematics, decoded without transcoding game assets.
#include <cstdio>
#include <cmath>
#define PL_MPEG_IMPLEMENTATION
#include "../../third_party/pl_mpeg/pl_mpeg.h"
#include "system/movie.h"
#include "system/fsmgr.h"
#include "system/gfx.h"
#include "system/sound.h"
#include "winp.h"
#include <GL/gl.h>
#include <algorithm>
#include <memory>
#include <vector>

namespace {
bool playing=false, suspended=false;
int MovieEvents(void *,SDL_Event *event) {
    switch(event->type) {
    case SDL_APP_WILLENTERBACKGROUND: suspended=true; break;
    case SDL_APP_DIDENTERFOREGROUND: suspended=false; break;
    case SDL_KEYDOWN:
    case SDL_JOYBUTTONDOWN:
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_MOUSEBUTTONDOWN:
    case SDL_FINGERDOWN:
        playing=false;
        return 0;
    case SDL_KEYUP:
    case SDL_JOYBUTTONUP:
    case SDL_CONTROLLERBUTTONUP:
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEMOTION:
    case SDL_FINGERUP:
    case SDL_FINGERMOTION:
        return 0; // Do not click/fire into the screen behind a cinematic.
    }
    return 1;
}
struct Playback {
    GLuint texture=0;
    std::vector<uint8_t> rgb;
    size_t frames=0, audioFrames=0;
    static void Video(plm_t *,plm_frame_t *frame,void *user) {
        auto &self=*static_cast<Playback *>(user);
        self.rgb.resize(frame->width*frame->height*3);
        plm_frame_to_rgb(frame,self.rgb.data(),frame->width*3);
        auto size=System::GetResolution();
        glViewport(0,0,size.x,size.y);
        GFX::Engine.SetProjectionMatrix(mat4x4f());
        GFX::Engine.SetModelViewMatrix(mat4x4f());
        auto &states=GFX::Engine.States();
        auto saved=states;
        states.Zwrite=false; states.DepthTest=false; states.AlphaBlend=false;
        states.Prog=GFX::Engine.GetStdShaderProg();
        if(!self.texture) glGenTextures(1,&self.texture);
        states.Tex=self.texture;
        GFX::Engine.SetRenderStates(0);
        glClearColor(0,0,0,1);
        glClear(GL_COLOR_BUFFER_BIT);
        glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,frame->width,frame->height,0,
                     GL_RGB,GL_UNSIGNED_BYTE,self.rgb.data());
        // Original movies are presented at 4:3, including 352x240 MPEG frames.
        const double aspect=4.0/3.0, screenAspect=double(size.x)/size.y;
        const float w=std::min(1.0,aspect/screenAspect);
        const float h=std::min(1.0,screenAspect/aspect);
        std::array<GFX::TVertex,4> vertices={
            GFX::TVertex(vec3f(-w,h,0),tUtV(0,0)),
            GFX::TVertex(vec3f(-w,-h,0),tUtV(0,1)),
            GFX::TVertex(vec3f(w,-h,0),tUtV(1,1)),
            GFX::TVertex(vec3f(w,h,0),tUtV(1,0))};
        GFX::Engine.DrawVtxQuad(vertices);
        System::Flip();
        states=saved;
        ++self.frames;
    }
    static void Audio(plm_t *,plm_samples_t *samples,void *user) {
        auto &self=*static_cast<Playback *>(user);
        std::vector<int16_t> pcm(samples->count*2);
        for(size_t i=0;i<pcm.size();++i)
            pcm[i]=int16_t(std::max(-1.0f,std::min(1.0f,samples->interleaved[i]))*32767);
        if(SFXEngine::SFXe.AudioStream)
            SFXEngine::SFXe.AudioStream->Feed(pcm.data(),pcm.size()*sizeof(int16_t),int32_t(samples->time*1000));
        ++self.audioFrames;
    }
};
}
namespace System {
TMovie TMovie::Instance;
TMovie::TMovie()=default;
TMovie::~TMovie()=default;
void TMovie::Init() { SDL_Log("OpenUA original MPEG movie player ready"); }
void TMovie::PlayMovie(const std::string &name,int volume) {
    auto *file=FSMgr::iDir::findNode(name);
    std::unique_ptr<plm_t,decltype(&plm_destroy)> decoder(
        file?plm_create_with_filename(file->getPath().c_str()):nullptr,plm_destroy);
    if(!decoder || !plm_has_headers(decoder.get()) || !plm_get_width(decoder.get())) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,"OpenUA movie missing/unsupported: %s",name.c_str());
        return;
    }
    Playback playback;
    auto *audio=SFXEngine::SFXe.AudioStream;
    if(audio) {
        audio->stop();
        audio->setMasterVolume(volume);
        if(plm_get_samplerate(decoder.get())>0)
            audio->SetFormat(AL_FORMAT_STEREO16,plm_get_samplerate(decoder.get()));
    }
    plm_set_video_decode_callback(decoder.get(),Playback::Video,&playback);
    if(audio) plm_set_audio_decode_callback(decoder.get(),Playback::Audio,&playback);
    else plm_set_audio_enabled(decoder.get(),0);
    plm_set_audio_lead_time(decoder.get(),0.1);
    NC_STACK_winp::AndroidReset();
    playing=true; suspended=false;
    auto projection=GFX::Engine.GetProjectionMatrix();
    EventsAddHandler(MovieEvents);
    SDL_Log("OpenUA movie started: %s",name.c_str());
    Uint32 last=SDL_GetTicks();
    while(playing && !plm_has_ended(decoder.get())) {
        if(ProcessEvents()) { playing=false; PostQuitMessage(); break; }
        const Uint32 now=SDL_GetTicks();
        const double dt=std::min((now-last)/1000.0,0.05);
        last=now; // Excludes screen-off time even if SDL blocks until resume.
        if(!suspended && playing) plm_decode(decoder.get(),dt);
        SDL_Delay(suspended?20:1);
    }
    if(playing && audio) {
        // Let the short queued audio tail finish naturally; skipping stops at once.
        const Uint32 until=SDL_GetTicks()+audio->BuffersTime()+100;
        while(playing && SDL_GetTicks()<until) {
            if(ProcessEvents()) { PostQuitMessage(); break; }
            SDL_Delay(5);
        }
    }
    EventsDeleteHandler(MovieEvents);
    if(audio) audio->stop();
    if(playback.texture) glDeleteTextures(1,&playback.texture);
    GFX::Engine.SetProjectionMatrix(projection);
    GFX::Engine.SetModelViewMatrix(mat4x4f());
    GFX::Engine.SetRenderStates(1);
    NC_STACK_winp::AndroidReset();
    SDL_Log("OpenUA movie finished: %s (%zu video, %zu audio frames)",name.c_str(),playback.frames,playback.audioFrames);
}
}
