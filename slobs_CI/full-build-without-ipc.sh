#!/bin/bash

set -eE

## SET UP ENVIRONMENT ##
PRODUCT_NAME="OBS-Studio"
COLOR_RED=$(/usr/bin/tput setaf 1)
COLOR_GREEN=$(/usr/bin/tput setaf 2)
COLOR_BLUE=$(/usr/bin/tput setaf 4)
COLOR_ORANGE=$(/usr/bin/tput setaf 3)
COLOR_RESET=$(/usr/bin/tput sgr0)

hr() {
    /bin/echo "${COLOR_BLUE}[${PRODUCT_NAME}] ${1}${COLOR_RESET}"
}

step() {
    /bin/echo "${COLOR_GREEN}  + ${1}${COLOR_RESET}"
}

info() {
    /bin/echo "${COLOR_ORANGE} + ${1}${COLOR_RESET}"
}

error() {
    /bin/echo "${COLOR_RED}  + ${1}${COLOR_RESET}"
}

exists() {
  /usr/bin/command -v "$1" >/dev/null 2>&1
}

ensure_dir() {
    [[ -n "${1}" ]] && /bin/mkdir -p "${1}" && builtin cd "${1}"
}


CHECKOUT_DIR="$(/usr/bin/git rev-parse --show-toplevel)"
DEPS_BUILD_DIR="${CHECKOUT_DIR}/../obs-build-dependencies"
BUILD_DIR="${BUILD_DIR:-build_node}"
BUILD_CONFIG=${BUILD_CONFIG:-RelWithDebInfo}
PACKED_BUILD="${CHECKOUT_DIR}/${BUILD_DIR}/packed_build"
CI_SCRIPTS="${CHECKOUT_DIR}/CI/scripts/macos"
CI_WORKFLOW="${CHECKOUT_DIR}/.github/workflows/main.yml"
CI_MACOS_CEF_VERSION=$(/bin/cat "${CI_WORKFLOW}" | /usr/bin/sed -En "s/[ ]+MACOS_CEF_BUILD_VERSION: '([0-9]+)'/\1/p")
CI_DEPS_VERSION=$(/bin/cat "${CI_WORKFLOW}" | /usr/bin/sed -En "s/[ ]+MACOS_DEPS_VERSION: '([0-9\-]+)'/\1/p")
CI_VLC_VERSION=$(/bin/cat "${CI_WORKFLOW}" | /usr/bin/sed -En "s/[ ]+VLC_VERSION: '([0-9\.]+)'/\1/p")
CI_SPARKLE_VERSION=$(/bin/cat "${CI_WORKFLOW}" | /usr/bin/sed -En "s/[ ]+SPARKLE_VERSION: '([0-9\.]+)'/\1/p")
CI_QT_VERSION=$(/bin/cat "${CI_WORKFLOW}" | /usr/bin/sed -En "s/[ ]+QT_VERSION: '([0-9\.]+)'/\1/p" | /usr/bin/head -1)
CI_MIN_MACOS_VERSION=$(/bin/cat "${CI_WORKFLOW}" | /usr/bin/sed -En "s/[ ]+MIN_MACOS_VERSION: '([0-9\.]+)'/\1/p")
NPROC="${NPROC:-$(sysctl -n hw.ncpu)}"
CURRENT_ARCH=$(uname -m)

info "root path = ${CHECKOUT_DIR}"

