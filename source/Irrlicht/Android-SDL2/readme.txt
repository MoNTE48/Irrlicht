Build in this directory with:

export ANDROID_HOME=/path/to/android-sdk/
export PATH="/path/to/android-ndk:$ANDROID_HOME/tools:$ANDROID_HOME/platform-tools:$PATH"
export SDL2_PATH=/path/to/SDL2
export CPPFLAGS="-I/path/to/libjpeg/include -I/path/to/libpng/include"

ndk-build NDEBUG=1 APP_CPPFLAGS="$CPPFLAGS"

Compiled libraries will be available in Irrlicht/lib/Android-SDL2 directory.
