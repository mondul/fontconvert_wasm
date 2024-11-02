if which emcc > /dev/null; then
    git submodule update --init
else
    echo "ERROR: Emscripten SDK not active.\n       Please go to the emsdk folder and run:\n       source ./emsdk_env.sh" >&2
    exit 1
fi

rm -rf build web
mkdir build
mkdir web

pushd build
CMAKE_INSTALL_PREFIX=freetype emcmake cmake ../freetype
emmake make
emmake make install
popd

touch web/favicon.ico
emcc -O3 -Ibuild/freetype/include/freetype2 -Lbuild/freetype/lib -lfreetype fontconvert.c -o web/index.html --shell-file fontconvert.html
