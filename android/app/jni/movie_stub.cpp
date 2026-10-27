// Early Android prototype: cinematic playback is intentionally deferred.
#include "system/movie.h"
#include <SDL2/SDL.h>
namespace System {
TMovie TMovie::Instance;
TMovie::TMovie() = default;
TMovie::~TMovie() = default;
void TMovie::Init() { SDL_Log("OpenUA prototype: movie playback not built yet"); }
void TMovie::PlayMovie(const std::string &name, int) { SDL_Log("OpenUA prototype: skipping movie %s", name.c_str()); }
}
