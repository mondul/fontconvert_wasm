if which emcc > /dev/null; then
  git submodule update --init
else
  echo "ERROR: Emscripten SDK not active.\n       Please go to the emsdk folder and run:\n       source ./emsdk_env.sh" >&2
  exit 1
fi

if [ ! -d lib ]; then
  mkdir lib
  cd lib
  emcmake cmake ../freetype
  emmake make
  cd ..
fi

rm -rf web
mkdir web
touch web/favicon.ico
emcc \
  -O3 \
  -Ifreetype/include \
  -Llib \
  -lfreetype \
  -sEXPORTED_FUNCTIONS=_fontconvert \
  --shell-file fontconvert.html \
  -o web/index.html \
  fontconvert.c
