#include "../fmtlib/printf.h"
#include <SDL2/SDL_opengl.h>

#if defined(__APPLE__) && defined(__MACH__)
#include <OpenGL/glext.h>
#else
#include <GL/glext.h>
#include <queue>
#include <algorithm>
#include <functional>
#include <cmath>
#endif

#include "gfx.h"
#include "../utils.h"
#include "common/common.h"
#include "../ini.h"
#include "../gui/root.h"
#include "../bitmap.h"
#include "../ilbm.h"
#include "../log.h"
#include "../font.h"
#include "inivals.h"
#include "android_display.h"
#ifdef __ANDROID__
#include "android_widefill.h"
#include "android_touchsprites.h"
#endif
#include "glfuncs.h"
#ifdef __ANDROID__
#include "../winp.h"
#endif

#ifdef __ANDROID__
bool UAAndroidMapGripRect(int &x, int &y, int &w, int &h);
#endif

namespace GFX
{   
GFXEngine GFXEngine::Instance;
#ifdef __ANDROID__
struct AndroidCursor {
    std::shared_ptr<SDL_Surface> image;
    int hotX = 0, hotY = 0;
};
static std::map<std::string, AndroidCursor> androidCursors;
#endif

int GFXEngine::can_srcblend;
int GFXEngine::can_destblend;
int GFXEngine::can_stippling;
uint32_t GFXEngine::FpsMaxTicks = 1000/60;

SDL_PixelFormat *GFXEngine::_pixfmt = NULL;
GLint GFXEngine::_glPixfmt, GFXEngine::_glPixtype;
bool GFXEngine::_staticInited = false;

const std::array<vec3d, 8> GFXEngine::_clrEff 
{   vec3d(1.0,  1.0,  1.0)
,   vec3d(1.21, 0.0,  0.29)
,   vec3d(0.13, 0.43, 2.17)
,   vec3d(0.0,  1.60, 1.60)
,   vec3d(1.0,  1.0,  1.0)
,   vec3d(0.57, 0.59, 0.59)
,   vec3d(1.4,  1.08,  1.12)
,   vec3d(0.3,  0.60, 0.7)};

std::vector<TGFXDeviceInfo> GFXEngine::_devices
{
    TGFXDeviceInfo("Opengl", "<primary>")         
};


bool TRenderNode::CompareSolid(TRenderNode *a, TRenderNode *b)
{
    // Strict weak ordering (required by std::stable_sort): group by texture,
    // then by render flags so identical states are drawn back to back.
    if ( !a->Mesh || !b->Mesh )
        return a->Mesh == NULL && b->Mesh != NULL;
    
    if (a->Tex != b->Tex)
        return a->Tex < b->Tex;
    
    return a->Flags < b->Flags;
}

bool TRenderNode::CompareTransparent(TRenderNode* a, TRenderNode* b)
{
    return a->Distance > b->Distance;
}

bool TRenderNode::CompareDistance(TRenderNode* a, TRenderNode* b)
{
    return a->Distance < b->Distance;
}

bool TRenderParams::operator==(const TRenderParams &b)
{
    if (Flags != b.Flags)
        return false;

    if (Flags & RFLAGS_DYNAMIC_TEXTURE)
        return TexSource->IsSameRes( b.TexSource );

    return Tex == b.Tex;
}

void GFXEngine::StaticInit()
{
    if (_staticInited)
        return;

    _staticInited = true;
    
    SDL_DisplayMode curr;
    SDL_GetCurrentDisplayMode(0, &curr);
#ifdef __ANDROID__
    // GLES has no BGRA upload; keep the 2D layer in RGBA byte order so the
    // per-frame screen texture upload needs no CPU swizzle inside gl4es.
    curr.format = SDL_PIXELFORMAT_ABGR8888;
#endif
    
    switch(curr.format)
    {
        case SDL_PIXELFORMAT_RGB888:
        case SDL_PIXELFORMAT_ARGB8888:
            _pixfmt = SDL_AllocFormat( SDL_PIXELFORMAT_ARGB8888 );
            _glPixfmt = GL_BGRA;
            _glPixtype = GL_UNSIGNED_BYTE;
            break;
        
        case SDL_PIXELFORMAT_BGR888:
        case SDL_PIXELFORMAT_ABGR8888:
        default:
            _pixfmt = SDL_AllocFormat( SDL_PIXELFORMAT_ABGR8888 );
            _glPixfmt = GL_RGBA;
            _glPixtype = GL_UNSIGNED_BYTE;
            break;
    }
}

GFXEngine::GFXEngine()
{
    for(TileMap *&t : _tiles)
        t = NULL;
    
    for(SDL_Color &c : _palette)
        c = {0, 0, 0, 0};
    
    _forcesoftcursor = 0;
    _field_38 = 0;
    _txt16bit = 0;
    _use_simple_d3d = 0;
    _disable_lowres = 0;
    _export_window_mode = 0;
    _flags = 0;

    _dither = 0;
    _filter = 0;
    _antialias = 0;
    _alpha = 255;
    _zbuf_when_tracy = 0;
    _colorkey = 0;

    _sceneBeginned = 0;

    _corrIW = _corrW = 1.0;
    _corrIH = _corrH = 1.0;

    _solidFont = true;
    
    _setFrustumClip(1.0, 8192.0);
    
    _normClr = vec3d(1.0, 1.0, 1.0);
    _invClr = vec3d(0.0, 0.0, 0.0);
}

std::string read_guid(const std::string &filename)
{
    FSMgr::FileHandle *fil = uaOpenFileAlloc(filename, "r");
    if ( !fil )
        return "";
    
    std::string guid;
    fil->ReadLine(&guid);
    delete fil;
    return guid;
}

bool out_guid_to_file(const std::string &filename, const std::string &name)
{
    FSMgr::FileHandle *fil = uaOpenFileAlloc(filename, "w");
    if ( !fil )
        return false;
    
    fil->puts(name);
    delete fil;
    return true;
}

void out_yes_no_status(const char *filename, int val)
{
    FSMgr::FileHandle *fil = uaOpenFileAlloc(filename, "w");
    if ( fil )
    {
        if ( val )
            fil->puts("yes");
        else
            fil->puts("no");
        delete fil;
    }
}

void GFXEngine::DrawTextEntry(const ScreenText *txt)
{
    if ( _font.ttfFont )
    {
        if ( txt->flag & 0x20 )
        {
            _font.r = txt->p1;
            _font.g = txt->p2;
            _font.b = txt->p3;
        }
        else
        {
            if (!txt->string.empty())
            {

                int cx = 0, cy = 0;

                if ( txt->flag & 0xE )
                {
                    TTF_SizeUTF8(_font.ttfFont, txt->string.c_str(), &cx, &cy);
                }

                int p1 = txt->p1;
                int p2 = txt->p2;
                int p3 = txt->p3;
                int p4 = txt->p4;

                if ( txt->flag & 8 )
                    p3 = cx * p3 / 100;

                SDL_Rect clipRect;

                clipRect.x = p1;
                clipRect.w = p3 + 4;
                clipRect.h = p4 + 1;
                clipRect.y = p2;

                if ( txt->flag & 2 )
                {
                    if ( txt->flag & 8 )
                    {
                        p1 -= cx;
                        clipRect.x = p1;
                        clipRect.w = p3 + 4;
                    }
                    else
                    {
                        p1 += (p3 - cx);
                    }
                }
                else if ( txt->flag & 4 )
                {
                    if ( txt->flag & 8 )
                    {
                        p1 -= cx / 2;
                        clipRect.x = p1;
                        clipRect.w = p3 + 4;
                    }
                    else
                    {
                        p1 += (p3 - cx) / 2;
                    }
                }

                SDL_SetClipRect(Screen(), &clipRect);


                int v10 = ((p4 - _font.height) / 2) - 2 + p2;
                if ( txt->flag & 0x10 )
                {
                    v10++;
                    p1++;
                }

                SDL_Color clr;
                clr.a = 255;
                clr.r = 0;
                clr.g = 0;
                clr.b = 0;

                SDL_Surface *tmp;

                if (_solidFont)
                {
                    tmp = TTF_RenderUTF8_Solid(_font.ttfFont, txt->string.c_str(), clr);
                    SDL_SetSurfaceBlendMode(tmp, SDL_BLENDMODE_NONE);
                }
                else
                {
                    tmp = TTF_RenderUTF8_Blended(_font.ttfFont, txt->string.c_str(), clr);
                    SDL_SetSurfaceBlendMode(tmp, SDL_BLENDMODE_BLEND);
                }
                
                if (tmp)
                {
                    SDL_Rect want;
                    want.w = tmp->w;
                    want.h = tmp->h;
                    want.x = p1 + 2;
                    want.y = v10 + 1;

                    SDL_BlitSurface(tmp, NULL, Screen(), &want);

                    clr.a = 255;
                    clr.r = _font.r;
                    clr.g = _font.g;
                    clr.b = _font.b;

                    if (_solidFont)
                    {
                        SDL_SetPaletteColors(tmp->format->palette, &clr, 1, 1);
                    }
                    else
                    {
                        SDL_FreeSurface(tmp);
                        tmp = TTF_RenderUTF8_Blended(_font.ttfFont, txt->string.c_str(), clr);
                        SDL_SetSurfaceBlendMode(tmp, SDL_BLENDMODE_BLEND);
                    }

                    want.w = tmp->w;
                    want.h = tmp->h;
                    want.x = p1 + 1;
                    want.y = v10;

                    SDL_BlitSurface(tmp, NULL, Screen(), &want);
                    SDL_FreeSurface(tmp);


                    SDL_SetClipRect(Screen(), NULL);
                }
            }
        }
    }
}

void GFXEngine::AddScreenText(const std::string &string, int p1, int p2, int p3, int p4, int flag)
{
    ScreenText *v8 = new ScreenText;
    v8->string = string;
    v8->p1 = p1;
    v8->p2 = p2;
    v8->p3 = p3;
    v8->p4 = p4;
    v8->flag = flag;

    _font.entries.push_back(v8);
}

void GFXEngine::DrawScreenText()
{
    _font.r = 255;
    _font.g = 255;
    _font.b = 0;

    for ( std::list<ScreenText *>::iterator it = _font.entries.begin(); it != _font.entries.end(); it++ )
    {
        DrawTextEntry(*it);
        delete (*it);
    }

    _font.entries.clear();
}

void GFXEngine::initPolyEngine()
{
    _states = GfxStates();
    _states.LinearFilter = (_filter != 0);

    SetRenderStates(1);

    glEnable(GL_CULL_FACE);

    if (_dither)
        glEnable(GL_DITHER);
    else
        glDisable(GL_DITHER);
}

int GFXEngine::LoadFontByDescr(const std::string &fontname)
{
    std::vector<std::string> splt = Stok::Split(fontname, ",");

    std::string facename;
    std::string s_height;
    
    if (splt.size() > 0)
        facename = splt[0];
    
    if (splt.size() > 1)
        s_height = splt[1];
    //const char *s_weight = strtok(0, ",");
    //const char *s_charset = strtok(0, ",");

    int height;//, weight, charset;
    if ( !facename.empty() && !s_height.empty() )//&& s_weight && s_charset )
    {
        height = std::stoi(s_height);
        //weight = atoi(s_weight);
        //charset = atoi(s_charset);
    }
    else
    {
        height = 12;
        //charset = 0;
        facename = "MS Sans Serif";
        //weight = 400;
    }

    if ( _font.ttfFont )
    {
        TTF_CloseFont(_font.ttfFont);
        _font.ttfFont = NULL;
    }

    _font.height = height;
    _font.ttfFont = System::LoadFont(facename, height);

    if ( _font.ttfFont )
    {
        if (!_solidFont)
            TTF_SetFontHinting(_font.ttfFont, TTF_HINTING_LIGHT);
        else
            TTF_SetFontHinting(_font.ttfFont, TTF_HINTING_MONO);

        return 1;
    }

    printf("Can't load font\n");

    return 0;
}


size_t GFXEngine::windd_func0(IDVList &stak)
{
    int txt16bit_def = read_yes_no_status("env/txt16bit.def", 1);
    int drawprim_def = read_yes_no_status("env/drawprim.def", 0);
    _export_window_mode = System::IniConf::GfxExportWindowMode.Get<bool>();     // gfx.export_window_mode

    switch(System::IniConf::GfxBlending.Get<int>())
    {
        case 0:
        {
            can_srcblend = 1;
            can_destblend = 0;
            can_stippling = 0;
        }
        break;
        
        default:
        case 1:
        {
            can_srcblend = 1;
            can_destblend = 1;
            can_stippling = 0;
        }
        break;
    
        case 2:
        {
            can_srcblend = 0;
            can_destblend = 0;
            can_stippling = 1;
        }
        break;
    }

    SetResVariables( Common::Point(stak.Get<int32_t>(ATT_WIDTH, DEFAULT_WIDTH), stak.Get<int32_t>(ATT_HEIGHT, DEFAULT_HEIGHT)) );

    _forcesoftcursor = 0;
    _disable_lowres = System::IniConf::GfxDisableLowres.Get<bool>();
    _txt16bit = txt16bit_def;
    _use_simple_d3d = drawprim_def;

    _solidFont = System::IniConf::GfxSolidFont.Get<bool>();

    switch( System::IniConf::GfxVsync.Get<int>() )
    {
        case 0:
            SDL_GL_SetSwapInterval(0);
            break;
        
        default:
        case 1:
            SDL_GL_SetSwapInterval(1);
            break;
        
        case 2:
            {
                if ( SDL_GL_SetSwapInterval(-1) == -1)
                    SDL_GL_SetSwapInterval(1);
            }
            break;
    }

    fpsLimitter(System::IniConf::GfxMaxFps.Get<int>());

    LoadFontByDescr("MS Sans Serif,12,400,0");

    //win3d->field_54______rsrc_field4 = (bitmap_intern *)getRsrc_pData();
    return 1;
}


bool GFXEngine::SetResVariables(Common::Point res)
{
#ifdef __ANDROID__
    res = Android::LogicalSize();
#endif
    _resolution = res;
    
    _clip = _resolution - Common::Point(1, 1);

    _field_54c = _resolution.x / 2;
    _field_550 = _resolution.y / 2;

    _field_554 = _resolution.x / 2;
    _field_558 = _resolution.y / 2;
    
    return true;
}

size_t GFXEngine::func0(IDVList &stak)
{
    System::IniConf::ReadFromNucleusIni();

    if ( !windd_func0(stak) )
        return 0;

    _dither = System::IniConf::GfxDither.Get<bool>();
    _filter = System::IniConf::GfxFilter.Get<bool>();
    _antialias = System::IniConf::GfxAntialias.Get<bool>();
    _zbuf_when_tracy = System::IniConf::GfxZbufWhenTracy.Get<bool>();
    _colorkey = System::IniConf::GfxColorkey.Get<bool>();

    if ( can_srcblend )
        _alpha = 192;
    else
        _alpha = 128;

    ApplyResolution();

    return 1;
}

void GFXEngine::ApplyResolution()
{
#ifdef __ANDROID__
    _corrW = (4.0f/3.0f) / (float(_resolution.x)/_resolution.y);
    _corrH = 1.0f; _corrIW=1.0f/_corrW; _corrIH=1.0f;
    initPolyEngine(); return;
#endif
    if ( (float)_resolution.x / (float)_resolution.y >= 1.4 )
    {
        int half = (_resolution.x + _resolution.y) / 2;
        _corrW = (float)half * 1.1429 / (float)_resolution.x;
        _corrH = (float)half * 0.85715 / (float)_resolution.y;
        _corrIW = 1.0 / _corrW;
        _corrIH = 1.0 / _corrH;
    }
    else //No correction
    {
        _corrIW = _corrW = 1.0;
        _corrIH = _corrH = 1.0;
    }
    
    initPolyEngine();
}


size_t GFXEngine::raster_func198(const Common::FLine &arg)
{
    float tX = _field_554 - 1.0;
    float tY = _field_558 - 1.0;

    int y1 = (arg.y1 + 1.0) * tY;
    int y2 = (arg.y2 + 1.0) * tY;
    int x1 = (arg.x1 + 1.0) * tX;
    int x2 = (arg.x2 + 1.0) * tX;

    DrawLine(Screen(),
                      Common::Line(x1, y1, x2, y2),
                      _field_4.r,
                      _field_4.g,
                      _field_4.b);
    return 1;
}


size_t GFXEngine::raster_func199(const Common::Line &arg)
{
    DrawLine(Screen(),
                      Common::Line(_field_54c + arg.x1, _field_550 + arg.y1,
                                   _field_54c + arg.x2, _field_550 + arg.y2),
                      _field_4.r,
                      _field_4.g,
                      _field_4.b );

    return 1;
}

void GFXEngine::sub_420EDC(Common::Line line, uint8_t r, uint8_t g, uint8_t b)
{
    if ( line.ClipBy(_clip) )
    {
        Common::Line tmp2 = line;

        if ( _inverseClip.IsEmpty() || !tmp2.ClipBy(_inverseClip) )
        {
            DrawLine(Screen(), line, r, g, b);
        }
        else
        {
            if ( tmp2.P2() != line.P2() )
            {
                if ( tmp2.P1() != line.P1() )
                {
                    DrawLine(Screen(), Common::Line(line.P1(), tmp2.P1()), r, g, b);
                    DrawLine(Screen(), Common::Line(tmp2.P2(), line.P2()), r, g, b);
                }
                else
                {
                    DrawLine(Screen(), Common::Line(tmp2.P2(), line.P2()), r, g, b);
                }
            }
            else
            {
                DrawLine(Screen(), Common::Line(line.P1(), tmp2.P1()), r, g, b);
            }
        }
    }
}

size_t GFXEngine::raster_func200(const Common::FLine &arg)
{
    float tX = _field_554 - 1.0;
    float tY = _field_558 - 1.0;

    sub_420EDC (Common::Line( (arg.x1 + 1.0) * tX
                            , (arg.y1 + 1.0) * tY
                            , (arg.x2 + 1.0) * tX
                            , (arg.y2 + 1.0) * tY),
                        _field_4.r,
                        _field_4.g,
                        _field_4.b);

    return 1;
}

size_t GFXEngine::raster_func201(const Common::Line &l)
{
    sub_420EDC( Common::Line( _field_54c + l.x1
                            , _field_550 + l.y1
                            , _field_54c + l.x2
                            , _field_550 + l.y2),
                        _field_4.r,
                        _field_4.g,
                        _field_4.b );

    return 1;
}

size_t GFXEngine::raster_func202(rstr_arg204 *arg)
{
    Common::Rect r1;
    r1.left   = (arg->float4.left   + 1.0) * (arg->pbitm->width / 2);
    r1.top    = (arg->float4.top    + 1.0) * (arg->pbitm->height / 2);
    r1.right  = (arg->float4.right  + 1.0) * (arg->pbitm->width / 2);
    r1.bottom = (arg->float4.bottom + 1.0) * (arg->pbitm->height / 2);

    Common::Rect r2;
    r2.left   = _field_554 * (arg->float14.left   + 1.0);
    r2.top    = _field_558 * (arg->float14.top    + 1.0);
    r2.right  = _field_554 * (arg->float14.right  + 1.0);
    r2.bottom = _field_558 * (arg->float14.bottom + 1.0);
    
    SDL_Rect src = r1;
    SDL_Rect dst = r2;
    
#ifdef __ANDROID__
    // A whole bitmap stretched over the whole logical screen is a shell background.
    if (src.x <= 0 && src.y <= 0 && src.w >= arg->pbitm->width - 1 && src.h >= arg->pbitm->height - 1 &&
        dst.x <= 0 && dst.y <= 0 && dst.x + dst.w >= _resolution.x - 1 && dst.y + dst.h >= _resolution.y - 1)
        NoteAndroidBackground(arg->pbitm->swTex);
#endif
    SDL_BlitScaled(arg->pbitm->swTex, &src, Screen(), &dst);
    
    return 1;
}

size_t GFXEngine::raster_func204(rstr_arg204 *arg)
{   
    Common::Rect r1;
    r1.left   = (arg->float4.left   + 1.0) * (arg->pbitm->width / 2);
    r1.top    = (arg->float4.top    + 1.0) * (arg->pbitm->height / 2);
    r1.right  = (arg->float4.right  + 1.0) * (arg->pbitm->width / 2);
    r1.bottom = (arg->float4.bottom + 1.0) * (arg->pbitm->height / 2);

    Common::Rect r2;
    r2.left   = _field_554 * (arg->float14.left   + 1.0);
    r2.top    = _field_558 * (arg->float14.top    + 1.0);
    r2.right  = _field_554 * (arg->float14.right  + 1.0);
    r2.bottom = _field_558 * (arg->float14.bottom + 1.0);
    
#ifdef __ANDROID__
    // Briefing/debriefing backgrounds are drawn through this clipped blit.
    if ( r1.left <= 0 && r1.top <= 0 && r1.right >= arg->pbitm->width - 1 && r1.bottom >= arg->pbitm->height - 1 &&
         r2.left <= 0 && r2.top <= 0 && r2.right >= _resolution.x - 1 && r2.bottom >= _resolution.y - 1 )
        NoteAndroidBackground(arg->pbitm->swTex, 1);
#endif
    if ( _clip.IsIntersects(r2) )
    {
        if ( r2.left < _clip.left )
        {
            r1.left += (_clip.left - r2.left) * r1.Width() / r2.Width();
            r2.left = _clip.left;
        }

        if ( r2.right > _clip.right )
        {
            r1.right += (_clip.right - r2.right) * r1.Width() / r2.Width();
            r2.right = _clip.right;
        }

        if ( r2.top < _clip.top )
        {
            r1.top += (_clip.top - r2.top) * r1.Height() / r2.Height();
            r2.top = _clip.top;
        }

        if ( r2.bottom > _clip.bottom )
        {
            r1.bottom += (_clip.bottom - r2.bottom) * r1.Height() / r2.Height();
            r2.bottom = _clip.bottom;
        }
    
        SDL_Rect src = r1;
        SDL_Rect dst = r2;
        SDL_BlitScaled(arg->pbitm->swTex, &src, Screen(), &dst);
    }

    return 1;
}

void GFXEngine::SetRenderStates(int setAll)
{
//    static const std::array<int, 4> blends = {GL_ZERO, GL_ONE, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA};
    GfxStates *newStates;
    
    if (setAll < 2)
        newStates = &_states;
    else
        newStates = &_lastStates;
    
    bool forceSetShader = false;
    if (setAll)
        forceSetShader = true;
    
    if (_glext)
    {
        if (setAll || (newStates->Prog.ID != _lastStates.Prog.ID))
        {
            if (_vbo && _lastStates.Prog.ID)
            {
                if (_lastStates.Prog.PosLoc != -1)
                    Glext::GLDisableVertexAttribArray(_lastStates.Prog.PosLoc);
                
                if (_lastStates.Prog.ColorLoc != -1)
                    Glext::GLDisableVertexAttribArray(_lastStates.Prog.ColorLoc);
                
                if (_lastStates.Prog.UVLoc != -1)
                    Glext::GLDisableVertexAttribArray(_lastStates.Prog.UVLoc);
            }
            
            Glext::GLUseProgram(newStates->Prog.ID);
            
            forceSetShader = true;
            
            if (_vbo && newStates->Prog.ID)
            {
                if (newStates->Prog.PosLoc != -1)
                    Glext::GLEnableVertexAttribArray(newStates->Prog.PosLoc);
                
                if (newStates->Prog.ColorLoc != -1)
                    Glext::GLEnableVertexAttribArray(newStates->Prog.ColorLoc);
            }
        }
    }
    
    if (_vbo)
    {        
        if (setAll || (newStates->DataBuf != _lastStates.DataBuf))
        {
            Glext::GLBindBuffer(GL_ARRAY_BUFFER, newStates->DataBuf);
        }
        
        if (setAll || (newStates->IndexBuf != _lastStates.IndexBuf))
        {
            Glext::GLBindBuffer(GL_ELEMENT_ARRAY_BUFFER, newStates->IndexBuf);
        }
        
        if (forceSetShader || (newStates->Tex != _lastStates.Tex))
        {
            if (newStates->Tex)
            {
                if (newStates->Prog.UVLoc != -1)
                    Glext::GLEnableVertexAttribArray(newStates->Prog.UVLoc);
                
                glBindTexture(GL_TEXTURE_2D, newStates->Tex);

                _vboStatesBlock.Textured = 1;
            }
            else
            {
                if (newStates->Prog.UVLoc != -1)
                    Glext::GLDisableVertexAttribArray(newStates->Prog.UVLoc);
                
                glBindTexture(GL_TEXTURE_2D, 0);

                _vboStatesBlock.Textured = 0;
            }
            _vboStatesChanged = true;
        }
        
        if ((forceSetShader || (newStates->Fog != _lastStates.Fog) ||
            (newStates->FogLength != _lastStates.FogLength) ||
            (newStates->FogStart != _lastStates.FogStart)) )
        {
            if (newStates->Fog)
            {
                _vboStatesBlock.Fog = 1.0;
                _vboStatesBlock.FogStart = newStates->FogStart;
                _vboStatesBlock.FogLength = newStates->FogLength;
            }
            else
            {
                _vboStatesBlock.Fog = 0.0;
            }
            _vboStatesChanged = true;
        }
        
        if ((forceSetShader || (newStates->AFog != _lastStates.AFog) ||
            (newStates->AFogLength != _lastStates.AFogLength) ||
            (newStates->AFogStart != _lastStates.AFogStart)) )
        {
            if (newStates->AFog)
            {
                _vboStatesBlock.AFog = 1.0;
                _vboStatesBlock.AFogStart = newStates->AFogStart;
                _vboStatesBlock.AFogLength = newStates->AFogLength;
            }
            else
            {
                _vboStatesBlock.AFog = 0.0;
            }
            _vboStatesChanged = true;
        }
        
        if (forceSetShader || (newStates->Shaded != _lastStates.Shaded))
        {
            if (newStates->Shaded)
            {
                _vboStatesBlock.Flat = 0;
            }
            else
            {
                _vboStatesBlock.Flat = 1;
            }
            _vboStatesChanged = true;
        }
        
        if (setAll || (newStates->AlphaTest != _lastStates.AlphaTest))
        {
            if (newStates->AlphaTest == false)
            {
                _vboStatesBlock.ATest = 0;
            }
            else
            {
                _vboStatesBlock.ATest = 1;
            }
            _vboStatesChanged = true;
        }
    }
    else
    {
        if (setAll || (newStates->Stipple != _lastStates.Stipple))
        {
        }

        if (setAll || (newStates->Fog != _lastStates.Fog) ||
            (newStates->FogLength != _lastStates.FogLength) ||
            (newStates->FogStart != _lastStates.FogStart) )
        {
            if (newStates->Fog)
            {
                glEnable(GL_FOG);
                glFogf(GL_FOG_DENSITY, 1.0);
                glFogi(GL_FOG_MODE, GL_LINEAR);
                glHint(GL_FOG_HINT, GL_DONT_CARE);

                float fcolors[4] = {0.0, 0.0, 0.0, 1.0};

                glFogfv(GL_FOG_COLOR, fcolors);
                
                glFogf(GL_FOG_START, newStates->FogStart);
                glFogf(GL_FOG_END, newStates->FogStart + newStates->FogLength);
            }
            else
            {
                glDisable(GL_FOG);
            }
        }
        
        if (setAll || (newStates->Shaded != _lastStates.Shaded))
        {
            if (newStates->Shaded)
                glShadeModel(GL_SMOOTH);
            else
                glShadeModel(GL_FLAT);
        }
        
        if (setAll || (newStates->Tex != _lastStates.Tex))
        {
            if (newStates->Tex)
            {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, newStates->Tex);
                glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            }
            else
            {
                glDisable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, 0);
                glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            }
        }
        
