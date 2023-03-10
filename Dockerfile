FROM debian:stable-slim as build

RUN apt-get update && \
    apt-get install -y build-essential autoconf automake libtool pkg-config libupnp-dev libgstreamer1.0-dev uuid-runtime

WORKDIR /opt/gmrender-resurrect
COPY . .

RUN ./autogen.sh && ./configure CPPFLAGS="-DGMRENDER_UUID='\"`uuidgen`\"'" && make && make install

FROM debian:stable-slim as run

COPY --from=build /usr/local/bin/gmediarender /usr/local/bin/gmediarender
COPY --from=build /usr/local/share/gmediarender/grender-64x64.png /usr/local/share/gmediarender/grender-64x64.png
COPY --from=build /usr/local/share/gmediarender/grender-128x128.png /usr/local/share/gmediarender/grender-128x128.png

RUN apt-get update && \
    apt-get install -y pulseaudio libupnp-dev libgstreamer1.0-dev gstreamer1.0-libav gstreamer1.0-pulseaudio \
        gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly

EXPOSE 49494

ENTRYPOINT gmediarender --logfile=stdout -f $FRIENDLY_NAME