## OBS BUILD FROM SOURCE ##
configure_obs_build() {
    ensure_dir "${CHECKOUT_DIR}/${BUILD_DIR}"

    CUR_DATE=$(/bin/date +"%Y-%m-%d@%H%M%S")
    NIGHTLY_DIR="${CHECKOUT_DIR}/nightly-${CUR_DATE}"
    PACKAGE_NAME=$(/usr/bin/find . -name "*.dmg")

    if [ -d ./OBS.app ]; then
        ensure_dir "${NIGHTLY_DIR}"
        /bin/mv "../${BUILD_DIR}/OBS.app" .
        info "You can find OBS.app in ${NIGHTLY_DIR}"
    fi
    ensure_dir "${CHECKOUT_DIR}/${BUILD_DIR}"
    if ([ -n "${PACKAGE_NAME}" ] && [ -f ${PACKAGE_NAME} ]); then
        ensure_dir "${NIGHTLY_DIR}"
        /bin/mv "../${BUILD_DIR}/$(basename "${PACKAGE_NAME}")" .
        info "You can find ${PACKAGE_NAME} in ${NIGHTLY_DIR}"
    fi

    ensure_dir "${CHECKOUT_DIR}/${BUILD_DIR}"

    hr "Run CMAKE for OBS..."

    if [ -d ${PACKED_BUILD} ]; then
      rm -r ${PACKED_BUILD}
      mkdir ${PACKED_BUILD}
    fi
    cmake -DENABLE_SPARKLE_UPDATER=ON \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=${MIN_MACOS_VERSION:-${CI_MIN_MACOS_VERSION}} \
        -DCMAKE_INSTALL_PREFIX=$PACKED_BUILD \
        -DENABLE_SCRIPTING=OFF \
    	  -DENABLE_UI=OFF \
    	  -DDISABLE_UI=ON \
        -DSWIGDIR="/tmp/obsdeps" \
        -DDepsPath="/tmp/obsdeps" \
        -DVLCPath="${DEPS_BUILD_DIR}/vlc-${VLC_VERSION:-${CI_VLC_VERSION}}" \
        -DBUILD_BROWSER=ON \
        -DWITH_RTMPS=ON \
        -DCMAKE_BUILD_TYPE="${BUILD_CONFIG}" \
        -DBUILD_FOR_ELECTRON=ON \
        -DBUILD_BROWSER=OFF \
        ..

}

run_obs_build() {
    ensure_dir "${CHECKOUT_DIR}/${BUILD_DIR}"
    hr "Build OBS..."
#    /usr/bin/make -j${NPROC}
    cmake --build . --target install --config Debug -v
}