        if (setAll || (newStates->AlphaTest != _lastStates.AlphaTest))
        {
            if (newStates->AlphaTest == false)
            {
                glDisable(GL_ALPHA_TEST);
            }
            else
            {
                glEnable(GL_ALPHA_TEST);
                glAlphaFunc(GL_GREATER, 0.0);
            }
        }
    }
    
    if (setAll || (newStates->DepthTest != _lastStates.DepthTest))
    {
        if (newStates->DepthTest)
        {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
        }
        else
        {
            glDisable(GL_DEPTH_TEST);
        }
    }

    if (setAll || (newStates->Tex != _lastStates.Tex)
               || (newStates->LinearFilter != _lastStates.LinearFilter) )
    {
        if (newStates->Tex)
        {
            if (newStates->LinearFilter)
            {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            }
            else
            {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            }
        }
    }
    
    if (setAll || (newStates->SrcBlend != _lastStates.SrcBlend)
                   || (newStates->DstBlend != _lastStates.DstBlend))
    {
        glBlendFunc(newStates->SrcBlend, newStates->DstBlend);
    }

    if (setAll || (newStates->TexBlend != _lastStates.TexBlend))
    {
        if (newStates->TexBlend == 0)
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        else if (newStates->TexBlend == 1)
        {
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
            glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
            glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
        }
        else if (newStates->TexBlend == 2)
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    }

    if (setAll || (newStates->AlphaBlend != _lastStates.AlphaBlend))
    {
        if (newStates->AlphaBlend)
            glEnable(GL_BLEND);
        else
            glDisable(GL_BLEND);
    }

    if (setAll || (newStates->Zwrite != _lastStates.Zwrite))
    {
        if (newStates->Zwrite)
            glDepthMask(GL_TRUE);
        else
            glDepthMask(GL_FALSE);
    }

    if (setAll < 2)
        _lastStates = _states;
}

void GFXEngine::SetupNodeStatesOld(TRenderNode *nod, bool &useComputedColor)
{
    TMesh *mesh = nod->Mesh;
    uint32_t flags = nod->Flags;
    useComputedColor = false;

    _states.Shaded = false;
    _states.Stipple = false;
    _states.SrcBlend = GL_ONE;
    _states.DstBlend = GL_ZERO;
    _states.TexBlend = 0; //REPLACE
    _states.AlphaBlend = false;
    _states.Zwrite = true;
    _states.Tex = 0;
    _states.LinearFilter = (_filter != 0);
    _states.Fog = false;
    _states.AlphaTest = false;
    _states.AFog = false;
    _states.Prog = TShaderProg();


    if ( flags & RFLAGS_TEXTURED )
    {
        if (nod->Tex)
            _states.Tex = nod->Tex->hwTex;
    }

    if ( flags & RFLAGS_SHADED )
    {
        _states.TexBlend = 2; //MODULATE
        _states.Shaded = true;
    }
    
    if ( flags & RFLAGS_FOG )
    {
        _states.Fog = true;
        _states.FogStart = nod->FogStart;
        _states.FogLength = nod->FogLength;
    }
    
    if ( flags & RFLAGS_LUMTRACY )
    {
        if ( !_zbuf_when_tracy )
            _states.Zwrite = false;

        if ( can_destblend )
        {
            _states.AlphaBlend = true;
            _states.TexBlend = 1; //MODULATEALPHA;
            _states.SrcBlend = GL_ONE;
            _states.DstBlend = GL_ONE;
            _states.Shaded = false;
        }
        else if ( can_srcblend )
        {
            _states.AlphaBlend = true;
            _states.TexBlend = 1; //MODULATEALPHA;
            _states.SrcBlend = GL_SRC_ALPHA;
            _states.DstBlend = GL_ONE_MINUS_SRC_ALPHA;
            _states.Shaded = false;
        }
        else if ( can_stippling )
        {
            _states.AlphaBlend = true;
            _states.TexBlend = 1; //MODULATEALPHA;
            _states.SrcBlend = GL_SRC_ALPHA;//D3DBLEND_SRCALPHA;, 
            _states.DstBlend = GL_ONE_MINUS_SRC_ALPHA;//D3DBLEND_INVSRCALPHA;
            _states.Stipple = true;
            _states.Shaded = false;
        }
    }
    else if ( flags & RFLAGS_ZEROTRACY )
    {
        _states.AlphaTest = true;        

        if ( _pixfmt->BytesPerPixel != 1 )
        {
            _states.SrcBlend = GL_SRC_ALPHA;//D3DBLEND_SRCALPHA;, 
            _states.DstBlend = GL_ONE_MINUS_SRC_ALPHA;//D3DBLEND_INVSRCALPHA;
        }

        _states.AlphaBlend = true;
        _states.LinearFilter = false;
        _states.TexBlend = 2; //MODULATE
    }

    if (flags & RFLAGS_SKY)
    {
        _states.Fog = false;
        _states.Shaded = true;
    }
    else if (flags & RFLAGS_FALLOFF)
    {
        const float transDist = _frameSkyDistance;
        const float transLen = _frameSkyLength;
        const mat4x4 &tf = nod->TForm;

        // Conservative early-out: if every corner of the bounding box is inside
        // the fade distance, no vertex changes and the plain colours are used.
        bool mayFade = true;
        if (mesh->BoundBox.Inited)
        {
            const vec3d &mn = mesh->BoundBox.Min, &mx = mesh->BoundBox.Max;
            float far2 = 0.0;
            for (int c = 0; c < 8; ++c)
            {
                vec3d corner((c & 1) ? mx.x : mn.x, (c & 2) ? mx.y : mn.y, (c & 4) ? mx.z : mn.z);
                vec3d t = tf.Transform(corner);
                far2 = std::max(far2, float(t.x * t.x + t.z * t.z));
            }
            mayFade = far2 > transDist * transDist;
        }

        if (mayFade)
        {
            // Only X and Z are needed; compare squared distances so sqrt is
            // taken only for vertices that actually fade.
            const float a0 = tf.m00, a1 = tf.m01, a2 = tf.m02, a3 = tf.m03;
            const float c0 = tf.m20, c1 = tf.m21, c2 = tf.m22, c3 = tf.m23;
            const float td2 = transDist * transDist;
            const float invLen = transLen > 0.0f ? 1.0f / transLen : 0.0f;
            for(TVertex &v : mesh->Vertexes)
            {
                v.ComputedColor = v.Color;
                const float x = a0 * v.Pos.x + a1 * v.Pos.y + a2 * v.Pos.z + a3;
                const float z = c0 * v.Pos.x + c1 * v.Pos.y + c2 * v.Pos.z + c3;
                const float d2 = x * x + z * z;
                if (d2 > td2)
                {
                    float prc = (std::sqrt(d2) - transDist) * invLen;
                    if (prc > 1.0f) prc = 1.0f;
                    v.ComputedColor.a *= (1.0f - prc);
                    useComputedColor = true;
                }
            }
        }

        if (useComputedColor)
        {
            _states.Zwrite = true;
            _states.AlphaBlend = true;
            _states.TexBlend = 2; // MODULATE
            _states.SrcBlend = GL_SRC_ALPHA;

            if (flags & RFLAGS_LUMTRACY)
                _states.DstBlend = GL_ONE;
            else
                _states.DstBlend = GL_ONE_MINUS_SRC_ALPHA;

            _states.Shaded = true;
            _states.Stipple = false;
        }
    }
    
    if (flags & RFLAGS_COMPUTED_COLOR)
        useComputedColor = true;
    
    if (flags & RFLAGS_DISABLE_ZWRITE)
        _states.Zwrite = false;

}

void GFXEngine::RenderingMeshOld(TRenderNode *nod)
{
    if ( !_sceneBeginned )
        return;
    
    if (!nod)
        return;
    
    TMesh *mesh = nod->Mesh;
    
    if (!mesh)
        return;
    
    uint32_t flags = nod->Flags;
    bool useComputedColor = false;
    SetupNodeStatesOld(nod, useComputedColor);

    SetRenderStates(0);

    SetModelViewMatrix( nod->TForm );
  
    glVertexPointer(3, GL_FLOAT, sizeof(TVertex), &mesh->Vertexes[0].Pos);
    
    if (useComputedColor)
        glColorPointer(4, GL_FLOAT, sizeof(TVertex), &mesh->Vertexes[0].ComputedColor);
    else
        glColorPointer(4, GL_FLOAT, sizeof(TVertex), &mesh->Vertexes[0].Color);
    
    if (flags & RFLAGS_TEXTURED)
    {
        if ( (flags & RFLAGS_DYNAMIC_TEXTURE) && nod->coordsID >= 0 )
            glTexCoordPointer(2, GL_FLOAT, sizeof(tUtV), nod->Mesh->CoordsCache.at( nod->coordsID ).Coords.data());
        else
            glTexCoordPointer(2, GL_FLOAT, sizeof(TVertex), &mesh->Vertexes[0].TexCoord);
    }
    
    glDrawElements(GL_TRIANGLES, mesh->Indixes.size(), GLINDEXTYPE, mesh->Indixes.data());
}

