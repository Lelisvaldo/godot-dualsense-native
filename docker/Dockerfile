# syntax=docker/dockerfile:1
# Build toolchain for godot-dualsense-native: godot-cpp (built against the exact Godot API),
# hidapi (source), static libopus, gcc (Linux) and mingw-w64 posix (Windows).
# Every download is pinned by SHA-256. Used by ../build.sh.

ARG DEBIAN_IMAGE=debian:trixie-slim@sha256:a99cfc517144bc59b1978475ec53b46ecabec7e43635402ee5b77cc54cd1b20a

FROM ${DEBIAN_IMAGE} AS fetch
ARG GODOT_URL=https://github.com/godotengine/godot/releases/download/4.7.2-stable/Godot_v4.7.2-stable_linux.x86_64.zip
ARG GODOT_SHA256=cadd3204e728a35d3f13adb7fd0d7902636b79f6b95c40c265eb73b6c35329e4
ARG GODOT_CPP_URL=https://github.com/godotengine/godot-cpp/archive/refs/tags/10.0.0-stable.tar.gz
ARG GODOT_CPP_SHA256=ebbf64f0f8f8b8b66bc4bc4d3797db97f95b19cf0dfb5b3f8cf56683d9df7db4
ARG HIDAPI_URL=https://github.com/libusb/hidapi/archive/refs/tags/hidapi-0.15.0.tar.gz
ARG HIDAPI_SHA256=5d84dec684c27b97b921d2f3b73218cb773cf4ea915caee317ac8fc73cef8136
ARG OPUS_URL=https://downloads.xiph.org/releases/opus/opus-1.6.1.tar.gz
ARG OPUS_SHA256=6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates curl unzip && apt-get clean
WORKDIR /fetch
RUN curl -fsSL -o godot.zip "$GODOT_URL" && echo "$GODOT_SHA256  godot.zip" | sha256sum -c - \
 && unzip -q godot.zip && mkdir -p /out && mv Godot_v*_linux.x86_64 /out/godot \
 && curl -fsSL -o gcpp.tgz "$GODOT_CPP_URL" && echo "$GODOT_CPP_SHA256  gcpp.tgz" | sha256sum -c - \
 && mkdir /out/godot-cpp && tar -xzf gcpp.tgz -C /out/godot-cpp --strip-components=1 \
 && curl -fsSL -o hid.tgz "$HIDAPI_URL" && echo "$HIDAPI_SHA256  hid.tgz" | sha256sum -c - \
 && mkdir /out/hidapi && tar -xzf hid.tgz -C /out/hidapi --strip-components=1 \
 && curl -fsSL -o opus.tgz "$OPUS_URL" && echo "$OPUS_SHA256  opus.tgz" | sha256sum -c - \
 && mkdir /out/opus-src && tar -xzf opus.tgz -C /out/opus-src --strip-components=1

FROM ${DEBIAN_IMAGE} AS toolchain
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates fontconfig build-essential pkg-config libudev-dev scons python3 \
      gcc-mingw-w64-x86-64-posix g++-mingw-w64-x86-64-posix \
 && apt-get clean
COPY --from=fetch /out/godot /usr/local/bin/godot
COPY --from=fetch /out/godot-cpp /opt/godot-cpp
COPY --from=fetch /out/hidapi /opt/hidapi
COPY --from=fetch /out/opus-src /opt/opus-src
WORKDIR /opt/godot-cpp
# Exact API of the pinned Godot, then godot-cpp static libraries (release) for both platforms.
RUN HOME=/tmp godot --headless --dump-extension-api && test -s extension_api.json \
 && scons platform=linux arch=x86_64 target=template_release custom_api_file=extension_api.json -j2 \
 && scons platform=windows use_mingw=yes arch=x86_64 target=template_release custom_api_file=extension_api.json -j2
# Static libopus: PIC for the Linux .so, mingw-w64 for the Windows DLL.
RUN for target in linux windows; do \
      mkdir -p /tmp/opus-$target && cd /tmp/opus-$target \
      && if [ "$target" = windows ]; then host="--host=x86_64-w64-mingw32"; else host=""; fi \
      && /opt/opus-src/configure $host --prefix=/opt/opus/$target --disable-shared --enable-static \
           --with-pic --disable-doc --disable-extra-programs CFLAGS="-O2" >/dev/null \
      && make -j2 >/dev/null && make install >/dev/null || exit 1; \
    done
WORKDIR /work