copy_dependency_lib() {
  ensure_dir "${PACKED_BUILD}"
  hr "copy lib"
  if [ -d Frameworks ]; then
    /bin/rm -r Frameworks
  fi
  /bin/mkdir Frameworks

  #cp mac virtual cam plugin
 /bin/cp -r ${CHECKOUT_DIR}/${BUILD_DIR}/plugins/mac-virtualcam/src/dal-plugin/vizard-mac-virtualcam.plugin ./data/obs-plugins/mac-virtualcam/

  #cp obs helper
  if [ -f ./obs-plugins/obs-browser.so ]; then
      #cp cef
      /bin/cp -R \
      "${DEPS_BUILD_DIR}/cef_binary_${MACOS_CEF_BUILD_VERSION:-${CI_MACOS_CEF_VERSION}}_macosx64/Release/Chromium Embedded Framework.framework" ./Frameworks/
      /bin/cp -f "${DEPS_BUILD_DIR}/cef_binary_${MACOS_CEF_BUILD_VERSION:-${CI_MACOS_CEF_VERSION}}_macosx64/Release/Chromium Embedded Framework.framework/Libraries/libEGL.dylib" ./obs-plugins/
      /bin/cp -f "${DEPS_BUILD_DIR}/cef_binary_${MACOS_CEF_BUILD_VERSION:-${CI_MACOS_CEF_VERSION}}_macosx64/Release/Chromium Embedded Framework.framework/Libraries/libGLESv2.dylib" ./obs-plugins/
      /bin/cp -f "${DEPS_BUILD_DIR}/cef_binary_${MACOS_CEF_BUILD_VERSION:-${CI_MACOS_CEF_VERSION}}_macosx64/Release/Chromium Embedded Framework.framework/Libraries/libswiftshader_libEGL.dylib" ./obs-plugins/
      /bin/cp -f "${DEPS_BUILD_DIR}/cef_binary_${MACOS_CEF_BUILD_VERSION:-${CI_MACOS_CEF_VERSION}}_macosx64/Release/Chromium Embedded Framework.framework/Libraries/libswiftshader_libGLESv2.dylib" ./obs-plugins/
      /bin/cp -f "${DEPS_BUILD_DIR}/cef_binary_${MACOS_CEF_BUILD_VERSION:-${CI_MACOS_CEF_VERSION}}_macosx64/Release/Chromium Embedded Framework.framework/Libraries/libvk_swiftshader.dylib" ./obs-plugins/
      /bin/cp -f "${DEPS_BUILD_DIR}/cef_binary_${MACOS_CEF_BUILD_VERSION:-${CI_MACOS_CEF_VERSION}}_macosx64/Release/Chromium Embedded Framework.framework/Libraries/vk_swiftshader_icd.json" ./obs-plugins/
    if ! [ "${CEF_MAC_BUILD_VERSION}" -le 3770 ]; then
          hr "cp obs helper"
          /bin/cp -R "../plugins/obs-browser/obs64 Helper.app" "./Frameworks/"
          /bin/cp -R "../plugins/obs-browser/obs64 Helper (GPU).app" "./Frameworks/"
          /bin/cp -R "../plugins/obs-browser/obs64 Helper (Plugin).app" "./Frameworks/"
          /bin/cp -R "../plugins/obs-browser/obs64 Helper (Renderer).app" "./Frameworks/"
    fi
    sudo install_name_tool -change \
        @executable_path/../Frameworks/Chromium\ Embedded\ Framework.framework/Chromium\ Embedded\ Framework \
        @rpath/Frameworks/Chromium\ Embedded\ Framework.framework/Chromium\ Embedded\ Framework \
        ./obs-plugins/obs-browser.so
  else
    error "not find obs-browser.so"
  fi
  if [ -f ./obs-plugins/obs-browser-page ]; then
    sudo install_name_tool -change \
        @executable_path/../Frameworks/Chromium\ Embedded\ Framework.framework/Chromium\ Embedded\ Framework \
        @rpath/Frameworks/Chromium\ Embedded\ Framework.framework/Chromium\ Embedded\ Framework \
        ./obs-plugins/obs-browser-page
  else
    error "not find obs-browser-page"
  fi

  #copy /tmp/obsdeps
  TMP_LIBS=(
    libavcodec.58.dylib
    libavcodec.dylib
    libavdevice.58.dylib
    libavdevice.dylib
    libavfilter.7.dylib
    libavfilter.dylib
    libavformat.58.dylib
    libavformat.dylib
    libavutil.56.dylib
    libavutil.dylib
    libfreetype.6.dylib
    libfreetype.dylib
    libjansson.4.dylib
    libjansson.dylib
    libluajit-5.1.2.1.0.dylib
    libluajit-5.1.2.dylib
    libluajit-5.1.dylib
    libmbedcrypto.2.24.0.dylib
    libmbedcrypto.5.dylib
    libmbedcrypto.dylib
    libmbedtls.13.dylib
    libmbedtls.2.24.0.dylib
    libmbedtls.dylib
    libmbedx509.1.dylib
    libmbedx509.2.24.0.dylib
    libpostproc.55.dylib
    librnnoise.0.dylib
    libswresample.3.dylib
    libswresample.dylib
    libswscale.5.dylib
    libswscale.dylib
    libx264.161.dylib
    libx264.dylib
  )


  for value in "${TMP_LIBS[@]}"
  do
      if [ -f /tmp/obsdeps/lib/${value} ];
      then
        /bin/cp -f /tmp/obsdeps/lib/${value} ./bin/
#        sudo install_name_tool -change /tmp/obsdeps/lib/${{TMP_LIBS[$i]} @executable_path/libavutil.56.dylib $PACKED_BUILD/bin/libavutil.56.dylib
      else
        error "${value} not find in tmp/obsdeps"
        exit
      fi
  done

  sudo install_name_tool -change /tmp/obsdeps/lib/libavcodec.58.dylib @rpath/libavcodec.58.dylib ./bin/libobs.0.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libavformat.58.dylib @rpath/libavformat.58.dylib ./bin/libobs.0.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libavutil.56.dylib @rpath/libavutil.56.dylib ./bin/libobs.0.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libswscale.5.dylib @rpath/libswscale.5.dylib ./bin/libobs.0.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libswresample.3.dylib @rpath/libswresample.3.dylib ./bin/libobs.0.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libjansson.4.dylib @rpath/libjansson.4.dylib ./bin/libobs.0.dylib

  sudo install_name_tool -change /tmp/obsdeps/lib/libavcodec.58.dylib @rpath/libavcodec.58.dylib ./bin/libavcodec.58.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libswresample.3.dylib @rpath/libswresample.3.dylib ./bin/libavcodec.58.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libavutil.56.dylib @rpath/libavutil.56.dylib ./bin/libavcodec.58.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libx264.161.dylib @rpath/libx264.161.dylib ./bin/libavcodec.58.dylib
  sudo install_name_tool -change /usr/local/opt/xz/lib/liblzma.5.dylib @rpath/liblzma.5.dylib ./bin/libavcodec.58.dylib
  sudo install_name_tool -change /usr/local/opt/xz/lib/liblzma.5.dylib @rpath/liblzma.5.dylib ./bin/libavformat.58.dylib
  sudo install_name_tool -change /usr/local/opt/xz/lib/liblzma.5.dylib @rpath/liblzma.5.dylib ./bin/libavfilter.7.dylib
  sudo install_name_tool -change /usr/local/opt/xz/lib/liblzma.5.dylib @rpath/liblzma.5.dylib ./bin/libavdevice.58.dylib

  sudo install_name_tool -change /tmp/obsdeps/lib/libavformat.58.dylib @rpath/libavformat.58.dylib ./bin/libavformat.58.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libavcodec.58.dylib @rpath/libavcodec.58.dylib ./bin/libavformat.58.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libswresample.3.dylib @rpath/libswresample.3.dylib ./bin/libavformat.58.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libavutil.56.dylib @rpath/libavutil.56.dylib ./bin/libavformat.58.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libx264.161.dylib @rpath/libx264.161.dylib ./bin/libavformat.58.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedtls.2.24.0.dylib @rpath/libmbedtls.2.24.0.dylib ./bin/libavformat.58.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedx509.2.24.0.dylib @rpath/libmbedx509.2.24.0.dylib ./bin/libavformat.58.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedcrypto.2.24.0.dylib @rpath/libmbedcrypto.2.24.0.dylib ./bin/libavformat.58.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedtls.13.dylib @rpath/libmbedtls.13.dylib ./bin/libavformat.58.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedx509.1.dylib @rpath/libmbedx509.1.dylib ./bin/libavformat.58.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedcrypto.5.dylib @rpath/libmbedcrypto.5.dylib ./bin/libavformat.58.dylib


  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedtls.13.dylib @rpath/libmbedtls.13.dylib ./bin/libavdevice.58.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedx509.1.dylib @rpath/libmbedx509.1.dylib ./bin/libavdevice.58.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedcrypto.5.dylib @rpath/libmbedcrypto.5.dylib ./bin/libavdevice.58.dylib

  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedtls.13.dylib @rpath/libmbedtls.13.dylib ./bin/libavfilter.7.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedx509.1.dylib @rpath/libmbedx509.1.dylib ./bin/libavfilter.7.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedcrypto.5.dylib @rpath/libmbedcrypto.5.dylib ./bin/libavfilter.7.dylib

  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedx509.1.dylib @rpath/libmbedx509.1.dylib ./bin/libmbedtls.13.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedcrypto.5.dylib @rpath/libmbedcrypto.5.dylib ./bin/libmbedtls.13.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedtls.2.24.0.dylib @rpath/libmbedtls.2.24.0.dylib ./bin/libmbedtls.13.dylib

  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedx509.1.dylib @rpath/libmbedx509.1.dylib ./bin/libmbedtls.2.24.0.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedcrypto.5.dylib @rpath/libmbedcrypto.5.dylib ./bin/libmbedtls.2.24.0.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedtls.2.24.0.dylib @rpath/libmbedtls.2.24.0.dylib ./bin/libmbedtls.2.24.0.dylib

  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedx509.2.24.0.dylib @rpath/libmbedx509.2.24.0.dylib  ./bin/libmbedx509.2.24.0.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedcrypto.5.dylib @rpath/libmbedcrypto.5.dylib ./bin/libmbedx509.2.24.0.dylib

  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedcrypto.5.dylib @rpath/libmbedcrypto.5.dylib ./bin/libmbedx509.1.dylib

  sudo install_name_tool -change /tmp/obsdeps/lib/libavutil.56.dylib @rpath/libavutil.56.dylib ./bin/libavutil.56.dylib

  sudo install_name_tool -change /tmp/obsdeps/lib/libswscale.5.dylib @rpath/libswscale.5.dylib ./bin/libswscale.5.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libavutil.56.dylib @rpath/libavutil.56.dylib ./bin/libswscale.5.dylib

  sudo install_name_tool -change /tmp/obsdeps/lib/libswresample.3.dylib @rpath/libswresample.3.dylib ./bin/libswresample.3.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libavutil.56.dylib @rpath/libavutil.56.dylib ./bin/libswresample.3.dylib

  sudo install_name_tool -change /tmp/obsdeps/lib/libavfilter.7.dylib @rpath/libavfilter.7.dylib ./bin/libavfilter.7.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libswscale.5.dylib @rpath/libswscale.5.dylib ./bin/libavfilter.7.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libpostproc.55.dylib @rpath/libpostproc.55.dylib ./bin/libavfilter.7.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libavformat.58.dylib @rpath/libavformat.58.dylib ./bin/libavfilter.7.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libavcodec.58.dylib @rpath/libavcodec.58.dylib ./bin/libavfilter.7.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libswresample.3.dylib @rpath/libswresample.3.dylib ./bin/libavfilter.7.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libavutil.56.dylib @rpath/libavutil.56.dylib ./bin/libavfilter.7.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libx264.161.dylib @rpath/libx264.161.dylib ./bin/libavfilter.7.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedtls.2.24.0.dylib @rpath/libmbedtls.2.24.0.dylib ./bin/libavfilter.7.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedx509.2.24.0.dylib @rpath/libmbedx509.2.24.0.dylib ./bin/libavfilter.7.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedcrypto.2.24.0.dylib @rpath/libmbedcrypto.2.24.0.dylib ./bin/libavfilter.7.dylib

  sudo install_name_tool -change /tmp/obsdeps/lib/libpostproc.55.dylib @rpath/libpostproc.55.dylib ./bin/libpostproc.55.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libavutil.56.dylib @rpath/libavutil.56.dylib ./bin/libpostproc.55.dylib

  sudo install_name_tool -change /tmp/obsdeps/lib/libavdevice.58.dylib @rpath/libavdevice.58.dylib ./bin/libavdevice.58.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libavfilter.7.dylib @rpath/libavfilter.7.dylib ./bin/libavdevice.58.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libswscale.5.dylib @rpath/libswscale.5.dylib ./bin/libavdevice.58.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libpostproc.55.dylib @rpath/libpostproc.55.dylib ./bin/libavdevice.58.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libavformat.58.dylib @rpath/libavformat.58.dylib ./bin/libavdevice.58.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libavcodec.58.dylib @rpath/libavcodec.58.dylib ./bin/libavdevice.58.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libswresample.3.dylib @rpath/libswresample.3.dylib ./bin/libavdevice.58.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libavutil.56.dylib @rpath/libavutil.56.dylib ./bin/libavdevice.58.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libx264.161.dylib @rpath/libx264.161.dylib ./bin/libavdevice.58.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedtls.2.24.0.dylib @rpath/libmbedtls.2.24.0.dylib ./bin/libavdevice.58.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedx509.2.24.0.dylib @rpath/libmbedx509.2.24.0.dylib ./bin/libavdevice.58.dylib
  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedcrypto.2.24.0.dylib @rpath/libmbedcrypto.2.24.0.dylib ./bin/libavdevice.58.dylib


  sudo /bin/cp -f /usr/local/Cellar/openssl@1.1/1.1.1k/lib/libcrypto.1.1.dylib ./bin/
  sudo /bin/cp -f /usr/local/opt/curl/lib/libcurl.4.dylib ./bin/
  sudo /bin/cp -f /usr/local/Cellar/berkeley-db/18.1.40/lib/libdb-18.1.dylib ./bin/
  sudo /bin/cp -f /usr/local/Cellar/fdk-aac/2.0.2/lib/libfdk-aac.2.dylib ./bin/
  sudo /bin/cp -f /usr/local/opt/freetype/lib/libfreetype.6.dylib ./bin/
  sudo /bin/cp -f /usr/local/opt/jack/lib/libjack.0.dylib ./bin/
  sudo /bin/cp -f /usr/local/opt/libpng/lib/libpng16.16.dylib ./bin/
  sudo /bin/cp -f /usr/local/Cellar/speexdsp/1.2.0/lib/libspeexdsp.1.dylib ./bin/
  sudo /bin/cp -f /usr/local/opt/openssl@1.1/lib/libssl.1.1.dylib ./bin/
  sudo /bin/cp -f /usr/local/opt/xz/lib/liblzma.5.dylib ./bin/

  sudo /bin/chmod u+w ./bin/libspeexdsp.1.dylib
  sudo /bin/chmod u+w ./bin/libssl.1.1.dylib
  sudo /bin/chmod u+w ./bin/libfdk-aac.2.dylib
  sudo /bin/chmod u+w ./bin/libcurl.4.dylib
  sudo /bin/chmod u+w ./bin/liblzma.5.dylib
  sudo /bin/chmod u+w ./bin/libcrypto.1.1.dylib

  sudo install_name_tool -change /usr/local/opt/openssl@1.1/lib/libssl.1.1.dylib @rpath/libssl.1.1.dylib ./bin/libdb-18.1.dylib
  sudo install_name_tool -change /usr/local/opt/openssl@1.1/lib/libcrypto.1.1.dylib @rpath/libcrypto.1.1.dylib ./bin/libdb-18.1.dylib

  sudo install_name_tool -change /usr/local/Cellar/openssl@1.1/1.1.1d/lib/libcrypto.1.1.dylib @rpath/libcrypto.1.1.dylib ./bin/libssl.1.1.dylib

  sudo install_name_tool -change /usr/local/opt/libpng/lib/libpng16.16.dylib @rpath/libpng16.16.dylib ./bin/libfreetype.6.dylib

  sudo install_name_tool -change /usr/local/opt/berkeley-db/lib/libdb-18.1.dylib @rpath/libdb-18.1.dylib ./bin/libjack.0.dylib

  sudo install_name_tool -change /usr/local/Cellar/openssl@1.1/1.1.1d/lib/libcrypto.1.1.dylib @rpath/libcrypto.1.1.dylib ./bin/libcrypto.1.1.dylib

  sudo install_name_tool -change /usr/local/opt/curl/lib/libcurl.4.dylib @rpath/libcurl.4.dylib ./obs-plugins/obs-outputs.so
  sudo install_name_tool -change /tmp/obsdeps/lib/libjansson.4.dylib @rpath/libjansson.4.dylib ./obs-plugins/obs-outputs.so
  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedtls.13.dylib @rpath/libmbedtls.13.dylib ./obs-plugins/obs-outputs.so
  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedtls.2.24.0.dylib @rpath/libmbedtls.2.24.0.dylib ./obs-plugins/obs-outputs.so
  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedx509.1.dylib @rpath/libmbedx509.1.dylib ./obs-plugins/obs-outputs.so
  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedcrypto.5.dylib @rpath/libmbedcrypto.5.dylib ./obs-plugins/obs-outputs.so
  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedcrypto.2.24.0.dylib  @rpath/libmbedcrypto.2.24.0.dylib ./obs-plugins/obs-outputs.so
  sudo install_name_tool -change /tmp/obsdeps/lib/libmbedx509.2.24.0.dylib @rpath/libmbedx509.2.24.0.dylib ./obs-plugins/obs-outputs.so

  sudo install_name_tool -change /usr/local/opt/curl/lib/libcurl.4.dylib @rpath/libcurl.4.dylib ./obs-plugins/rtmp-services.so
  sudo install_name_tool -change /tmp/obsdeps/lib/libjansson.4.dylib @rpath/libjansson.4.dylib ./obs-plugins/rtmp-services.so

  sudo install_name_tool -change /tmp/obsdeps/lib/libfreetype.6.dylib @rpath/libfreetype.6.dylib ./obs-plugins/text-freetype2.so

  sudo install_name_tool -change /tmp/obsdeps/lib/libspeexdsp.1.dylib  @rpath/libspeexdsp.1.dylib ./obs-plugins/obs-filters.so
  sudo install_name_tool -change /tmp/obsdeps/lib/librnnoise.0.dylib  @rpath/librnnoise.0.dylib ./obs-plugins/obs-filters.so

if [ -f "./obs-plugins/slobs-virtual-cam.so" ]; then
  sudo install_name_tool -change /tmp/obsdeps/lib/libavcodec.58.dylib @rpath/libavcodec.58.dylib ./obs-plugins/slobs-virtual-cam.so
  sudo install_name_tool -change /tmp/obsdeps/lib/libavfilter.7.dylib @rpath/libavfilter.7.dylib ./obs-plugins/slobs-virtual-cam.so
  sudo install_name_tool -change /tmp/obsdeps/lib/libavdevice.58.dylib @rpath/libavdevice.58.dylib ./obs-plugins/slobs-virtual-cam.so
  sudo install_name_tool -change /tmp/obsdeps/lib/libavutil.56.dylib @rpath/libavutil.56.dylib ./obs-plugins/slobs-virtual-cam.so
  sudo install_name_tool -change /tmp/obsdeps/lib/libswscale.5.dylib @rpath/libswscale.5.dylib ./obs-plugins/slobs-virtual-cam.so
  sudo install_name_tool -change /tmp/obsdeps/lib/libavformat.58.dylib @rpath/libavformat.58.dylib ./obs-plugins/slobs-virtual-cam.so
  sudo install_name_tool -change /tmp/obsdeps/lib/libswresample.3.dylib @rpath/libswresample.3.dylib ./obs-plugins/slobs-virtual-cam.so
fi

  sudo install_name_tool -change /tmp/obsdeps/lib/libx264.161.dylib @rpath/libx264.161.dylib ./obs-plugins/obs-x264.so
  sudo install_name_tool -change /tmp/obsdeps/lib/libx264.155.dylib @rpath/libx264.155.dylib ./obs-plugins/obs-x264.so

  sudo install_name_tool -change /tmp/obsdeps/lib/libavcodec.58.dylib @rpath/libavcodec.58.dylib ./obs-plugins/obs-ffmpeg.so
  sudo install_name_tool -change /tmp/obsdeps/lib/libavfilter.7.dylib @rpath/libavfilter.7.dylib ./obs-plugins/obs-ffmpeg.so
  sudo install_name_tool -change /tmp/obsdeps/lib/libavdevice.58.dylib @rpath/libavdevice.58.dylib ./obs-plugins/obs-ffmpeg.so
  sudo install_name_tool -change /tmp/obsdeps/lib/libavutil.56.dylib @rpath/libavutil.56.dylib ./obs-plugins/obs-ffmpeg.so
  sudo install_name_tool -change /tmp/obsdeps/lib/libswscale.5.dylib @rpath/libswscale.5.dylib ./obs-plugins/obs-ffmpeg.so
  sudo install_name_tool -change /tmp/obsdeps/lib/libavformat.58.dylib @rpath/libavformat.58.dylib ./obs-plugins/obs-ffmpeg.so
  sudo install_name_tool -change /tmp/obsdeps/lib/libswresample.3.dylib @rpath/libswresample.3.dylib ./obs-plugins/obs-ffmpeg.so

  sudo install_name_tool -change /tmp/obsdeps/lib/libavcodec.58.dylib @rpath/libavcodec.58.dylib ./bin/obs-ffmpeg-mux
  sudo install_name_tool -change /tmp/obsdeps/lib/libavutil.56.dylib @rpath/libavutil.56.dylib ./bin/obs-ffmpeg-mux
  sudo install_name_tool -change /tmp/obsdeps/lib/libavformat.58.dylib @rpath/libavformat.58.dylib ./bin/obs-ffmpeg-mux

  sudo install_name_tool -change /tmp/obsdeps/lib/libfreetype.6.dylib @rpath/libfreetype.6.dylib ./obs-plugins/text-freetype2.so

  sudo install_name_tool -change /usr/local/opt/fdk-aac/lib/libfdk-aac.2.dylib @rpath/libfdk-aac.2.dylib ./obs-plugins/obs-libfdk.so

}

configure_obs_build
run_obs_build
copy_dependency_lib
step "Finish"