void GFXEngine::RenderingMesh(TRenderNode *nod)
{
    if ( !_sceneBeginned )
        return;
    
    if (!nod)
        return;
    
    TMesh *mesh = nod->Mesh;
    
    if (!mesh)
        return;
    
    uint32_t flags = nod->Flags;

    _states.Shaded = false;
    _states.Stipple = false;
    _states.SrcBlend = GL_ONE;
    _states.DstBlend = GL_ZERO;
    _states.TexBlend = 0; //REPLACE
    _states.AlphaBlend = false;
    _states.Zwrite = true;
    _states.Tex = 0;
    _states.LinearFilter = (_filter != 0);
    _states.Fog = false;
    _states.AlphaTest = false;
    _states.AFog = false;
    _states.Prog = _stdShaderProg;
    
    if ( flags & RFLAGS_TEXTURED )
    {
        if (nod->Tex)
            _states.Tex = nod->Tex->hwTex;
    }

    if ( flags & RFLAGS_SHADED )
    {
        _states.TexBlend = 2; //MODULATE
        _states.Shaded = true;
    }
    
    if ( flags & RFLAGS_FOG )
    {
        _states.Fog = true;
        _states.FogStart = nod->FogStart;
        _states.FogLength = nod->FogLength;
    }
    
    if ( flags & RFLAGS_LUMTRACY )
    {
        if ( !_zbuf_when_tracy )
            _states.Zwrite = false;

        if ( can_destblend )
        {
            _states.AlphaBlend = true;
            _states.TexBlend = 1; //MODULATEALPHA;
            _states.SrcBlend = GL_ONE;
            _states.DstBlend = GL_ONE;
            _states.Shaded = false;
        }
        else if ( can_srcblend )
        {
            _states.AlphaBlend = true;
            _states.TexBlend = 1; //MODULATEALPHA;
            _states.SrcBlend = GL_SRC_ALPHA;
            _states.DstBlend = GL_ONE_MINUS_SRC_ALPHA;
            _states.Shaded = false;
        }
        else if ( can_stippling )
        {
            _states.AlphaBlend = true;
            _states.TexBlend = 1; //MODULATEALPHA;
            _states.SrcBlend = GL_SRC_ALPHA;//D3DBLEND_SRCALPHA;, 
            _states.DstBlend = GL_ONE_MINUS_SRC_ALPHA;//D3DBLEND_INVSRCALPHA;
            _states.Stipple = true;
            _states.Shaded = false;
        }
    }
    else if ( flags & RFLAGS_ZEROTRACY )
    {
        _states.AlphaTest = true;        

        if ( _pixfmt->BytesPerPixel != 1 )
        {
            _states.SrcBlend = GL_SRC_ALPHA;//D3DBLEND_SRCALPHA;, 
            _states.DstBlend = GL_ONE_MINUS_SRC_ALPHA;//D3DBLEND_INVSRCALPHA;
        }

        _states.AlphaBlend = true;
        _states.LinearFilter = false;
        _states.TexBlend = 2; //MODULATE
    }

    if (flags & RFLAGS_SKY)
    {
        _states.Fog = false;
        _states.Shaded = true;
    }
    else if (flags & RFLAGS_FALLOFF)
    {
        _states.Zwrite = true;
        _states.AlphaBlend = true;
        _states.TexBlend = 2; // MODULATE
        _states.SrcBlend = GL_SRC_ALPHA;

        if (flags & RFLAGS_LUMTRACY)
            _states.DstBlend = GL_ONE;
        else
            _states.DstBlend = GL_ONE_MINUS_SRC_ALPHA;

        _states.Shaded = true;
        _states.Stipple = false;
        
        _states.AFog = true; 
        _states.AFogStart = System::IniConf::GfxSkyDistance.Get<int>();
        _states.AFogLength = System::IniConf::GfxSkyLength.Get<int>();
    }
    
    if (flags & RFLAGS_DISABLE_ZWRITE)
        _states.Zwrite = false;

    _states.DataBuf = nod->Mesh->glDataBuf;
    _states.IndexBuf = nod->Mesh->glIndexBuf;
    
    SetRenderStates(0);
    
    SetModelViewMatrix(nod->TForm);

    Glext::GLVertexAttribPointer(_lastStates.Prog.PosLoc, 3, GL_FLOAT, GL_FALSE,  sizeof(TVertex), (void *)offsetof(TVertex, Pos));
    
    if (flags & RFLAGS_COMPUTED_COLOR)
        Glext::GLVertexAttribPointer(_lastStates.Prog.ColorLoc, 4, GL_FLOAT, GL_FALSE,  sizeof(TVertex), (void *)offsetof(TVertex, ComputedColor));
    else
        Glext::GLVertexAttribPointer(_lastStates.Prog.ColorLoc, 4, GL_FLOAT, GL_FALSE,  sizeof(TVertex), (void *)offsetof(TVertex, Color));
    
    if (flags & RFLAGS_TEXTURED)
    {
        if ( (flags & RFLAGS_DYNAMIC_TEXTURE) && nod->coordsID >= 0 )
            Glext::GLVertexAttribPointer(_lastStates.Prog.UVLoc, 2, GL_FLOAT, GL_FALSE,  sizeof(tUtV), (void *)(size_t)nod->Mesh->CoordsCache.at( nod->coordsID ).BufferPos);
        else
            Glext::GLVertexAttribPointer(_lastStates.Prog.UVLoc, 2, GL_FLOAT, GL_FALSE,  sizeof(TVertex), (void *)offsetof(TVertex, TexCoord));
    }
    
    CommitUBOParameters();    
    glDrawElements(GL_TRIANGLES, mesh->Indixes.size(), GLINDEXTYPE, NULL);
}

void GFXEngine::RenderNode(TRenderNode *node)
{
    if (!node)
        return;
    
    switch(node->Type)
    {
        case TRenderNode::TYPE_MESH:
            if (_vbo)
                RenderingMesh(node);
            else
                RenderingMeshOld(node);
            break;
            
        case TRenderNode::TYPE_PARTICLE:
        {
            if (_vbo)
                RenderingMesh(node);
            else
                RenderingMeshOld(node);
        }
            break;
            
        default:
            break;
    }
}

void GFXEngine::QueueRenderMesh(TRenderNode *nod)
{
    if (!nod)
        return;
    
    TMesh *mesh = nod->Mesh;    
    if (!mesh)
        return;
    
    uint32_t flags = nod->Flags;
    
    if (flags & RFLAGS_SKY)
        _renderSkyBoxList.push_back(nod);
    else if (flags & RFLAGS_ZEROTRACY)
        _renderZeroTracyList.push_back(nod);
    else if (flags & RFLAGS_LUMTRACY)
        _renderLumaTracyList.push_back(nod);
    else
        _renderSolidList.push_back(nod);
}

static bool ParticleBatchable(const TRenderNode *a, const TRenderNode *b)
{
    return a->Type == TRenderNode::TYPE_PARTICLE && b->Type == TRenderNode::TYPE_PARTICLE &&
           a->Mesh && b->Mesh && a->Tex == b->Tex && a->Flags == b->Flags &&
           a->FogStart == b->FogStart && a->FogLength == b->FogLength &&
           a->Mesh->Indixes.size() == 6 && b->Mesh->Indixes.size() == 6 &&
           a->Mesh->Vertexes.size() == 4 && b->Mesh->Vertexes.size() == 4;
}

void GFXEngine::RenderParticleBatchOld(TRenderNode **nodes, size_t count)
{
    // Many particles with identical render state become one draw call. Vertices
    // are pre-transformed into view space (each particle's TForm is only a scale
    // and a translation), so the result is identical to drawing them one by one
    // in the same order, without per-particle matrix/array/draw overhead.
    static std::vector<float> pos, col, uv;
    static std::vector<uint16_t> idx;
    const size_t maxPerBatch = 65536 / 4;

    while (count)
    {
        const size_t n = std::min(count, maxPerBatch);
        pos.resize(n * 12); col.resize(n * 16); uv.resize(n * 8); idx.resize(n * 6);

        for (size_t i = 0; i < n; ++i)
        {
            TRenderNode *nod = nodes[i];
            TMesh *mesh = nod->Mesh;
            const mat4x4 &tf = nod->TForm;
            const tUtV *coords = NULL;
            if ( (nod->Flags & RFLAGS_TEXTURED) && (nod->Flags & RFLAGS_DYNAMIC_TEXTURE) && nod->coordsID >= 0 )
                coords = mesh->CoordsCache.at( nod->coordsID ).Coords.data();

            for (int v = 0; v < 4; ++v)
            {
                const TVertex &vx = mesh->Vertexes[v];
                vec3d p = tf.Transform( vec3d(vx.Pos.x, vx.Pos.y, vx.Pos.z) );
                float *pp = &pos[(i * 4 + v) * 3];
                pp[0] = p.x; pp[1] = p.y; pp[2] = p.z;
                float *cc = &col[(i * 4 + v) * 4];
                const TGLColor &vc = (nod->Flags & RFLAGS_COMPUTED_COLOR) ? vx.ComputedColor : vx.Color;
                cc[0] = vc.r; cc[1] = vc.g; cc[2] = vc.b; cc[3] = vc.a;
                const tUtV &t = coords ? coords[v] : vx.TexCoord;
                uv[(i * 4 + v) * 2 + 0] = t.tu;
                uv[(i * 4 + v) * 2 + 1] = t.tv;
            }
            for (int k = 0; k < 6; ++k)
                idx[i * 6 + k] = uint16_t(i * 4 + mesh->Indixes[k]);
        }

        bool useComputedColor = false;
        SetupNodeStatesOld(nodes[0], useComputedColor);
        SetRenderStates(0);
        SetModelViewMatrix( mat4x4() );

        glVertexPointer(3, GL_FLOAT, 0, pos.data());
        glColorPointer(4, GL_FLOAT, 0, col.data());
        if (nodes[0]->Flags & RFLAGS_TEXTURED)
            glTexCoordPointer(2, GL_FLOAT, 0, uv.data());
        glDrawElements(GL_TRIANGLES, GLsizei(n * 6), GL_UNSIGNED_SHORT, idx.data());

        nodes += n;
        count -= n;
    }
}

void GFXEngine::RenderQueue(std::vector<TRenderNode *> &queue, bool batchParticles)
{
    const size_t total = queue.size();
    for (size_t i = 0; i < total; )
    {
        TRenderNode *node = queue[i];
        size_t j = i + 1;
        if (batchParticles && !_vbo && node && node->Type == TRenderNode::TYPE_PARTICLE)
            while (j < total && queue[j] && ParticleBatchable(node, queue[j]))
                ++j;

        if (j - i > 1)
        {
            if (_sceneBeginned)
                RenderParticleBatchOld(&queue[i], j - i);
        }
        else
            RenderNode(node);
        i = j;
    }
    queue.clear();
}

static bool IsCommutativeParticle(const TRenderNode *n, bool zbufWhenTracy, bool destBlend)
{
    // Additive (ONE, ONE) particles without depth writes produce the same image
    // in any order, so a run of them may be regrouped by state for batching.
    return n->Type == TRenderNode::TYPE_PARTICLE && destBlend && !zbufWhenTracy &&
           (n->Flags & RFLAGS_LUMTRACY) &&
           !(n->Flags & (RFLAGS_FALLOFF | RFLAGS_SKY | RFLAGS_ZEROTRACY));
}

void GFXEngine::Rasterize(uint32_t RasterEtapes)
{
    if (RasterEtapes & RASTER_SKY)
    {
        std::stable_sort(_renderSkyBoxList.begin(), _renderSkyBoxList.end(), TRenderNode::CompareSolid);
        RenderQueue(_renderSkyBoxList, false);
    }
    
    if (RasterEtapes & RASTER_SOLID)
    {
        std::stable_sort(_renderSolidList.begin(), _renderSolidList.end(), TRenderNode::CompareSolid);
        RenderQueue(_renderSolidList, true);
    }
    
    if (RasterEtapes & RASTER_ZEROTR)
    {
        std::stable_sort(_renderZeroTracyList.begin(), _renderZeroTracyList.end(), TRenderNode::CompareSolid);
        RenderQueue(_renderZeroTracyList, true);
    }
    
    if (RasterEtapes & RASTER_LUMATR)
    {
        std::stable_sort(_renderLumaTracyList.begin(), _renderLumaTracyList.end(), TRenderNode::CompareTransparent);
        
        if (!_vbo)
        {
            // Inside each back-to-front run of order-independent additive
            // particles, group equal states together so they batch.
            auto &q = _renderLumaTracyList;
            const bool zb = _zbuf_when_tracy != 0, db = can_destblend != 0;
            for (size_t i = 0; i < q.size(); )
            {
                if (!q[i] || !IsCommutativeParticle(q[i], zb, db)) { ++i; continue; }
                size_t j = i + 1;
                while (j < q.size() && q[j] && IsCommutativeParticle(q[j], zb, db)) ++j;
                if (j - i > 2)
                    std::stable_sort(q.begin() + i, q.begin() + j, TRenderNode::CompareSolid);
                i = j;
            }
        }
        RenderQueue(_renderLumaTracyList, true);
    }
}


void GFXEngine::raster_func207(int id, TileMap *t)
{
    _tiles[id] = t;
}

TileMap *GFXEngine::raster_func208(int id)
{
    return _tiles[id];
}

int GFXEngine::raster_func208(TileMap *t)
{
    if ( t )
    {
        for (int i = 0; i < 256; i++)
        {
            if (_tiles[i] == t)
                return i;
        }
    }
    return -1;
}

void GFXEngine::ProcessDrawSeq(const CmdStream &drawSeq, const CmdIncludes *includes)
{
    struct CmdStkEntr
    {
        const CmdStream &seq;
        const int32_t pos;
    };
    
    int v11;

    int bytesPerColor = Screen()->format->BytesPerPixel;

    int32_t curPos = 0;
    const CmdStream *curStream = &drawSeq;
    
    int w_pixels = Screen()->pitch / bytesPerColor;
    TileMap *tile = NULL;

    int x_out = 0;
    int y_out = 0;

    int x_out_txt = 0;
    int y_out_txt = 0;
    int txt_flag = 0;

    int y_pos_line = 0;
    int x_pos_line = 0;

    int halfWidth = _resolution.x / 2;
    int halfHeight = _resolution.y / 2;

    int line_width = 0;
    int line_height = 0;

    //if v11 = 0 - clone first column of tile  (count = line_width)
    //if v11 = 1 - normal copy of tile
    v11 = 1;


    int x_off = 0;
    int y_off = 0;

    std::stack<CmdStkEntr> Stack;

    while ( 1 )
    {
        int v13 = FontUA::get_u8(*curStream, &curPos);

        if ( v13 )
        {
            Common::PointRect &chrr = tile->map[v13];

            int cpy_width;

            if ( line_width )
                cpy_width = line_width - x_off;
            else
                cpy_width = chrr.w - x_off;

            int cpy_height = line_height - y_off;

            //SDL_Rect srcR, dstR;
            Common::Rect dstR(x_out, y_out, x_out + cpy_width, y_out + cpy_height);            
            /*dstR.x = x_out;
            dstR.y = y_out;
            dstR.w = cpy_width;
            dstR.h = cpy_height;*/
            
            Common::Rect srcR;
            srcR.left = chrr.x + x_off;
            srcR.top = chrr.y + y_off;
            srcR.bottom = chrr.y + y_off + cpy_height;
            
            if (v11)
                srcR.right = chrr.x + x_off + cpy_width;
            else
                srcR.right = chrr.x + x_off + 1;
            
            DrawFill(tile->img->GetSwTex(), srcR, Screen(), dstR);
                    
            /*srcR.h = cpy_height;
            
            if (v11)
                srcR.w = cpy_width;
            else
                srcR.w = 1;
            
            for(int i = 0; i < cpy_width; i += srcR.w)
            {
                SDL_BlitSurface(tile->img->GetSwTex(), &srcR, Screen(), &dstR);
                dstR.x += srcR.w;
            }*/     

            line_width = 0;
            x_off = 0;
            x_out += cpy_width;
            v11 = 1;
        }
        else // 0
        {
            int opcode = FontUA::get_u8(*curStream, &curPos);

            switch ( opcode )
            {
            case 0: // End
                
                if (Stack.empty())
                {
                    DrawScreenText();
                    return;
                }
                
                curPos = Stack.top().pos;
                curStream = &Stack.top().seq;
                
                Stack.pop();
                break;

            case 1: // x pos from center
                x_out = halfWidth + FontUA::get_s16(*curStream, &curPos);
                x_pos_line = x_out;

                y_pos_line = y_out;
                y_off = 0;

                line_height = tile->h;
                break;

            case 2: // y pos from center
                y_out = halfHeight + FontUA::get_s16(*curStream, &curPos);
                x_pos_line = x_out;

                y_pos_line = y_out;
                y_off = 0;

                line_height = tile->h;
                break;

            case 3: //xpos
                x_out = FontUA::get_s16(*curStream, &curPos);
                if ( x_out < 0 )
                    x_out += w_pixels;

                x_pos_line = x_out;
                y_pos_line = y_out;

                line_height = tile->h;
                y_off = 0;
                break;

            case 4: //ypos
                y_out = FontUA::get_s16(*curStream, &curPos);
                if ( y_out < 0 )
                    y_out += _resolution.y;

                x_pos_line = x_out;
                y_pos_line = y_out;

                line_height = tile->h;
                y_off = 0;
                break;

            case 5: //add to x pos
                x_out += FontUA::get_s16(*curStream, &curPos);
                break;

            case 6: //add to y pos
                y_out += FontUA::get_s16(*curStream, &curPos);
                break;

            case 7: //next line
                y_out = (line_height - y_off) + y_pos_line;
                y_pos_line = y_out;
                x_out = x_pos_line;

                y_off = 0;
                line_height = tile->h;
                break;

            case 8: // Select tileset
                tile = _tiles[FontUA::get_u8(*curStream, &curPos)];
                break;

            case 9: // Include another cmdlist source
            {
                int azaza = FontUA::get_u8(*curStream, &curPos);
                Stack.push( {*curStream, curPos} );
                curPos = 0;
                curStream = includes->at(azaza);
            }
            break;

            case 10:
                line_width = FontUA::get_u8(*curStream, &curPos);

                v11 = 0;
                x_off = 0;

                break;

            case 11:

                line_width = FontUA::get_u8(*curStream, &curPos);

                v11 = 0;
                x_off = 0;

                line_width -= (x_out - x_pos_line);
                break;

            case 12: // Set x offset
                x_off = FontUA::get_u8(*curStream, &curPos);
                break;

            case 13: // Set x width
                line_width = FontUA::get_u8(*curStream, &curPos);
                break;

            case 14: // Set y offset
                y_off = FontUA::get_u8(*curStream, &curPos);
                break;

            case 15: // Set y height
                line_height = FontUA::get_u8(*curStream, &curPos);
                break;

            case 16: // Full reset tileset
                tile = _tiles[FontUA::get_u8(*curStream, &curPos)];
                line_height = tile->h;
                y_off = 0;
                break;

            case 17:
                line_width = FontUA::get_s16(*curStream, &curPos);
                v11 = 0;
                x_off = 0;
                line_width -= (x_out - x_pos_line);
                break;

            case 18: // Add text
            {
                int block_width = FontUA::get_s16(*curStream, &curPos);
                int flag = txt_flag | FontUA::get_u16(*curStream, &curPos);
                
                int32_t sz = FontUA::get_u16(*curStream, &curPos);

                std::string txt;
                txt.assign((const char *)(curStream->data()) + curPos, sz);
                

                curPos += sz + 1;
                AddScreenText(txt, x_out_txt, y_out_txt, block_width, tile->h, flag);
            }
            break;

            case 19: // Copy current x/y pos for text output
                x_out_txt = x_out;
                y_out_txt = y_out;
                break;

            case 20: // Add txtout flag
                txt_flag |= FontUA::get_u16(*curStream, &curPos);
                break;

            case 21: // Delete txtout flag
                txt_flag &= ~(FontUA::get_u16(*curStream, &curPos));
                break;

            case 22: // set color for font
            {
                int r = FontUA::get_u16(*curStream, &curPos);

                int g = FontUA::get_u16(*curStream, &curPos);

                int b = FontUA::get_u16(*curStream, &curPos);

                AddScreenText("", r, g, b, 0, 0x20);
            }
            break;
            }
        }
    }
}


