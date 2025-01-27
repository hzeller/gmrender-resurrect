ARG AUDIO_BACKEND="alsa"

# Build image
FROM alpine:latest AS build
ARG UUID
RUN apk add --update build-base autoconf automake libtool pkgconfig gstreamer-dev libupnp-dev uuidgen
WORKDIR /opt/gmrender-resurrect
COPY . .
RUN ./autogen.sh && ./configure CPPFLAGS="-DGMRENDER_UUID='\"${UUID:-`uuidgen`}\"'"
RUN make && make install DESTDIR=/gmrender-install

# ALSA image
FROM alpine:latest AS alpine-alsa
RUN apk add --update alsa-lib alsa-utils

# PulsaAudio image
FROM alpine:latest AS alpine-pulse
RUN apk add --update pulseaudio

# Run image
FROM alpine-${AUDIO_BACKEND}
COPY --from=build /gmrender-install /
RUN apk add --update tini libupnp gstreamer gstreamer-tools gst-plugins-base gst-plugins-good gst-plugins-bad gst-plugins-ugly
ENV FRIENDLY_NAME=
ENV UUID=
ENV OPTIONS=
EXPOSE 49494
ENTRYPOINT ["/sbin/tini", "--"]
CMD ["/bin/sh", "-c", "/usr/local/bin/gmediarender --logfile=stdout ${FRIENDLY_NAME:+-f \"$FRIENDLY_NAME\"} ${UUID:+--uuid \"$UUID\"} $OPTIONS"]