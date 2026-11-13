# Building DroidUA

DroidUA is the OpenUA engine (`src/`) plus an Android app (`android/`). The desktop build of OpenUA is unchanged; see [`README-OpenUA.md`](README-OpenUA.md).

## Toolchain

| Tool | Version |
|---|---|
| JDK | 21 |
| Android SDK | compileSdk 35, build-tools 36.0.0 |
| Android NDK | 30.0.16248370 |
| CMake | 4.1.2 |
| ABI | arm64-v8a only |

Native code is built `RelWithDebInfo` (optimised) even for the debug APK.

## Folder layout

The CMake project in `android/app/jni/` expects the library sources **next to** this repository, in a sibling folder called `Android`:

```
<workspace>/
  UA_source/            <- this repository
  Android/
    SDL2/               <- SDL2 release-2.32.10
    Dependencies/
      SDL_image  SDL_net  SDL_ttf  freetype  gl4es  lua  ogg  openal-soft  vorbis
```

## Library versions

| Library | Repository | Commit |
|---|---|---|
| SDL2 | https://github.com/libsdl-org/SDL | `release-2.32.10` |
| SDL_image | https://github.com/libsdl-org/SDL_image | `c1bf2245b0ba63a25afe2f8574d305feca25af77` |
| SDL_net | https://github.com/libsdl-org/SDL_net | `669e75b84632e2c6cc5c65974ec9e28052cb7a4e` |
| SDL_ttf | https://github.com/libsdl-org/SDL_ttf | `2a891473eaf05ba1707a4b7913e6c4db7de7458a` |
| FreeType | https://github.com/freetype/freetype | `42608f77f20749dd6ddc9e0536788eaad70ea4b5` |
| gl4es | https://github.com/ptitSeb/gl4es | `1891af323d4547487296e50ba5932bde65c7b345` + patch below |
| Lua | https://github.com/lua/lua | `6e22fedb74cf0c9b6656e9fce8b7331db847c605` |
| Ogg | https://github.com/xiph/ogg | `e1774cd77f471443541596e09078e78fdc342e4f` |
| Vorbis | https://github.com/xiph/vorbis | `0657aee69dec8508a0011f47f3b69d7538e9d262` |
| OpenAL Soft | https://github.com/kcat/openal-soft | `dc7d7054a5b4f3bec1dc23a42fd616a0847af948` |

pl_mpeg is included in `android/third_party/`.

### gl4es patch

Apply [`android/patches/gl4es-droidua.patch`](android/patches/gl4es-droidua.patch) inside `Android/Dependencies/gl4es`:

```
git apply ../../UA_source/android/patches/gl4es-droidua.patch
```

It exports `gl4es_flush_psa()` so the shader cache can be saved on Android.

## Build

1. Create `android/local.properties` with your SDK path: `sdk.dir=C\:/path/to/Android/Sdk`
2. Signing: the debug config uses `Android/debug.keystore`. Create your own development keystore there, or point Gradle at your own key.
3. From `android/`:

```
gradlew assembleDebug
```

The APK is written to `android/app/build/outputs/apk/debug/`.

## Game data

No game data is in this repository. For testing, import the OpenUA Standalone Package from Metropolis Dawn through the app's launcher (**Import game files**).