void GFXEngine::raster_func210(const Common::FRect &arg)
{
    _clip = Common::Rect(   (arg.left + 1.0) * (_field_554 + -1.0),
                            (arg.top + 1.0) * (_field_558 + -1.0),
                            (arg.right + 1.0) * (_field_554 + -1.0),
                            (arg.bottom + 1.0) * (_field_558 + -1.0) );
    }

void GFXEngine::raster_func211(const Common::Rect &arg)
{
    _clip = Common::Rect(   _field_54c + arg.left,
                            _field_550 + arg.top,
                            _field_54c + arg.right,
                            _field_550 + arg.bottom );
}


void GFXEngine::BeginScene()
{
    SetRenderStates(2);
    
    SetProjectionMatrix( _frustum );
    SetModelViewMatrix( mat4x4f() );
    
    _sceneBeginned = 1;
}

// Draw transparent
void GFXEngine::EndScene()
{
    _sceneBeginned = 0;
    
    _renderSkyBoxList.clear();
    _renderSolidList.clear();
    _renderZeroTracyList.clear();
    _renderLumaTracyList.clear();
    
    _renderNodesCache.Rewind();
}

size_t GFXEngine::raster_func217(SDL_Color color)
{
    //if ( !ColorCmp(color, Color(255, 255, 255, 255) ) )
    _field_4 = color;

    return 0;
}

void GFXEngine::raster_func218(rstr_218_arg *arg)
{
    Common::Rect sRect( (arg->rect1.left + 1.0) * (arg->bitm_intern->width / 2),
                        (arg->rect1.top + 1.0) * (arg->bitm_intern->height / 2),
                        (arg->rect1.right + 1.0) * (arg->bitm_intern->width / 2),
                        (arg->rect1.bottom + 1.0) * (arg->bitm_intern->height / 2) );

    Common::Rect dRect( (arg->rect2.left + 1.0) * _field_554,
                        (arg->rect2.top + 1.0) * _field_558,
                        (arg->rect2.right + 1.0) * _field_554,
                        (arg->rect2.bottom + 1.0) * _field_558 );

    BlitScaleMasked(arg->bitm_intern->swTex, sRect, arg->bitm_intern2->swTex, arg->flg, Screen(), dRect);
}

void GFXEngine::raster_func221(const Common::Rect &arg)
{
    _inverseClip.left = _field_54c + arg.left;
    _inverseClip.top = _field_550 + arg.top;
    _inverseClip.right = _field_54c + arg.right;
    _inverseClip.bottom = _field_550 + arg.bottom;
}

void GFXEngine::BeginFrame()
{
    _frameNewSky = System::IniConf::GfxNewSky.Get<bool>();
    _frameSkyDistance = System::IniConf::GfxSkyDistance.Get<int>();
    _frameSkyLength = System::IniConf::GfxSkyLength.Get<int>();
    SDL_FillRect(Screen(), NULL, SDL_MapRGBA(Screen()->format, 0, 0, 0, 0) );
    
    Common::Point scrSz = System::GetResolution();
#ifdef __ANDROID__
    Android::SetViewport();
#else
    glViewport(0, 0, scrSz.x, scrSz.y);
#endif
    
    bool saved = _states.Zwrite;
    
    _states.Zwrite = true;
    SetRenderStates(0);
    
    if (_colorEffects)
    {
        Glext::GLBindFramebuffer(GL_FRAMEBUFFER, 0);
    
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
        Glext::GLBindFramebuffer(GL_FRAMEBUFFER, _fbo);
    }

    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    _states.Zwrite = saved;
    SetRenderStates(0);
    
    if (!_vbo)
    {
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
    }
    
    _states.Prog = _stdShaderProg;
}

void GFXEngine::EndFrame()
{       
    if (_colorEffects > 1)
    {
        Glext::GLBindFramebuffer(GL_FRAMEBUFFER, 0);   
        DrawFBO();
    }    

    Gui::Root::Instance.Draw(Screen());
#ifdef __ANDROID__
    // Android has no visible system pointer for joystick-generated movement.
    // Composite the original cursor into the logical UI before uploading it.
    static const char *names[] = {"Pointer", "Cancel", "Select", "Attack", "Goto", "Disk", "New", "Add", "Control", "Beam", "Build"};
    if (CurrentCursorID > 0 && CurrentCursorID <= 11) {
        auto found = androidCursors.find(names[CurrentCursorID - 1]);
        if (found != androidCursors.end() && found->second.image) {
            const Common::Point p = NC_STACK_winp::AndroidPointer();
            SDL_Rect dest = {p.x - found->second.hotX, p.y - found->second.hotY, 0, 0};
            SDL_SetClipRect(Screen(), nullptr);
            SDL_BlitSurface(found->second.image.get(), nullptr, Screen(), &dest);
        }
    }
    Android::DrawOverlay(Screen());
    SDL_ShowCursor(SDL_DISABLE);
    DrawAndroidWideFill();
#endif
    DrawScreenSurface();
#ifdef __ANDROID__
    DrawAndroidTouchOverlay();
#endif
    Gui::Root::Instance.HwCompose();
    
    if (_colorEffects == 1)
    {
        Glext::GLBindFramebuffer(GL_FRAMEBUFFER, 0);   
        DrawFBO();
    }
    
    System::Flip();
}

void GFXEngine::SetColorEffectsPowers(const std::vector<ColorFx> &arg)
{    
    if (arg.empty())
    {
        _normClr = vec3d(1.0, 1.0, 1.0);
        _invClr = vec3d(0.0, 0.0, 0.0);
    }
    else
    {
        _normClr = vec3d(0.0, 0.0, 0.0);
        _invClr = vec3d(0.0, 0.0, 0.0);

        for (ColorFx fx : arg)
        {
            switch(fx.Id)
            {
                case 4:
                case 5:
                case 7:
                    _invClr += _clrEff.at( fx.Id ) * fx.Pwr;
                    break;
                default:
                    _normClr += _clrEff.at( fx.Id ) * fx.Pwr;
                    break;
            }
        }
    }
}

bool GFXEngine::AllocTexture(ResBitmap *bitm)
{
    if (bitm->swTex && !bitm->hwTex)
    {
        glGenTextures(1, &bitm->hwTex);

        if (!bitm->hwTex)
        {
            return false;
        }

        _states.Tex = bitm->hwTex;
        SetRenderStates(0);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        if (bitm->swTex->format->format == _pixfmt->format)
        {
            SDL_LockSurface(bitm->swTex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bitm->width, bitm->height, 0, _glPixfmt, _glPixtype, bitm->swTex->pixels);
            SDL_UnlockSurface(bitm->swTex);
        }
        else
        {
            SDL_Surface *conv = ConvertSDLSurface(bitm->swTex, _pixfmt);
            
            SDL_LockSurface(conv);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bitm->width, bitm->height, 0, _glPixfmt, _glPixtype, conv->pixels);
            SDL_UnlockSurface(conv);
            
            SDL_FreeSurface(conv);
        }
    }

    return true;
}

void GFXEngine::FreeTexture(ResBitmap *bitm)
{
    if ( bitm->hwTex )
        glDeleteTextures(1, &bitm->hwTex);

    bitm->hwTex = 0;
}

void GFXEngine::display_func271(IDVPair *stak)
{

}

void GFXEngine::win3d_func274__sub0(FSMgr::FileHandle *fil)
{
    int bf_w = 0, bf_h = 0;

    uint8_t *buf = MakeScreenCopy(&bf_w, &bf_h);

    if (buf && bf_w && bf_h)
    {
        fil->printf("P6\n#YPA screenshot\n%d %d\n255\n", bf_w, bf_h);

        int lwidth = 3 * bf_w;

        for (int j = 0; j < bf_h; j++)
        {
            uint8_t *ln = buf + lwidth * (bf_h - 1 - j);

            for (int i = 0; i < bf_w; i++)
            {
                uint8_t *px = ln + i * 3;

                uint8_t r = px[0];
                uint8_t g = px[1];
                uint8_t b = px[2];

                fil->writeU8(r);
                fil->writeU8(g);
                fil->writeU8(b);
            }
        }

        free(buf);
    }
}

void GFXEngine::SaveScreenshot(const std::string & screenName)
{
    FSMgr::FileHandle *fil = uaOpenFileAlloc(fmt::sprintf("%s.ppm", screenName), "wb");
    if ( fil )
    {
        win3d_func274__sub0(fil);
        delete fil;
    }
}


void GFXEngine::windd_func320(IDVPair *)
{
}

void GFXEngine::windd_func321(IDVPair *)
{
}





char * GFXEngine::windd_func322__sub0(const char *box_title, const char *box_ok, const char *box_cancel, const char *box_startText, uint32_t timer_time, void (*timer_func)(int, int, int), void *timer_context, int replace, int maxLen)
{
    dprintf("MAKE ME %s\n","windd_func322__sub0");
    return NULL;
}

//Show DLGBox with edit field and get entered value
void GFXEngine::windd_func322(windd_dlgBox *dlgBox)
{
    windd_func320(NULL);

    dlgBox->result = windd_func322__sub0(
                         dlgBox->title,
                         dlgBox->ok,
                         dlgBox->cancel,
                         dlgBox->startText,
                         dlgBox->time,
                         dlgBox->timer_func,
                         dlgBox->timer_context,
                         dlgBox->replace,
                         dlgBox->maxLen);

    windd_func321(NULL);
}



const std::vector<TGFXDeviceInfo>& GFXEngine::GetDevices()
{
    return _devices;
}

void GFXEngine::SetDeviceByGUID(const std::string &guid, bool writefile)
{
    std::string guidWrite = guid;
    
    bool found = false;
    for( TGFXDeviceInfo &dev: _devices )
    {
        if (dev.guid == guid)
        {
            dev.isCurrent = true;
            found = true;
        }
        else
            dev.isCurrent = false;
    }
    
    if ( !found )
    {
        for( TGFXDeviceInfo &dev: _devices )
        {
            if (dev.guid == "<primary>")
            {
                dev.isCurrent = true;
                found = true;
                guidWrite = "<primary>";
            }
        }
        
        for( TGFXDeviceInfo &dev: _devices )
        {
            if (dev.guid == "<software>")
            {
                dev.isCurrent = true;
                found = true;
                guidWrite = "<software>";
            }
        }
        
        if (!_devices.empty())
        {
            TGFXDeviceInfo &dev = _devices[0];
            found = true;
            dev.isCurrent = true;
            guidWrite = dev.guid;
        }
    }
    
    if (writefile)
    {
        if (guidWrite == "<primary>" || guidWrite == "<software>")
            out_guid_to_file("env/guid3d.def", guidWrite);
        else
            out_guid_to_file("env/guid3d.def", "<error>");
    }
}



void GFXEngine::setWDD_cursor(int mode)
{
}

void GFXEngine::setWDD_disLowRes(int arg)
{
}

void GFXEngine::setWDD_16bitTex(int arg)
{
    out_yes_no_status("env/txt16bit.def", arg);
}

void GFXEngine::setWDD_drawPrim(int arg)
{
    out_yes_no_status("env/drawprim.def", arg);
}



int GFXEngine::getWDD_16bitTex()
{
    return _txt16bit;
}

int GFXEngine::getWDD_drawPrim()
{
    return _use_simple_d3d;
}

void GFXEngine::setW3D_texFilt(int arg)
{
    _filter = arg;
}

void GFXEngine::SetPalette(UA_PALETTE &newPal)
{
    _palette = newPal;
    
    _normClr = vec3d(1.0, 1.0, 1.0);
    _invClr = vec3d(0.0, 0.0, 0.0);
}

void GFXEngine::SetPen(SDL_Color pen)
{
    _field_4 = pen;
}

UA_PALETTE *GFXEngine::GetPalette()
{
    return &_palette;
}




void GFXEngine::draw2DandFlush()
{
    if (_colorEffects)
        Glext::GLBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    Gui::Root::Instance.Draw(Screen());
#ifdef __ANDROID__
    // Android has no visible system pointer for joystick-generated movement.
    // Composite the original cursor into the logical UI before uploading it.
    static const char *names[] = {"Pointer", "Cancel", "Select", "Attack", "Goto", "Disk", "New", "Add", "Control", "Beam", "Build"};
    if (CurrentCursorID > 0 && CurrentCursorID <= 11) {
        auto found = androidCursors.find(names[CurrentCursorID - 1]);
        if (found != androidCursors.end() && found->second.image) {
            const Common::Point p = NC_STACK_winp::AndroidPointer();
            SDL_Rect dest = {p.x - found->second.hotX, p.y - found->second.hotY, 0, 0};
            SDL_SetClipRect(Screen(), nullptr);
            SDL_BlitSurface(found->second.image.get(), nullptr, Screen(), &dest);
        }
    }
    Android::DrawOverlay(Screen());
    SDL_ShowCursor(SDL_DISABLE);
    DrawAndroidWideFill();
#endif
    DrawScreenSurface();
#ifdef __ANDROID__
    DrawAndroidTouchOverlay();
#endif
    Gui::Root::Instance.HwCompose();

    SDL_FillRect(Screen(), NULL, SDL_MapRGBA(Screen()->format, 0, 0, 0, 0) );
    
    if (_colorEffects)
        Glext::GLBindFramebuffer(GL_FRAMEBUFFER, _fbo);
}

void GFXEngine::matrixAspectCorrection(mat3x3 &inout, bool invert)
{
    if (invert)
    {
        inout.m00 *= _corrIW;
        inout.m01 *= _corrIW;
        inout.m02 *= _corrIW;
        inout.m10 *= _corrIH;
        inout.m11 *= _corrIH;
        inout.m12 *= _corrIH;
    }
    else
    {
        inout.m00 *= _corrW;
        inout.m01 *= _corrW;
        inout.m02 *= _corrW;
        inout.m10 *= _corrH;
        inout.m11 *= _corrH;
        inout.m12 *= _corrH;
    }
}

void GFXEngine::getAspectCorrection(float &cW, float &cH, bool invert)
{
    if (invert)
    {
        cW = _corrIW;
        cH = _corrIH;
    }
    else
    {
        cW = _corrW;
        cH = _corrH;
    }
}

void GFXEngine::setFrustumClip(float _near, float _far)
{
    if (_near != _frustumNear || _far != _frustumFar)
        _setFrustumClip(_near, _far);
}

void GFXEngine::_setFrustumClip(float _near, float _far)
{
    //-z * frustum
    _frustumNear = _near;
    _frustumFar = _far;
    
    _frustum = mat4x4f::UAFrustum(_near, _far);
}

void GFXEngine::ConvAlphaPalette(UA_PALETTE *dst, const UA_PALETTE &src, bool transp)
{
    for (uint16_t i = 0; i < dst->size(); i++)
    {
        SDL_Color &c = (*dst)[i];
        c = src[i];
        c.a = 255;

        if (c.r == 255 && c.g == 255 && c.b == 0)
        {
            c.a = 0;
            c.r = 0;
            c.g = 0;
            c.b = 0;
        }
        else
        {
            if (!can_destblend && can_srcblend && transp)
            {
                int mx = (c.r >= c.g) ? (c.r > c.b ? c.r: c.b) : (c.g > c.b ? c.g : c.b);

                if (mx <= 8)
                {
                    c.r = 0;
                    c.g = 0;
                    c.b = 0;
                    c.a = 0;
                }
                else
                {
                    float prm = mx;
                    c.r = 255.0 * (c.r / prm);
                    c.g = 255.0 * (c.g / prm);
                    c.b = 255.0 * (c.b / prm);
                    c.a = mx;
                }
            }
            else
            {
                c.a = 255;
            }
        }
    }
}

SDL_Surface *GFXEngine::CreateSurfaceScreenFormat(int width, int height)
{
#if SDL_VERSION_ATLEAST(2,0,5)
    return SDL_CreateRGBSurfaceWithFormat(0, width, height, _pixfmt->BitsPerPixel, _pixfmt->format);
#else
    return SDL_CreateRGBSurface(0, width, height, _pixfmt->BitsPerPixel, _pixfmt->Rmask, _pixfmt->Gmask, _pixfmt->Bmask, _pixfmt->Amask );
#endif
}

SDL_Surface *GFXEngine::ConvertToScreenFormat(SDL_Surface *src)
{
    return ConvertSDLSurface(src, _pixfmt);
}

SDL_Surface * GFXEngine::ConvertSDLSurface(SDL_Surface *src, const SDL_PixelFormat * fmt)
{
#if (SDL_COMPILEDVERSION == SDL_VERSIONNUM(2, 0, 12))
    /***
     * Workaround for bug with convertation of surface with palette introduced
     * in SDL2 2.0.12 and fixed soon but after release.
     ***/
    if (src->format->BytesPerPixel == 1)
    {
#if SDL_VERSION_ATLEAST(2,0,5)
        SDL_Surface *tmp = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, fmt->BitsPerPixel, fmt->format);
#else
        SDL_Surface *tmp = SDL_CreateRGBSurface(0, src->w, src->h, fmt->BitsPerPixel, fmt->Rmask, fmt->Gmask, fmt->Bmask, fmt->Amask );
#endif
        SDL_BlendMode blend = SDL_BLENDMODE_NONE;
        SDL_GetSurfaceBlendMode(src, &blend);
        SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_NONE);
        SDL_BlitSurface(src, NULL, tmp, NULL);
        SDL_SetSurfaceBlendMode(src, blend);
        return tmp;
    }
    else
        return SDL_ConvertSurface(src, fmt, 0);
#else
    return SDL_ConvertSurface(src, fmt, 0);
#endif
}

void GFXEngine::fpsLimitter(int value)
{
#ifdef __ANDROID__
    FpsMaxTicks = 0; return; // Fractional pacing is in System::Flip.
#endif
    if (value > 1000)
        FpsMaxTicks = 0;
    else if (value <= 0)
        FpsMaxTicks = 0;
    else
        FpsMaxTicks = 1000/value;
}


}


TileMap::TileMap()
{
    img = NULL;
    h = 0;
}
    
TileMap::~TileMap()
{
    if (img)
        img->Delete();
}

void TileMap::Draw(SDL_Surface *surface, const Common::Point &pos, uint8_t c)
{
    SDL_Rect src = map[c];
    SDL_Rect dst = pos;
    SDL_BlitSurface(img->GetSwTex(), &src, surface, &dst);
}

void TileMap::Draw(SDL_Surface *surface, const Common::PointRect &pos, uint8_t c)
{
    SDL_Rect src = map[c];
    if (src.w > pos.w)
        src.w = pos.w;
    if (src.h > pos.h)
        src.h = pos.h;    
    SDL_Rect dst = pos;
    SDL_BlitSurface(img->GetSwTex(), &src, surface, &dst);
}

void TileMap::Draw(SDL_Surface *surface, const Common::Rect &pos, uint8_t c)
{
    SDL_Rect src = map[c];
    if (src.w > pos.Width())
        src.w = pos.Width();
    if (src.h > pos.Height())
        src.h = pos.Height();    
    SDL_Rect dst = pos;
    SDL_BlitSurface(img->GetSwTex(), &src, surface, &dst);
}

void TileMap::Fill(SDL_Surface *surface, const Common::Rect &rect, uint8_t c)
{
    GFX::Engine.DrawFill(img->GetSwTex(), map[c], surface, rect);
}

void TileMap::Fill(SDL_Surface *surface, const Common::PointRect &rect, uint8_t c)
{
    GFX::Engine.DrawFill(img->GetSwTex(), map[c], surface, rect);
}

void TileMap::FillColumn(SDL_Surface *surface, const Common::Rect &rect, uint8_t c)
{
    Common::PointRect sPRect = map[c];
    sPRect.w = 1;
    GFX::Engine.DrawFill(img->GetSwTex(), sPRect, surface, rect);
}

void TileMap::FillColumn(SDL_Surface *surface, const Common::PointRect &rect, uint8_t c)
{
    Common::PointRect sPRect = map[c];
    sPRect.w = 1;
    GFX::Engine.DrawFill(img->GetSwTex(), sPRect, surface, rect);
}

int TileMap::GetWidth(uint8_t c) const
{
    return map[c].w;
}

int TileMap::GetWidth(const std::string &str) const
{
    int wdth = 0;
    for (uint8_t c : str)
        wdth += map[c].w;
    return wdth;
}

Common::Point TileMap::GetSize(uint8_t c) const
{
    return Common::Point(map[c].w, h);
}












namespace GFX
{

GfxMode::GfxMode(GfxMode &&g)
{
    w = std::move(g.w);
    h = std::move(g.h);
    bpp = std::move(g.bpp);
    mode = std::move(g.mode);
    name = std::move(g.name);
    windowed = std::move(g.windowed);
}

GfxMode::GfxMode(const GfxMode &g)
{
    w = g.w;
    h = g.h;
    bpp = g.bpp;
    mode = g.mode;
    name = g.name;
    windowed = g.windowed;
}

GfxMode::GfxMode(const Common::Point &sz)
: w(sz.x), h(sz.y)
{
    name = GenName(w, h);
}

GfxMode& GfxMode::operator=(const GfxMode &g)
{
    w = g.w;
    h = g.h;
    bpp = g.bpp;
    mode = g.mode;
    name = g.name;
    windowed = g.windowed;
    return *this;
}

GfxMode::operator bool() const
{
    if (w == 0 || h == 0)
        return false;
    return true;
}

bool GfxMode::operator==(const GfxMode &g) const
{
    return (w == g.w && h == g.h);
}

bool GfxMode::operator==(const Common::Point &g) const
{
    return (w == g.x && h == g.y);
}

bool GfxMode::operator!=(const GfxMode &g) const
{
    return w != g.w || h != g.h;
}

bool GfxMode::operator!=(const Common::Point &g) const
{
    return w != g.x || h != g.y;
}

bool GfxMode::SortCompare(const GfxMode &a, const GfxMode &b)
{
    if (a.w > b.w)
        return true;
    else if (a.w == b.w && a.h > b.h)
        return true;
    return false;
}

std::string GfxMode::GenName(int w, int h)
{
    return fmt::sprintf("%d x %d", w, h);
}




void GFXEngine::AddGfxMode(const GfxMode &md)
{
    for (const GfxMode &m : graphicsModes)
    {
        if ( m.w == md.w && m.h == md.h )
            return;
    }

    graphicsModes.push_back(md);
}


uint32_t GFXEngine::CursPix(uint8_t *data, int ofs, int bpp)
{
    switch (bpp)
    {
    case 1:
        return (data[ofs / 8] >> (7 - ofs % 8)) & 1;
    case 2:
        return (data[ofs / 4] >> ((3 - ofs % 4) << 1)) & 3;
    case 4:
        return (data[ofs / 2] >> ((1 - ofs % 2) << 2)) & 15;
    case 8:
        return data[ofs];
    case 16:
        return data[2 * ofs] | data[2 * ofs + 1] << 8;
    case 24:
        return data[3 * ofs] | data[3 * ofs + 1] << 8 | data[ 3 * ofs + 2] << 16;
    case 32:
        return data[4 * ofs] | data[4 * ofs + 1] << 8 | data[4 * ofs + 2] << 16 | data[4 * ofs + 3] << 24;
    }

    return 0;
}

SDL_Cursor *GFXEngine::LoadCursor(const std::string &name)
{
    FSMgr::FileHandle *fil = uaOpenFileAlloc( fmt::sprintf("res/%s.cur", name) , "rb");

    UA_PALETTE pal;

    if (!fil)
        return NULL;

    fil->readU16L();
    if (fil->readU16L() != 2)
    {
        delete fil;
        return NULL;
    }

    fil->readU16L(); //count

    //Only first entry
    fil->readU8(); //w
    fil->readU8(); //h
    fil->readU8(); //color count
    fil->readU8(); //reserved
    int hotX = fil->readU16L();
    int hotY = fil->readU16L();
    fil->readU32L(); //size
    int off = fil->readU32L();

    //seek to cursor
    fil->seek(off, SEEK_SET);

    //read InfoHeader
    fil->readU32L(); //header size
    int bmpw = fil->readS32L();
    int bmph = fil->readS32L();
    fil->readU16L(); //planes
    int bitcount = fil->readU16L();
    fil->readU32L(); //compression
    fil->readU32L(); //imagesize
    fil->readU32L(); //XpixelsPerM
    fil->readU32L(); //YpixelsPerM
    int clrused = fil->readU32L(); //ColorsUsed
    fil->readU32L(); //ColorsImportant

    // read pallete
    int palcnt = 0;
    if (clrused == 0 || bitcount < 16)
    {
        palcnt = clrused != 0 ? clrused : 1 << bitcount;

        for (int i = 0; i < palcnt; i++)
        {
            pal[i].r = fil->readU8();
            pal[i].g = fil->readU8();
            pal[i].b = fil->readU8();
            fil->readU8(); //reserved
        }
    }

    int width = bmpw;
    int height = abs(bmph)/2;

    int imgsz = height * width * bitcount / 8;
    int mask_size = height * width / 8;

    uint8_t *data = (uint8_t *)malloc(imgsz);
    uint8_t *mask = (uint8_t *)malloc(mask_size);

    fil->read(data, imgsz);
    fil->read(mask, mask_size);

    delete fil;


    SDL_Surface *cursr = SDL_CreateRGBSurface(0, width, height, 32, 0xFF, 0xFF00, 0xFF0000, 0xFF000000);

    for (int y = 0; y < height; y++)
    {
        int invY = height - 1 - y;
        uint8_t *row = (uint8_t *)cursr->pixels + y * cursr->pitch;

        for (int x = 0; x < width; x++)
        {
            if (palcnt > 0)
            {
                int idx = CursPix(data, invY * width + x, bitcount);
                int alph = CursPix(mask, invY * width + x, 1);
                row[x * 4 + 0] = pal[ idx ].r;
                row[x * 4 + 1] = pal[ idx ].g;
                row[x * 4 + 2] = pal[ idx ].b;
                row[x * 4 + 3] = (1 - alph) * 255;
            }
            else
            {
                uint32_t clr = CursPix(data, invY * width + x, bitcount);
                int alph = CursPix(mask, invY * width + x, 1);
                row[x * 4 + 0] = clr & 0xFF;
                row[x * 4 + 1] = (clr >> 8) & 0xFF;
                row[x * 4 + 2] = (clr >> 16) & 0xFF;
                row[x * 4 + 3] = (1 - alph) * 255;
            }
        }
    }

    free(data);
    free(mask);

#ifdef __ANDROID__
    AndroidCursor &saved = androidCursors[name];
    saved.image.reset(SDL_ConvertSurface(cursr, cursr->format, 0), SDL_FreeSurface);
    saved.hotX = hotX;
    saved.hotY = hotY;
    SDL_SetSurfaceBlendMode(saved.image.get(), SDL_BLENDMODE_BLEND);
#endif
    SDL_Cursor* cursor = SDL_CreateColorCursor(cursr, hotX, hotY);

    SDL_FreeSurface(cursr);

    return cursor;
}

void GFXEngine::Init()
{
    StaticInit();
    
    _glext = Glext::init();
    
    SetDeviceByGUID( read_guid("env/guid3d.def") );
    
    System::EventsAddHandler(EventsWatcher);
    
    System::IniConf::ReadFromNucleusIni();
    
    _vbo = System::IniConf::GfxVBO.Get<bool>();
    _colorEffects = System::IniConf::GfxColorEffects.Get<int32_t>();
    
    if (!_glext)
    {
        _colorEffects = 0;
        _vbo = false;
    }
    
    if (_vbo)
    {
        Glext::GLGenVertexArrays(1, &_globalVao);
        Glext::GLBindVertexArray(_globalVao);
        
        Glext::GLGenBuffers(1, &_vboParams);
        Glext::GLBindBuffer(GL_UNIFORM_BUFFER, _vboParams);
        Glext::GLBufferData(GL_UNIFORM_BUFFER, _vboParamsSize, NULL, GL_STREAM_DRAW); 
        
        Glext::GLBindBufferBase(GL_UNIFORM_BUFFER, _vboParamsBlockBinding, _vboParams);
    }
    
    std::array<Common::Point, 17> checkModes
    {{
        {640, 480},     {800, 600},     {1024, 768},    {1280, 1024}, 
        {1440, 1050},   {1600, 1200},   {720, 480},     {852, 480}, 
        {1280, 720},    {1366, 768},    {1600, 900},    {1920, 1080}, 
        {1920, 1200},   {2560, 1080},   {2560, 1440},   {3440, 1440},
        {3840, 2160}
     }};
    
    graphicsModes.reserve(checkModes.size());

    for(Common::Point m : checkModes)
    {
        SDL_DisplayMode target, closest;

        target.w = m.x;
        target.h = m.y;
        target.format = _pixfmt->format;
        target.refresh_rate = 0;
        target.driverdata   = 0;

        if (SDL_GetClosestDisplayMode(0, &target, &closest) )
        {
            GfxMode mode;
            mode.w = closest.w;
            mode.h = closest.h;
            mode.mode = closest;
            mode.bpp = _pixfmt->BytesPerPixel;
            mode.name = GfxMode::GenName(mode.w, mode.h);

            AddGfxMode(mode);
        }
    }
    
    // Force to add custom resolutions
    std::vector<std::string> customModes = Stok::Split(System::IniConf::GfxAdditionalModes.Get<std::string>(), ",");
    for (std::string mod : customModes)
    {
        std::vector<std::string> vals = Stok::Split(mod, ":-x \t");
        if (vals.size() >= 2)
        {
            SDL_DisplayMode target, closest;
            
            target.w = std::stoi(vals[0]);
            target.h = std::stoi(vals[1]);
            target.format = _pixfmt->format;
            target.refresh_rate = 0;
            target.driverdata   = 0;

            GfxMode mode;
            mode.w = target.w;
            mode.h = target.h;
            mode.mode = target;
            mode.bpp = _pixfmt->BytesPerPixel;
            mode.name = GfxMode::GenName(mode.w, mode.h);
                
            if (SDL_GetClosestDisplayMode(0, &target, &closest) )
            {
                mode.mode = closest;
                mode.bpp = SDL_BYTESPERPIXEL(closest.format) * 8;
                mode.name = GfxMode::GenName(mode.w, mode.h);
            }
            
            AddGfxMode(mode);
        }
    }
    
    

    std::sort(graphicsModes.begin(), graphicsModes.end(), GfxMode::SortCompare);
    
    cursors[0] = LoadCursor("Pointer");
    cursors[1] = LoadCursor("Cancel");
    cursors[2] = LoadCursor("Select");
    cursors[3] = LoadCursor("Attack");
    cursors[4] = LoadCursor("Goto");
    cursors[5] = LoadCursor("Disk");
    cursors[6] = LoadCursor("New");
    cursors[7] = LoadCursor("Add");
    cursors[8] = LoadCursor("Control");
    cursors[9] = LoadCursor("Beam");
    cursors[10] = LoadCursor("Build");

    IDVList stak { {ATT_WIDTH,  (int32_t)640},
            {ATT_HEIGHT, (int32_t)480} };
    func0( stak );

    RecreateScreenSurface();
    Gui::Instance.SetScreenSize(GetScreenSize());
    
    LoadPalette(System::IniConf::GfxPalette.Get<std::string>());

    if (_vbo)
    {
        _stdPsShader = CompileShader(GL_FRAGMENT_SHADER, _stdPShaderText);
        _stdVsShader = CompileShader(GL_VERTEX_SHADER,   _stdVShaderText);
        uint32_t progID = Glext::GLCreateProgram();
        
        Glext::GLAttachShader(progID, _stdPsShader);
        Glext::GLAttachShader(progID, _stdVsShader);
        Glext::GLLinkProgram(progID);
        
        _stdShaderProg = TShaderProg( progID );
        
        BindVBOParameters(_stdShaderProg);
    }

    if (_colorEffects > 0)
    {
        Glext::GLGenFramebuffers(1, &_fbo);
        Glext::GLBindFramebuffer(GL_FRAMEBUFFER, _fbo);

        glGenTextures(1, &_fboTex);
        glBindTexture(GL_TEXTURE_2D, _fboTex);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glTexImage2D(GL_TEXTURE_2D, 0, FBOTEXTYPE, 640, 480, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);


        Glext::GLGenRenderbuffers(1, &_fbod);
        Glext::GLBindRenderbuffer(GL_RENDERBUFFER, _fbod);
        Glext::GLRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 640, 480);

        
        Glext::GLFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, _fbod);

        Glext::GLFrameBufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _fboTex, 0);

        Glext::GLBindRenderbuffer(GL_RENDERBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        Glext::GLBindFramebuffer(GL_FRAMEBUFFER, 0);

        if (_vbo)
        {
            _psShader = LoadShader(GL_FRAGMENT_SHADER, "res/clreff_vbo.ps");
            _vsShader = LoadShader(GL_VERTEX_SHADER, "res/clreff_vbo.vs");
        }
        else
        {
            _psShader = LoadShader(GL_FRAGMENT_SHADER, "res/clreff.ps");
            _vsShader = LoadShader(GL_VERTEX_SHADER, "res/clreff.vs");
        }

        uint32_t progID = Glext::GLCreateProgram();
        
        Glext::GLAttachShader(progID, _psShader);
        Glext::GLAttachShader(progID, _vsShader);
        Glext::GLLinkProgram(progID);
        
        _colorEffectsShaderProg = TColorEffectsProg(progID);
        
        if (_vbo)
            BindVBOParameters(_colorEffectsShaderProg);
    }
    
}

void GFXEngine::Deinit()
{
    if ( _font.ttfFont )
    {
        TTF_CloseFont(_font.ttfFont);
        _font.ttfFont = NULL;
    }
    
    if (ScreenSurface)
        SDL_FreeSurface(ScreenSurface);
    
    ScreenSurface = NULL;
    
    if (_stdQuadDataBuf)
        Glext::GLDeleteBuffers(1, &_stdQuadDataBuf);
    
    _stdQuadDataBuf = 0;
    
    if (_stdQuadIndexBuf)
        Glext::GLDeleteBuffers(1, &_stdQuadIndexBuf);
    
    _stdQuadIndexBuf = 0;
    
    if (_stdShaderProg.ID)
        Glext::GLDeleteProgram(_stdShaderProg.ID);
    
    if (_stdVsShader)
        Glext::GLDeleteShader(_stdVsShader);
    
    if (_stdPsShader)
        Glext::GLDeleteShader(_stdPsShader);
    
    _stdShaderProg.ID = 0;
    _stdVsShader = 0;
    _stdPsShader = 0;   
    
    if (_colorEffectsShaderProg.ID)
        Glext::GLDeleteProgram(_colorEffectsShaderProg.ID); 
    
    if (_psShader)
        Glext::GLDeleteShader(_psShader);
    
    if (_vsShader)
        Glext::GLDeleteShader(_vsShader);
    
    _colorEffectsShaderProg.ID = 0;
    _vsShader = 0;
    _psShader = 0;
}

GFXEngine::~GFXEngine()
{
    Deinit();
}

int GFXEngine::EventsWatcher(void *, SDL_Event *event)
{
    switch(event->type)
    {
    case SDL_WINDOWEVENT:
    {
        switch(event->window.event)
        {
        case SDL_WINDOWEVENT_RESIZED:
        case SDL_WINDOWEVENT_SIZE_CHANGED:
            Instance.UpdateFBOSizes();
            break;
        default:
            break;
        }

    }
    break;
    }
    
    return 1; // This event can be passed to another event watcher
}

void GFXEngine::SetCursor(int curID, int force)
{
    int sett = 0;

    if ( force )
        sett = 1;
    else if ( curID != CurrentCursorID )
        sett = 1;

    if ( sett )
    {
        if ( curID == 0 )
            SDL_ShowCursor(SDL_DISABLE);
        else if ( curID <= 11 )
        {
            if ( cursors[curID - 1] )
                SDL_SetCursor( cursors[curID - 1] );

            if (!CurrentCursorID)
                SDL_ShowCursor(SDL_ENABLE);
        }

    }

    CurrentCursorID = curID;
}

GfxMode GFXEngine::sub_41F68C()
{
    for (const GfxMode &m : graphicsModes)
    {
        if (m.w == 640 && m.h == 480)
            return m;
    }

    return graphicsModes.front();
}


GfxMode GFXEngine::windd_func0__sub0(const std::string &file)
{
    FSMgr::FileHandle *fil = uaOpenFileAlloc(file, "r");

    if ( fil )
    {
        std::string line;
        if ( fil->ReadLine(&line) )
        {
            size_t pos = line.find_first_of("\n\r");
            
            if (pos != std::string::npos)
                line.erase(pos);
            
            bool windowed = false;
            pos = line.find("Windowed");
            
            if (pos != std::string::npos && pos >= 1)
            {
                windowed = true;
                line.erase(pos - 1);
            }           

            for (const GfxMode &m : graphicsModes)
            {
                if ( StriCmp(m.name, line) == 0 )
                {
                    GfxMode tmp = m;
                    tmp.windowed = windowed;
                    return tmp;
                }
            }
        }
        delete fil;
    }

    return sub_41F68C();
}

int GFXEngine::GetGfxModeIndex(const Common::Point &res)
{
    int i = 0;
    for (const GfxMode &m : graphicsModes)
    {
        if (m == res)
            return i;
        ++i;
    }
    return -1;
}

void GFXEngine::SetResolution(const Common::Point &res, bool windowed)
{
#ifndef __ANDROID__
    if (GfxSelectedMode == res && GfxSelectedMode.windowed == windowed)
        return;
#endif
    
    UA_PALETTE *screen_palette = GetPalette();

    UA_PALETTE palette_copy;

    if ( screen_palette )
        palette_copy = *screen_palette; // Copy palette
    else
    {
        for(auto &x : palette_copy)
        {
            x.r = 0;
            x.g = 0;
            x.b = 0;
            x.a = 0;
        }
    }

    EndFrame();

    //cls3D->Delete();
    
    GfxMode picked;
    if ( res )
    {
        for (const GfxMode &m : graphicsModes)
        {
            if ( m.w == res.x && m.h == res.y )
            {
                picked = m;
                picked.windowed = windowed;
                break;
            }
        }

        if ( !picked )
            picked = sub_41F68C();
    }
    else
    {
        picked = windd_func0__sub0("env/vid.def");
    }
    
#ifdef __ANDROID__
    // Scale the logical interface independently from the native display.
    Common::Point logical = Android::LogicalSize();
    picked.w = logical.x; picked.h = logical.y;
#endif
    if (!picked.windowed)
        System::SetVideoMode(Common::Point(picked.w, picked.h), SDL_WINDOW_FULLSCREEN_DESKTOP, &picked.mode);
    else
        System::SetVideoMode(Common::Point(picked.w, picked.h), 0, NULL);
    
    SetResVariables(picked);        
    ApplyResolution();
    
    RecreateScreenSurface();
    
    BeginFrame();
    SetPalette(palette_copy);
    
    GfxSelectedMode = picked;
    
    FSMgr::FileHandle *fil = uaOpenFileAlloc("env/vid.def", "w");
    if ( fil )
    {
        if (picked.windowed)
            fil->printf("%s Windowed\n", picked.name.c_str());
        else
            fil->printf("%s\n", picked.name.c_str());
            
        delete fil;
    }
    
    Gui::Instance.SetScreenSize(GetScreenSize());
    
    UpdateFBOSizes();
}

const std::vector<GfxMode> &GFXEngine::GetAvailableModes()
{
    return graphicsModes;
}




void GFXEngine::SetTracyRmp(ResBitmap *rmp)
{
    setRSTR_trcRmp(rmp);
}

void GFXEngine::SetShadeRmp(ResBitmap *rmp)
{
    setRSTR_shdRmp(rmp);
}

GfxMode GFXEngine::GetGfxMode()
{
    return GfxSelectedMode;
}

TileMap * GFXEngine::GetTileset(int id)
{
    return raster_func208(id);
}

void GFXEngine::SetTileset(TileMap *tileset, int id)
{
    raster_func207(id, tileset);
}


bool GFXEngine::LoadPalette(const std::string &palette_ilbm)
{
    NC_STACK_bitmap *ilbm = Nucleus::CInit<NC_STACK_ilbm>( {
        {NC_STACK_rsrc::RSRC_ATT_NAME, std::string(palette_ilbm)},
        {NC_STACK_bitmap::BMD_ATT_HAS_COLORMAP, (int32_t)1}} );

    if (!ilbm)
        return false;

    SetPalette( *ilbm->getBMD_palette() );

    ilbm->Delete();

    return true;
}
    
uint8_t *GFXEngine::MakeScreenCopy(int *ow, int *oh)
{
    Common::Point res = System::GetResolution();

    // power of 2
    res.x &= ~1;
    res.y &= ~1;

    uint8_t *buf = (uint8_t *)malloc(res.x * res.y * 3);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glReadBuffer(GL_FRONT);
    glReadPixels(0, 0, res.x, res.y, GL_RGB, GL_UNSIGNED_BYTE, buf);

    *ow = res.x;
    *oh = res.y;
    return buf;
}

uint8_t *GFXEngine::MakeDepthScreenCopy(int *ow, int *oh)
{
    Common::Point res = System::GetResolution();

    // power of 2
    res.x &= ~1;
    res.y &= ~1;

    uint8_t *buf = (uint8_t *)malloc(res.x * res.y);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glReadBuffer(GL_FRONT);
    glReadPixels(0, 0, res.x, res.y, GL_DEPTH_COMPONENT, GL_UNSIGNED_BYTE, buf);

    *ow = res.x;
    *oh = res.y;
    return buf;
}



SDL_Surface *GFXEngine::Screen()
{
    return ScreenSurface;
}



// Draw line Bresenham's algorithm
void GFXEngine::DrawLine(SDL_Surface *surface, const Common::Line &line, uint8_t cr, uint8_t cg, uint8_t cb )
{
    if ((line.Width() == 0 && line.Height() == 0) || 
         !Common::Rect(surface->w, surface->h).IsIn(line.P1()) ||
         !Common::Rect(surface->w, surface->h).IsIn(line.P2()) )
        return;
    
    int rilWidth = surface->pitch / surface->format->BytesPerPixel;

    int xCount = line.Width();
    int yCount = line.Height();

    uint32_t color = SDL_MapRGBA(surface->format, cr, cg, cb, 255);

    int stepAdd, stepOdd;
    int steps, subSteps;

    if ( xCount <= yCount )
    {
        if ( line.y2 <= line.y1 )
            stepAdd = -rilWidth;
        else
            stepAdd = rilWidth;

        if ( line.x2 <= line.x1 )
            stepOdd = -1;
        else
            stepOdd = 1;

        steps = yCount;
        subSteps = xCount;
    }
    else
    {
        if ( line.x2 <= line.x1 )
            stepAdd = -1;
        else
            stepAdd = 1;

        if ( line.y2 <= line.y1 )
            stepOdd = -rilWidth;
        else
            stepOdd = rilWidth;
        
        steps = xCount;
        subSteps = yCount;
    }

    int incr1 = 2 * subSteps;
    int t = 2 * subSteps - steps;
    int incr2 = 2 * (subSteps - steps);
    
    SDL_LockSurface(surface);
    
    void *surfPos = (void *) ((uint8_t *) surface->pixels 
                    + line.y1 * surface->pitch 
                    + line.x1 * surface->format->BytesPerPixel );

    switch(surface->format->BytesPerPixel)
    {
        case 1:
        {
            uint8_t *surf = (uint8_t *)surfPos;

            for (int i = 0; i <= steps; i++) // Verify i bound
            {
                *surf = color;
                if ( t > 0 )
                {
                    t += incr2;
                    surf += stepOdd;
                }
                else
                    t += incr1;

                surf += stepAdd;
            }
        }
        break;
        
        case 2:
        {
            uint16_t *surf = (uint16_t *)surfPos;

            for (int i = 0; i <= steps; i++) // Verify i bound
            {
                *surf = color;
                if ( t > 0 )
                {
                    t += incr2;
                    surf += stepOdd;
                }
                else
                    t += incr1;

                surf += stepAdd;
            }
        }
        break;
        
        case 4:
        {
            uint32_t *surf = (uint32_t *)surfPos;

            for (int i = 0; i <= steps; i++) // Verify i bound
            {
                *surf = color;
                if ( t > 0 )
                {
                    t += incr2;
                    surf += stepOdd;
                }
                else
                    t += incr1;

                surf += stepAdd;
            }
        }
        break;
        
        default:
        break;
    }
    
    SDL_UnlockSurface(surface);
}

void GFXEngine::BlitScaleMasked(SDL_Surface *src, Common::Rect sRect, SDL_Surface *mask, uint8_t index, SDL_Surface *dst, Common::Rect dRect)
{
    if (mask->format->BitsPerPixel != 8)
        return;
        
    if (src->w != mask->w || src->h != mask->h)
        return;
    
    if (sRect.IsEmpty() || !sRect.IsValid())
        sRect = Common::Rect(src->w, src->h);
    else if (!Common::Rect(src->w, src->h).IsIn(sRect))
        return;
    
    if (dRect.IsEmpty() || !dRect.IsValid())
        dRect = Common::Rect(dst->w, dst->h);
    else if (!Common::Rect(dst->w, dst->h).IsIn(dRect))
        return;
    
    // Try fast
    if (src->format->format == dst->format->format)
    {
        switch(src->format->BytesPerPixel)
        {
            case 2:
            {
                SDL_LockSurface(src);
                SDL_LockSurface(mask);
                SDL_LockSurface(dst);
                
                int32_t dY = (sRect.Height() << 16) / dRect.Height();
                int32_t dX = (sRect.Width()  << 16) / dRect.Width();
                
                int32_t srcY  = sRect.top << 16;
                for (int y = dRect.top; y < dRect.bottom; y++)
                {
                    uint16_t *dBuf = (uint16_t *)((uint8_t *)dst->pixels + y * dst->pitch) + dRect.left;
                    uint16_t *sBuf = (uint16_t *)((uint8_t *)src->pixels + (srcY >> 16) * src->pitch) + sRect.left;
                    uint8_t  *mBuf = (uint8_t *)mask->pixels + (srcY >> 16) * mask->pitch + sRect.left;
                    
                    int32_t xx = 0;
                    for (int x = dRect.left; x < dRect.right; x++)
                    {
                        if (mBuf[xx >> 16] == index)
                            *dBuf = sBuf[xx >> 16];
                        dBuf++;
                        xx += dX;
                    }
                    srcY += dY;
                }
                
                SDL_UnlockSurface(dst);
                SDL_UnlockSurface(mask);
                SDL_UnlockSurface(src);
            }
            break;
            
            case 4:
            {
                SDL_LockSurface(src);
                SDL_LockSurface(mask);
                SDL_LockSurface(dst);
                
                int32_t dY = (sRect.Height() << 16) / dRect.Height();
                int32_t dX = (sRect.Width()  << 16) / dRect.Width();
                
                int32_t srcY  = sRect.top << 16;
                for (int y = dRect.top; y < dRect.bottom; y++)
                {
                    uint32_t *dBuf = (uint32_t *)((uint8_t *)dst->pixels + y * dst->pitch) + dRect.left;
                    uint32_t *sBuf = (uint32_t *)((uint8_t *)src->pixels + (srcY >> 16) * src->pitch) + sRect.left;
                    uint8_t  *mBuf = (uint8_t *)mask->pixels + (srcY >> 16) * mask->pitch + sRect.left;
                    
                    int32_t xx = 0;
                    for (int x = dRect.left; x < dRect.right; x++)
                    {
                        if (mBuf[xx >> 16] == index)
                            *dBuf = sBuf[xx >> 16];
                        dBuf++;
                        xx += dX;
                    }
                    srcY += dY;
                }
                
                SDL_UnlockSurface(dst);
                SDL_UnlockSurface(mask);
                SDL_UnlockSurface(src);
            }
            break;
            
            default:
            break;
        }
    }
    else // Slow
    {
        SDL_LockSurface(src);
        SDL_LockSurface(mask);
        SDL_LockSurface(dst);

        uint8_t sbpp = src->format->BytesPerPixel;
        uint8_t dbpp = dst->format->BytesPerPixel;
        
        int32_t dY = (sRect.Height() << 16) / dRect.Height();
        int32_t dX = (sRect.Width()  << 16) / dRect.Width();

        int32_t srcY  = sRect.top << 16;
        for (int y = dRect.top; y < dRect.bottom; y++)
        {
            uint8_t *dBuf = (uint8_t *)dst->pixels + y * dst->pitch + dRect.left * dbpp;
            uint8_t *sBuf = (uint8_t *)src->pixels + (srcY >> 16) * src->pitch + sRect.left * sbpp;
            uint8_t  *mBuf = (uint8_t *)mask->pixels + (srcY >> 16) * mask->pitch + sRect.left;

            int32_t xx = 0;
            for (int x = dRect.left; x < dRect.right; x++)
            {
                if (mBuf[xx >> 16] == index)
                {
                    uint8_t r,g,b;
                    uint32_t clr = 0;
                    
                    uint8_t *spix = sBuf + (xx >> 16) * sbpp;
                    for(int i = 0; i < sbpp; i++)
                        clr |= spix[i] << (i * 8);
                    
                    SDL_GetRGB(clr, src->format, &r, &g, &b);
                    clr = SDL_MapRGB(dst->format, r, g, b);
                    
                    for(int i = 0; i < dbpp; i++)
                        dBuf[i] = (clr >> (i * 8)) & 0xFF;
                }
                dBuf += dbpp;
                xx += dX;
            }
            srcY += dY;
        }

        SDL_UnlockSurface(dst);
        SDL_UnlockSurface(mask);
        SDL_UnlockSurface(src);
    }
}

void GFXEngine::DrawFillIntCpy(SDL_Surface *src, const Common::Rect &sRect, SDL_Surface *dst, const Common::Rect &dRect)
{
    const int32_t sh = sRect.Height();
    const int32_t sw = sRect.Width();
    const int32_t dh = dRect.Height();
    const int32_t dw = dRect.Width();
        
    SDL_LockSurface(src);
    SDL_LockSurface(dst);
                    
    if (dw > sw)
    {
        uint8_t *psrc = (uint8_t *)src->pixels + sRect.top * src->pitch + sRect.left * src->format->BytesPerPixel;
        uint8_t *pdst = (uint8_t *)dst->pixels + dRect.top * dst->pitch + dRect.left * dst->format->BytesPerPixel;
        
        const int32_t nit = dw / sw;
        const int32_t nit2 = (dw % sw) * dst->format->BytesPerPixel;
        const int32_t cpsz = sw * dst->format->BytesPerPixel;
        const int32_t skipdst = dst->pitch - dw * dst->format->BytesPerPixel + nit2;
                
        int sptch = src->pitch;
        
        int ln = 0;
        uint8_t *psrcl = psrc;
        for(int ih = dh; ih > 0; --ih)
        {
            for(int n = nit; n > 0; --n)
            {
                memcpy(pdst, psrcl, cpsz);
                pdst += cpsz;
            }
            
            memcpy(pdst, psrcl, nit2);
            pdst += skipdst;
            
            ++ln;
            if (ln >= sh)
            {
                ln = 0;
                psrcl = psrc;
            }
            else
                psrcl += sptch;
        }        
    }
    else
    {
        uint8_t *psrc = (uint8_t *)src->pixels + sRect.top * src->pitch + sRect.left * src->format->BytesPerPixel;
        uint8_t *pdst = (uint8_t *)dst->pixels + dRect.top * dst->pitch + dRect.left * dst->format->BytesPerPixel;
        
        const int32_t cpsz = dw * dst->format->BytesPerPixel;
        const int32_t skipdst = dst->pitch;
                
        int sptch = src->pitch;
        
        int ln = 0;
        uint8_t *psrcl = psrc;
        for(int ih = dh; ih > 0; --ih)
        {            
            memcpy(pdst, psrcl, cpsz);
            pdst += skipdst;
            
            ++ln;
            if (ln >= sh)
            {
                ln = 0;
                psrcl = psrc;
            }
            else
                psrcl += sptch;
        }        
    }
                
    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(src);
}
    
template <typename T>
void GFXEngine::DrawFillIntCKey(SDL_Surface *src, const Common::Rect &sRect, SDL_Surface *dst, const Common::Rect &dRect)
{
    const int32_t sh = sRect.Height();
    const int32_t sw = sRect.Width();
    const int32_t dh = dRect.Height();
    const int32_t dw = dRect.Width();
        
    SDL_LockSurface(src);
    SDL_LockSurface(dst);
    
    uint32_t clrKey = 0;
    SDL_GetColorKey(src, &clrKey);
                
    if (dw > sw)
    {
        const int32_t skipdst = dst->pitch - dw * dst->format->BytesPerPixel;
        uint8_t *psrc = (uint8_t *)src->pixels + sRect.top * src->pitch + sRect.left * src->format->BytesPerPixel;
        uint8_t *pdst = (uint8_t *)dst->pixels + dRect.top * dst->pitch + dRect.left * dst->format->BytesPerPixel;
        
        const int32_t nit = dw / sw;
        const int32_t nit2 = dw % sw;
        
        const int sz = dst->format->BytesPerPixel;
        
        int sptch = src->pitch;
        
        int ln = 0;
        uint8_t *psrcl = psrc;
        for(int ih = dh; ih > 0; --ih)
        {
            
            for(int n = nit; n > 0; --n)
            {
                const uint8_t *lsrc = psrcl;
                for (int iw = sw; iw > 0; --iw)
                {
                    if (*(T *)lsrc != clrKey)
                        *(T *)pdst = *(T *)lsrc;
                    
                    lsrc += sz;
                    pdst += sz;
                }
            }
            
            uint8_t *rsrc = psrcl;            
            for(int iw = nit2; iw > 0; --iw)
            {
                if (*(T *)rsrc != clrKey)
                    *(T *)pdst = *(T *)rsrc;

                rsrc += sz;
                pdst += sz;
            }
            
            ++ln;
            if (ln >= sh)
            {
                ln = 0;
                psrcl = psrc;
            }
            else
                psrcl += sptch;
            
            pdst += skipdst;
        }        
    }
    else
    {
        const int32_t skipdst = dst->pitch - dw * dst->format->BytesPerPixel;
        uint8_t *psrc = (uint8_t *)src->pixels + sRect.top * src->pitch + sRect.left * src->format->BytesPerPixel;
        uint8_t *pdst = (uint8_t *)dst->pixels + dRect.top * dst->pitch + dRect.left * dst->format->BytesPerPixel;
        
        const int sz = dst->format->BytesPerPixel;
        
        int sptch = src->pitch;
        
        int ln = 0;
        uint8_t *psrcl = psrc;
        for(int ih = dh; ih > 0; --ih)
        {            
            uint8_t *rsrc = psrcl;            
            for(int iw = dw; iw > 0; --iw)
            {
                if (*(T *)rsrc != clrKey)
                    *(T *)pdst = *(T *)rsrc;

                rsrc += sz;
                pdst += sz;
            }
            
            ++ln;
            if (ln >= sh)
            {
                ln = 0;
                psrcl = psrc;
            }
            else
                psrcl += sptch;
            
            pdst += skipdst;
        }        
    }
                
    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(src);
}

template <typename T>
void GFXEngine::DrawFillIntACpy(SDL_Surface* src, const Common::Rect& sRect, SDL_Surface* dst, const Common::Rect& dRect)
{
    const int32_t sh = sRect.Height();
    const int32_t sw = sRect.Width();
    const int32_t dh = dRect.Height();
    const int32_t dw = dRect.Width();
        
    SDL_LockSurface(src);
    SDL_LockSurface(dst);
    
    if (dw > sw)
    {
        const int32_t skipdst = dst->pitch - dw * dst->format->BytesPerPixel;
        uint8_t *psrc = (uint8_t *)src->pixels + sRect.top * src->pitch + sRect.left * src->format->BytesPerPixel;
        uint8_t *pdst = (uint8_t *)dst->pixels + dRect.top * dst->pitch + dRect.left * dst->format->BytesPerPixel;
        
        const int32_t nit = dw / sw;
        const int32_t nit2 = dw % sw;
        uint32_t amask = src->format->Amask;
        
        const int sz = dst->format->BytesPerPixel;
        
        int sptch = src->pitch;
        
        int ln = 0;
        uint8_t *psrcl = psrc;
        for(int ih = dh; ih > 0; --ih)
        {
            
            for(int n = nit; n > 0; --n)
            {
                const uint8_t *lsrc = psrcl;
                for (int iw = sw; iw > 0; --iw)
                {
                    if (*(T *)lsrc & amask)
                        *(T *)pdst = *(T *)lsrc;
                    
                    lsrc += sz;
                    pdst += sz;
                }
            }
            
            uint8_t *rsrc = psrcl;            
            for(int iw = nit2; iw > 0; --iw)
            {
                if (*(T *)rsrc & amask)
                    *(T *)pdst = *(T *)rsrc;

                rsrc += sz;
                pdst += sz;
            }
            
            ++ln;
            if (ln >= sh)
            {
                ln = 0;
                psrcl = psrc;
            }
            else
                psrcl += sptch;
            
            pdst += skipdst;
        }        
    }
    else
    {
        const int32_t skipdst = dst->pitch - dw * dst->format->BytesPerPixel;
        uint8_t *psrc = (uint8_t *)src->pixels + sRect.top * src->pitch + sRect.left * src->format->BytesPerPixel;
        uint8_t *pdst = (uint8_t *)dst->pixels + dRect.top * dst->pitch + dRect.left * dst->format->BytesPerPixel;
        
        const int sz = dst->format->BytesPerPixel;
        
        int sptch = src->pitch;
        uint32_t amask = src->format->Amask;
        
        int ln = 0;
        uint8_t *psrcl = psrc;
        for(int ih = dh; ih > 0; --ih)
        {            
            uint8_t *rsrc = psrcl;            
            for(int iw = dw; iw > 0; --iw)
            {
                if (*(T *)rsrc & amask)
                    *(T *)pdst = *(T *)rsrc;

                rsrc += sz;
                pdst += sz;
            }
            
            ++ln;
            if (ln >= sh)
            {
                ln = 0;
                psrcl = psrc;
            }
            else
                psrcl += sptch;
            
            pdst += skipdst;
        }        
    }
                
    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(src);
}

void GFXEngine::DrawFill(SDL_Surface *src, const Common::Rect &sRect, SDL_Surface *dst, const Common::Rect &dRect)
{
    if (sRect.IsEmpty() || dRect.IsEmpty())
        return;
    
    if (src->format->format == dst->format->format)
    {     
#if SDL_VERSION_ATLEAST(2, 0, 9)
        if (SDL_HasColorKey(src))
#else
        uint32_t tmp;
        if (SDL_GetColorKey(src, &tmp) == 0)
#endif
        {
            switch(src->format->BytesPerPixel)
            {

                case 2:
                    DrawFillIntCKey<uint16_t>(src, sRect, dst, dRect);
                    break;

                case 4:
                    DrawFillIntCKey<uint32_t>(src, sRect, dst, dRect);
                    break;

                default:
                break;
            }
        }
        else
        {
            if (src->format->Amask)
            {
                switch(src->format->BytesPerPixel)
                {

                    case 2:
                        DrawFillIntACpy<uint16_t>(src, sRect, dst, dRect);
                        break;

                    case 4:
                        DrawFillIntACpy<uint32_t>(src, sRect, dst, dRect);
                        break;

                    default:
                    break;
                }
            }
            else
                DrawFillIntCpy(src, sRect, dst, dRect);
        }  
    }
    else
    {
        SDL_Rect lsrc = sRect;
        SDL_Rect ldst;
        for(ldst.y = dRect.top; ldst.y < dRect.bottom; ldst.y += lsrc.h)
        {
            if (dRect.bottom - ldst.y < lsrc.h)
                lsrc.h = dRect.bottom - ldst.y;

            lsrc.w = sRect.Width();

            for(ldst.x = dRect.left; ldst.x < dRect.right; ldst.x += lsrc.w)
            {
                if (dRect.right - ldst.x < lsrc.w)
                    lsrc.w = dRect.right - ldst.x;

                SDL_BlitSurface(src, &lsrc, dst, &ldst);
            }
        }
    }
}

void GFXEngine::Draw(SDL_Surface *src, const Common::Rect &sRect, SDL_Surface *dst, Common::Point dPoint)
{
    SDL_Rect Ssrc = sRect;
    SDL_Rect Sdst = dPoint;
    SDL_BlitSurface(src, &Ssrc, dst, &Sdst);
}

void GFXEngine::Draw(SDL_Surface *src, const Common::Rect &sRect, SDL_Surface *dst, Common::PointRect dRect)
{
    SDL_Rect Ssrc = sRect;
    if (Ssrc.w > dRect.w)
        Ssrc.w = dRect.w;
    if (Ssrc.h > dRect.h)
        Ssrc.h = dRect.h;
    SDL_Rect Sdst = dRect;
    SDL_BlitSurface(src, &Ssrc, dst, &Sdst);
}

void GFXEngine::GetGlPixTypeFmt(GLint *format, GLint *type)
{
    *format = _glPixfmt;
    *type = _glPixtype;
}

void GFXEngine::RecreateScreenSurface()
{
    if ( ScreenSurface )
        SDL_FreeSurface(ScreenSurface);

    ScreenSurface = SDL_CreateRGBSurface(0, _resolution.x, _resolution.y, _pixfmt->BitsPerPixel, _pixfmt->Rmask, _pixfmt->Gmask, _pixfmt->Bmask, _pixfmt->Amask);

    if ( !screenTex )
        glGenTextures(1, &screenTex);

    _states.Tex = screenTex;
    SetRenderStates(0);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenSurface->w, ScreenSurface->h, 0, _glPixfmt, _glPixtype, NULL);
}

void GFXEngine::DrawVtxQuad(const std::array<GFX::TVertex, 4> &vtx)
{
    static const uint16_t indexes[6] = {0, 1, 2, 0, 2, 3};
    
    if (_vbo)
    {
        if (!_stdQuadDataBuf)
        {
            Glext::GLGenBuffers(1, &_stdQuadDataBuf);
            Glext::GLBindBuffer(GL_ARRAY_BUFFER, _stdQuadDataBuf);
            Glext::GLBufferData(GL_ARRAY_BUFFER, sizeof(TVertex) * vtx.size(), NULL, GL_STREAM_DRAW);
        }
        
        if (!_stdQuadIndexBuf)
        {
            Glext::GLGenBuffers(1, &_stdQuadIndexBuf);
            Glext::GLBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _stdQuadIndexBuf);
            Glext::GLBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indexes), &indexes, GL_STATIC_DRAW);
        }
        
        _states.DataBuf = _stdQuadDataBuf;
        _states.IndexBuf = _stdQuadIndexBuf;
        
        SetRenderStates(0);
        
        Glext::GLBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(TVertex) * vtx.size(), vtx.data()); 
        
        if (_lastStates.Prog.PosLoc != -1)
            Glext::GLVertexAttribPointer(_lastStates.Prog.PosLoc, 3, GL_FLOAT, GL_FALSE,  sizeof(TVertex), (void *)offsetof(TVertex, Pos));  
        if (_lastStates.Prog.ColorLoc != -1)
            Glext::GLVertexAttribPointer(_lastStates.Prog.ColorLoc, 4, GL_FLOAT, GL_FALSE,  sizeof(TVertex), (void *)offsetof(TVertex, Color));
        if (_lastStates.Prog.UVLoc != -1)
            Glext::GLVertexAttribPointer(_lastStates.Prog.UVLoc, 2, GL_FLOAT, GL_FALSE,  sizeof(TVertex), (void *)offsetof(TVertex, TexCoord));
        
        CommitUBOParameters();    
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, NULL);
    }
    else
    {
        glVertexPointer(3, GL_FLOAT, sizeof(TVertex), &vtx[0].Pos);
        glColorPointer(4, GL_FLOAT, sizeof(TVertex), &vtx[0].Color);
        glTexCoordPointer(2, GL_FLOAT, sizeof(TVertex), &vtx[0].TexCoord);

        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, &indexes);
    }
}

void GFXEngine::DrawScreenSurface()
{
    GfxStates save = _states;
    
    Common::Point scrSz = System::GetResolution();
#ifdef __ANDROID__
    Android::SetViewport();
#else
    glViewport(0, 0, scrSz.x, scrSz.y);
#endif
    
    SetProjectionMatrix( mat4x4f() );
    SetModelViewMatrix( mat4x4f() );
    
    _states.DepthTest = false;
    _states.Zwrite = false;
    _states.AlphaBlend = true;
    _states.SrcBlend = GL_SRC_ALPHA;
    _states.DstBlend = GL_ONE_MINUS_SRC_ALPHA;
    _states.Tex = screenTex;
    _states.TexBlend = 2;
    _states.Prog = _stdShaderProg;
    _states.AlphaTest = false;
    _states.Shaded = true;
    _states.LinearFilter = true;

    SetRenderStates(0);
    
    // Will be binded with SetRenderStates
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ScreenSurface->w, ScreenSurface->h, _glPixfmt, _glPixtype, ScreenSurface->pixels);

    static std::array<TVertex, 4> vtx = {
        GFX::TVertex( vec3f(-1.0,  1.0, 0.0), tUtV(0.0, 0.0) ),
        GFX::TVertex( vec3f(-1.0, -1.0, 0.0), tUtV(0.0, 1.0) ),
        GFX::TVertex( vec3f( 1.0, -1.0, 0.0), tUtV(1.0, 1.0) ),
        GFX::TVertex( vec3f( 1.0,  1.0, 0.0), tUtV(1.0, 0.0) )
    };
    
    DrawVtxQuad(vtx);
    
    _states = save;
}

#ifdef __ANDROID__
namespace {
// The fill never keeps a pointer to the background surface beyond the blit that
// noted it: shell bitmaps (and loading screens) are freed when switching between
// missions and menus. Everything needed is captured while the surface is alive.
struct WideFillKey {
    uint32_t sum = 0;
    int w = 0, h = 0, W = 0, left = 0, style = 0;
    bool operator==(const WideFillKey &o) const { return sum == o.sum && w == o.w && h == o.h && W == o.W && left == o.left && style == o.style; }
};
struct WideFillCache {
    WideFillKey key;
    GLuint tex = 0;
    uint32_t used = 0;
};
std::array<WideFillCache, 6> wideCache;
bool wideNoted = false, wideLastNoted = false;
WideFillKey wideKey, wideLastKey;           // values only, never surface pointers
std::vector<uint8_t> widePending;           // built image waiting for upload
WideFillKey widePendingKey;
uint32_t wideFrame = 0;

uint32_t SurfaceSum(SDL_Surface *s)
{
    // Cheap identity of the picture content.
    if (!s || !s->pixels || !s->format || s->w <= 0 || s->h <= 0) return 0;
    uint32_t h = 2166136261u ^ uint32_t(s->w * 31 + s->h);
    SDL_LockSurface(s);
    const uint8_t *p = static_cast<const uint8_t *>(s->pixels);
    const int bpp = s->format->BytesPerPixel;
    for (int i = 0; i < 64; ++i) {
        int y = (i * 37) % s->h, x = (i * 101) % s->w;
        const uint8_t *px = p + size_t(y) * s->pitch + x * bpp;
        for (int b = 0; b < bpp; ++b) h = (h ^ px[b]) * 16777619u;
    }
    SDL_UnlockSurface(s);
    return h;
}

bool WideGeometry(int &W, int &left)
{
    const SDL_Rect vp = Android::Viewport();
    const Common::Point win = System::GetResolution();
    if (vp.w <= 0 || vp.h <= 0 || (vp.x <= 1 && vp.x + vp.w >= win.x - 1)) return false; // no side bars
    left = int(std::lround(vp.x * 640.0 / vp.w));
    W = int(std::lround(win.x * 640.0 / vp.w));
    return W > 640 + 2;
}
}

void GFXEngine::NoteAndroidBackground(SDL_Surface *bg, int style)
{
    if (wideNoted || !bg || Android::InMission()) return; // first full-screen layer = base picture
    WideFillKey key;
    if (!WideGeometry(key.W, key.left)) return;
    key.sum = SurfaceSum(bg);
    key.w = bg->w; key.h = bg->h; key.style = style;
    wideNoted = true;
    wideKey = key;
    for (auto &c : wideCache) if (c.tex && c.key == key) return;
    if (!widePending.empty() && widePendingKey == key) return;
    // Build now, while the surface is guaranteed to be alive.
    widePending = style == 1 ? AndroidWideFill::BuildSideMonitors(bg, key.W, key.left)
                             : AndroidWideFill::BuildConsoleRails(bg, key.W, key.left);
    widePendingKey = key;
}

void GFXEngine::DrawAndroidWideFill()
{
    ++wideFrame;
    // Tolerate one frame without a background redraw (values, not pointers).
    const bool have = wideNoted || wideLastNoted;
    const WideFillKey key = wideNoted ? wideKey : wideLastKey;
    wideLastNoted = wideNoted; wideLastKey = wideKey;
    wideNoted = false;
    if (!have || Android::InMission()) return;

    int W = 0, left = 0;
    if (!WideGeometry(W, left) || W != key.W || left != key.left) return;
    const SDL_Rect vp = Android::Viewport();
    const Common::Point win = System::GetResolution();

    WideFillCache *slot = nullptr;
    for (auto &c : wideCache)
        if (c.tex && c.key == key) { slot = &c; break; }

    GfxStates save = _states;
    if (!slot) {
        if (widePending.empty() || !(widePendingKey == key)) return;
        slot = &wideCache[0];
        for (auto &c : wideCache) if (c.used < slot->used) slot = &c;
        if (!slot->tex) glGenTextures(1, &slot->tex);
        _states.Tex = slot->tex;
        SetRenderStates(0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, key.W, 480, 0, GL_RGBA, GL_UNSIGNED_BYTE, widePending.data());
        slot->key = key;
        widePending.clear(); widePending.shrink_to_fit();
        SDL_Log("DroidUA widescreen fill built: %dx480 (left %d)", key.W, key.left);
    }
    slot->used = wideFrame;

    // Draw across the whole window width, same vertical band as the 4:3 viewport.
    glViewport(0, win.y - vp.y - vp.h, win.x, vp.h);
    SetProjectionMatrix( mat4x4f() );
    SetModelViewMatrix( mat4x4f() );
    _states.DepthTest = false;
    _states.Zwrite = false;
    _states.AlphaBlend = false;
    _states.Tex = slot->tex;
    _states.TexBlend = 2;
    _states.Prog = _stdShaderProg;
    _states.AlphaTest = false;
    _states.Shaded = true;
    _states.Fog = false;
    _states.LinearFilter = true;
    SetRenderStates(0);
    // Only the two side strips: the 4:3 centre may hold 3D (briefing model).
    auto strip = [&](float x0, float x1, float u0, float u1) {
        if (x1 <= x0) return;
        std::array<TVertex, 4> vtx = {
            GFX::TVertex( vec3f(x0,  1.0, 0.0), tUtV(u0, 0.0) ),
            GFX::TVertex( vec3f(x0, -1.0, 0.0), tUtV(u0, 1.0) ),
            GFX::TVertex( vec3f(x1, -1.0, 0.0), tUtV(u1, 1.0) ),
            GFX::TVertex( vec3f(x1,  1.0, 0.0), tUtV(u1, 0.0) )
        };
        DrawVtxQuad(vtx);
    };
    const float lx = -1.0f + 2.0f * vp.x / win.x, rx = -1.0f + 2.0f * (vp.x + vp.w) / win.x;
    strip(-1.0f, lx, 0.0f, float(left) / W);
    strip(rx, 1.0f, float(left + 640) / W, 1.0f);
    _states = save;
    SetRenderStates(0);
    Android::SetViewport();
}
#endif

#ifdef __ANDROID__
namespace {
struct TouchTex { GLuint tex = 0; int w = 0, h = 0; };
std::map<std::string, TouchTex> touchTextures;
}

void GFXEngine::DrawAndroidTouchOverlay()
{
    static std::vector<NC_STACK_winp::AndroidTouchItem> items;
    NC_STACK_winp::AndroidCollectTouchControls(items);
    int gx = 0, gy = 0, gw = 0, gh = 0;
    const bool grip = UAAndroidMapGripRect(gx, gy, gw, gh);
    if (items.empty() && !grip) return;

    const SDL_Rect vp = Android::Viewport();
    const Common::Point win = System::GetResolution();
    const Common::Point logical = GetScreenSize();
    if (vp.w <= 0 || vp.h <= 0 || logical.x <= 0 || logical.y <= 0) return;
    const float sx = float(vp.w) / logical.x, sy = float(vp.h) / logical.y;
    const float scale = sy;
    const float opacity = Android::TouchOpacity() / 100.0f;
    const float labelOpacity = std::min(1.0f, opacity * 1.5f + 40.0f / 255.0f);

    if (touchTextures.size() > 96) { // layout/scale changed a lot: start over
        for (auto &t : touchTextures) glDeleteTextures(1, &t.second.tex);
        touchTextures.clear();
    }

    GfxStates save = _states;
    auto texture = [&](const std::string &key, const std::function<AndroidTouchSprites::Sprite()> &make) -> TouchTex * {
        auto it = touchTextures.find(key);
        if (it != touchTextures.end()) return it->second.tex ? &it->second : nullptr;
        AndroidTouchSprites::Sprite sp = make();
        TouchTex &t = touchTextures[key];
        if (sp.w <= 0 || sp.h <= 0) return nullptr;
        glGenTextures(1, &t.tex);
        _states.Tex = t.tex;
        SetRenderStates(0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sp.w, sp.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, sp.rgba.data());
        t.w = sp.w; t.h = sp.h;
        return &t;
    };

    glViewport(0, 0, win.x, win.y);
    SetProjectionMatrix( mat4x4f() );
    SetModelViewMatrix( mat4x4f() );
    _states.DepthTest = false;
    _states.Zwrite = false;
    _states.AlphaBlend = true;
    _states.SrcBlend = GL_ONE;                 // sprites are premultiplied
    _states.DstBlend = GL_ONE_MINUS_SRC_ALPHA;
    _states.TexBlend = 2;
    _states.Prog = _stdShaderProg;
    _states.AlphaTest = false;
    _states.Shaded = true;
    _states.Fog = false;
    _states.LinearFilter = true;

    // Draw a texture with its top-left at window pixel (x, y), 1:1 pixel size.
    auto quad = [&](TouchTex *t, float x, float y, float alpha) {
        if (!t) return;
        x = std::round(x); y = std::round(y);
        const float x0 = -1.0f + 2.0f * x / win.x, x1 = -1.0f + 2.0f * (x + t->w) / win.x;
        const float y0 = 1.0f - 2.0f * y / win.y, y1 = 1.0f - 2.0f * (y + t->h) / win.y;
        const TGLColor c(alpha, alpha, alpha, alpha);
        std::array<TVertex, 4> vtx = {
            GFX::TVertex( vec3f(x0, y0, 0.0), tUtV(0.0, 0.0), c ),
            GFX::TVertex( vec3f(x0, y1, 0.0), tUtV(0.0, 1.0), c ),
            GFX::TVertex( vec3f(x1, y1, 0.0), tUtV(1.0, 1.0), c ),
            GFX::TVertex( vec3f(x1, y0, 0.0), tUtV(1.0, 0.0), c )
        };
        _states.Tex = t->tex;
        SetRenderStates(0);
        DrawVtxQuad(vtx);
    };

    if (grip)
    {
        const float size = gw * scale;
        TouchTex *g = texture("g" + std::to_string(int(std::lround(size))), [&] { return AndroidTouchSprites::Grip(size, scale); });
        if (g) quad(g, vp.x + gx * sx, vp.y + gy * sy, 0.95f);
    }

    for (const auto &it : items)
    {
        const float cx = vp.x + it.x * sx, cy = vp.y + it.y * sy, r = it.r * scale;
        if (it.hh > it.r) // MOVE slider
        {
            const float hh = it.hh * scale;
            const std::string key = "s" + std::to_string(int(std::lround(r * 4))) + "x" + std::to_string(int(std::lround(hh * 4))) + (it.held ? "h" : "n");
            TouchTex *face = texture(key, [&] { return AndroidTouchSprites::Slider(r, hh, scale, it.held); });
            if (face) quad(face, cx - face->w / 2.0f, cy - face->h / 2.0f, opacity);
            TouchTex *knob = texture("k" + std::to_string(int(std::lround(scale * 100))), [&] { return AndroidTouchSprites::Knob(scale); });
            if (knob) quad(knob, cx - knob->w / 2.0f, vp.y + it.knobY * sy - knob->h / 2.0f, opacity);
            const int maxW = int(it.r * 1.75f * scale);
            TouchTex *label = texture("l" + it.label + "#" + std::to_string(maxW) + "#" + std::to_string(int(std::lround(scale * 100))),
                                      [&] { return AndroidTouchSprites::Label(it.label, scale, float(maxW)); });
            if (label) quad(label, cx - label->w / 2.0f, cy + hh - r * 0.35f - label->h, labelOpacity);
            continue;
        }
        const int rKey = int(std::lround(r * 4));
        const std::string base = "c" + std::to_string(rKey) + (it.pad ? "p" : "b") + (it.held ? "h" : "n");
        TouchTex *face = texture(base, [&] { return AndroidTouchSprites::Control(rKey / 4.0f, scale, it.pad, it.held); });
        if (face) quad(face, cx - face->w / 2.0f, cy - face->h / 2.0f, opacity);

        if (it.pad)
        {
            TouchTex *knob = texture("k" + std::to_string(int(std::lround(scale * 100))), [&] { return AndroidTouchSprites::Knob(scale); });
            if (knob) quad(knob, vp.x + it.knobX * sx - knob->w / 2.0f, vp.y + it.knobY * sy - knob->h / 2.0f, opacity);
        }

        const int maxW = int(it.r * 1.7f * scale);
        TouchTex *label = texture("l" + it.label + "#" + std::to_string(maxW) + "#" + std::to_string(int(std::lround(scale * 100))),
                                  [&] { return AndroidTouchSprites::Label(it.label, scale, float(maxW)); });
        if (label)
        {
            float ly = cy - label->h / 2.0f;
            if (it.pad)
            {
                ly = cy + r * 0.55f + scale;
                if (ly + label->h > cy + r - scale) ly = cy + r - scale - label->h;
            }
            quad(label, cx - label->w / 2.0f, ly, labelOpacity);
        }
    }

    _states = save;
    SetRenderStates(0);
    Android::SetViewport();
}
#endif

uint32_t GFXEngine::CompileShader(int32_t type, const std::string &string)
{
    GLuint sh = Glext::GLCreateShader(type);
    if (!sh)
        return 0;
    
    const GLchar *source = (const GLchar *)string.c_str();
    
    Glext::GLShaderSource(sh, 1, &source, 0);
    Glext::GLCompileShader(sh);
    
    GLint tmpvar;
    Glext::GLGetShaderiv(sh, GL_COMPILE_STATUS, &tmpvar);
    
    if (tmpvar == GL_FALSE)
    {
        Glext::GLGetShaderiv(sh, GL_INFO_LOG_LENGTH, &tmpvar);
        if (tmpvar > 0)
        {
            char *logbuff = new char[tmpvar + 2];
            Glext::GLGetShaderInfoLog(sh, tmpvar, NULL, logbuff);
            printf("Shader error: %s\n", logbuff);
            log_d3dlog("Shader error: %s\n", logbuff);
            delete[] logbuff;
        }
        Glext::GLDeleteShader(sh);
        return 0;
    }
    
    return sh;
}


uint32_t GFXEngine::LoadShader(int32_t type, const std::string &fl)
{
    FSMgr::FileHandle *f = FSMgr::iDir::openFileAlloc(fl, "rb");
    if (!f)
        return 0;

    f->seek(0, SEEK_END);
    size_t sz = f->tell();
    f->seek(0, SEEK_SET);

    char *tmp = new char[sz];
    f->read(tmp, sz);

    std::string b;
    b.assign(tmp, sz);

    delete[] tmp;

    delete f;
    
    return CompileShader(type, b);
}

void GFXEngine::DrawFBO()
{
    GfxStates save = _states;
    
    Common::Point scrSz = System::GetResolution();
#ifdef __ANDROID__
    Android::SetViewport();
#else
    glViewport(0, 0, scrSz.x, scrSz.y);
#endif
        
    SetProjectionMatrix( mat4x4f() );
    SetModelViewMatrix( mat4x4f() );
    
    _states.DepthTest = false;
    _states.Zwrite = false;
    _states.AlphaBlend = true;
    
    if (_fboBlend == 0)
    {
        _states.SrcBlend = GL_ONE;
        _states.DstBlend = GL_ONE_MINUS_SRC_ALPHA;
    }
    else if (_fboBlend == 1)
    {
        _states.SrcBlend = GL_SRC_ALPHA;
        _states.DstBlend = GL_ONE_MINUS_SRC_ALPHA;
    }
    
    _states.Tex = _fboTex;
    _states.TexBlend = 2;
    _states.Prog = _colorEffectsShaderProg;
    
    _states.AlphaTest = false;
    _states.Shaded = true;
    _states.LinearFilter = true;

    // Apply texture and program
    SetRenderStates(0);
    
    if (_colorEffectsShaderProg.NormLoc >= 0)
        Glext::GLUniform3f(_colorEffectsShaderProg.NormLoc, _normClr.x, _normClr.y, _normClr.z);
    
    if (_colorEffectsShaderProg.InvLoc >= 0)
        Glext::GLUniform3f(_colorEffectsShaderProg.InvLoc, _invClr.x, _invClr.y, _invClr.z);
    
    if (_colorEffectsShaderProg.RandLoc >= 0)
        Glext::GLUniform1i(_colorEffectsShaderProg.RandLoc, rand());
    
    if (_colorEffectsShaderProg.ScrSizeLoc >= 0)
        Glext::GLUniform2i(_colorEffectsShaderProg.ScrSizeLoc, scrSz.x, scrSz.y);
    
    if (_colorEffectsShaderProg.MillisecsLoc >= 0)
        Glext::GLUniform1i(_colorEffectsShaderProg.MillisecsLoc, SDL_GetTicks());
    
    static std::array<TVertex, 4> vtx = {
        GFX::TVertex( vec3f(-1.0,  1.0, 0.0), tUtV(0.0, 1.0) ),
        GFX::TVertex( vec3f(-1.0, -1.0, 0.0), tUtV(0.0, 0.0) ),
        GFX::TVertex( vec3f( 1.0, -1.0, 0.0), tUtV(1.0, 0.0) ),
        GFX::TVertex( vec3f( 1.0,  1.0, 0.0), tUtV(1.0, 1.0) )
    };
    
    DrawVtxQuad(vtx);
    
    _states = save;
}
 
void GFXEngine::SetFBOBlending(int mode)
{
    _fboBlend = mode;
}

void GFXEngine::UpdateFBOSizes()
{
    if (_colorEffects)
    {
        Common::Point scrSz = System::GetResolution();

        _states.Tex = _fboTex;
        SetRenderStates(0);
        
        glTexImage2D(GL_TEXTURE_2D, 0, FBOTEXTYPE, scrSz.x, scrSz.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

        Glext::GLBindRenderbuffer(GL_RENDERBUFFER, _fbod);
        Glext::GLRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, scrSz.x, scrSz.y);
        Glext::GLBindRenderbuffer(GL_RENDERBUFFER, 0);
    }
}

SDL_Color GFXEngine::Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    SDL_Color tmp;
    tmp.a = a;
    tmp.r = r;
    tmp.g = g;
    tmp.b = b;
    return tmp;
}

bool GFXEngine::ColorCmp(const SDL_Color &a, const SDL_Color &b)
{
    if (a.a != b.a || a.r != b.r || a.g != b.g || a.b != b.b)
        return false;
    return true;
}

float GFXEngine::GetColorEffectPower(int id)
{
    int32_t pwr = 0;
    switch(id)
    {
        default:
        case 0:
            return 1.0;
        
        case 1:
            pwr = System::IniConf::GfxColorEffPower1.Get<int32_t>();
            break;
        
        case 2:
            pwr = System::IniConf::GfxColorEffPower2.Get<int32_t>();
            break;
        
        case 3:
            pwr = System::IniConf::GfxColorEffPower3.Get<int32_t>();
            break;
        
        case 4:
            pwr = System::IniConf::GfxColorEffPower4.Get<int32_t>();
            break;
            
        case 5:
            pwr = System::IniConf::GfxColorEffPower5.Get<int32_t>();
            break;
            
        case 6:
            pwr = System::IniConf::GfxColorEffPower6.Get<int32_t>();
            break;
            
        case 7:
            pwr = System::IniConf::GfxColorEffPower7.Get<int32_t>();
            break;
    }
    
    if (pwr < 0)
        pwr = 0;
    
    if (pwr > 100)
        pwr = 100;
    
    return (float)pwr / 100.0;
}

Common::Point GFXEngine::ConvertPosTo2DStuff(const Common::Point &pos)
{
#ifdef __ANDROID__
    return Android::LogicalPoint(pos);
#endif
    Common::Point real = System::GetResolution();

    if (real == _resolution)
        return pos;
    
    Common::Point t( pos.x * _resolution.x / real.x,
                     pos.y * _resolution.y / real.y );

    if (t.x < 0)
        t.x = 0;
    if (t.x >= _resolution.x)
        t.x = _resolution.x - 1;

    if (t.y < 0)
        t.y = 0;
    if (t.y >= _resolution.y)
        t.y = _resolution.y - 1;

    return t;
}

TMesh::TMesh(const TMesh &b)
{
    Mat = b.Mat;
    Vertexes = b.Vertexes;
    Indixes = b.Indixes;
    CoordsCache = b.CoordsCache;
    BoundBox = b.BoundBox;
    
    glDataBuf = 0;
    glIndexBuf = 0;
}

TMesh &TMesh::operator=(const TMesh& b)
{
    Mat = b.Mat;
    Vertexes = b.Vertexes;
    Indixes = b.Indixes;
    CoordsCache = b.CoordsCache;
    BoundBox = b.BoundBox;
    
    glDataBuf = 0;
    glIndexBuf = 0;
    
    return *this;
}

TMesh::~TMesh()
{
    GFX::Engine.MeshFreeVBO(this);
    
    // Just notify about still existing buffers
    if (glDataBuf || glIndexBuf)
        printf("TMesh still has buffers!\n");
}

void GFXEngine::MeshMakeVBO(TMesh *mesh)
{
    if (_vbo)
    {        
        if (!mesh->glDataBuf)
            Glext::GLGenBuffers(1, &mesh->glDataBuf);
        
        if (!mesh->glIndexBuf)
            Glext::GLGenBuffers(1, &mesh->glIndexBuf);
                
        Glext::GLBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->glIndexBuf);
        Glext::GLBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh->Indixes.size() * sizeof(IndexType), mesh->Indixes.data(), GL_STATIC_DRAW);
        
        Glext::GLBindBuffer(GL_ARRAY_BUFFER, mesh->glDataBuf);
        int32_t vtxDataSz = mesh->Vertexes.size() * sizeof(TVertex);
        int32_t coordDataSz = mesh->Vertexes.size() * sizeof(tUtV);
        Glext::GLBufferData(GL_ARRAY_BUFFER, vtxDataSz + mesh->CoordsCache.size() * coordDataSz, NULL, GL_STATIC_DRAW);
        
        int32_t off = 0;
        Glext::GLBufferSubData(GL_ARRAY_BUFFER, off, vtxDataSz, mesh->Vertexes.data());
        
        off += vtxDataSz;
        for (TCoordsCache &cch : mesh->CoordsCache)
        {
            Glext::GLBufferSubData(GL_ARRAY_BUFFER, off, coordDataSz, cch.Coords.data());
            cch.BufferPos = off;
            
            off += coordDataSz;
        }
        
        Glext::GLBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _lastStates.IndexBuf);
        Glext::GLBindBuffer(GL_ARRAY_BUFFER, _lastStates.DataBuf);  
    }
}

void GFXEngine::MeshFreeVBO(TMesh *mesh)
{
    if (_vbo)
    {
        if (mesh->glDataBuf)
        {
            Glext::GLDeleteBuffers(1, &mesh->glDataBuf);
            mesh->glDataBuf = 0;
        }
        
        if (mesh->glIndexBuf)
        {
            Glext::GLDeleteBuffers(1, &mesh->glIndexBuf);
            mesh->glIndexBuf = 0;
        }
        
//        if (mesh->glVao)
//        {
//            Glext::GLDeleteVertexArrays(1, &mesh->glVao);
//            mesh->glVao = 0;
//        }
    }
}

void GFXEngine::BindVBOParameters(TShaderProg &shader)
{
    if (_vbo)
    {
        uint32_t blockIndex = Glext::GLGetUniformBlockIndex(shader.ID, "Parameters");   
        if (blockIndex != GL_INVALID_INDEX)
            Glext::GLUniformBlockBinding(shader.ID, blockIndex, _vboParamsBlockBinding);
    }
}

void GFXEngine::CommitUBOParameters()
{
    if (_vbo && _vboStatesChanged)
    {
        _vboStatesChanged = false;
        
        Glext::GLBindBuffer(GL_UNIFORM_BUFFER, _vboParams);
        
        // Orphan ubo
        Glext::GLBufferData(GL_UNIFORM_BUFFER, _vboParamsSize, NULL, GL_STREAM_DRAW);
        
        Glext::GLBufferData(GL_UNIFORM_BUFFER, _vboParamsSize, &_vboStatesBlock, GL_STREAM_DRAW);
    }
}

void GFXEngine::SetProjectionMatrix(const mat4x4f &mat)
{
    mat4x4f tmp = mat.Transpose();
    if (_vbo)
    {
        _vboStatesBlock.Proj = tmp;
        _vboStatesChanged = true;
    }
    else
    {
        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf(&tmp.m00);
    }
}

void GFXEngine::SetModelViewMatrix(const mat4x4f &mat)
{
    mat4x4f tmp = mat.Transpose();
    if (_vbo)
    {
        _vboStatesBlock.View = tmp;
        _vboStatesChanged = true;
    }
    else
    {
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf(&tmp.m00);
    }
}

TShaderProg::TShaderProg(uint32_t id)
: ID(id)
{
    PosLoc = Glext::GLGetAttribLocation(ID, "vPos");
    UVLoc  = Glext::GLGetAttribLocation(ID, "vUV");
    ColorLoc = Glext::GLGetAttribLocation(ID, "vColor");
}

TColorEffectsProg::TColorEffectsProg(uint32_t id)
: TShaderProg(id)
{
    NormLoc = Glext::GLGetUniformLocation(ID, "normclr");
    InvLoc = Glext::GLGetUniformLocation(ID, "invclr");
    RandLoc = Glext::GLGetUniformLocation(ID, "randval");
    ScrSizeLoc = Glext::GLGetUniformLocation(ID, "screenSize");
    MillisecsLoc = Glext::GLGetUniformLocation(ID, "millisecs");
}

}