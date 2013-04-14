# Installing

On a typical Ubuntu or Debian system, you need tools to be able to bootstrap the
compilation configuration:

    sudo apt-get install autoconf automake libtool

.. and the libraries needed for gmrender:

    sudo apt-get install libupnp-dev libxml2-dev libgstreamer0.10-dev \
                gstreamer0.10-plugins-base gstreamer0.10-plugins-good \
                gstreamer0.10-plugins-bad gstreamer0.10-plugins-ugly \
                gstreamer0.10-ffmpeg gstreamer0.10-pulseaudio 


Get the source. If this is your first time using git, you first need to install
it:

    sudo apt-get install git

.. Then check out the source:

    git clone https://github.com/hzeller/gmrender-resurrect.git

Then configure and build

    cd gmrender-resurrect
    ./autogen.sh
    ./configure
    make

You then can run gmrender directly from here if you want. The `-f` option
provides the name under which the UPnP renderer advertises:

    ./src/gmediarender -f "My Renderer"

.. to install, run

    sudo make install

The final binary is in `/usr/local/bin/gmediarender` (unless you changed the
PREFIX in the configure step).

# Raspberry Pi
If you're installing gmrender-resurrect on the Raspberry Pi, there have
been reports of bad sound quality. For one, the 3.5mm output is very low
quality, so don't expect wonders.
But apparently it is also important to have pulseaudio running. Stephen Phillips
wrote a nice comprehensive blog-post about installing gmrender-resurrect on the
Raspberry Pi:

http://blog.scphillips.com/2013/01/using-a-raspberry-pi-with-android-phones-for-media-streaming/

# Running
If you write an init script for your gmediarender, then the following options
are particularly useful:

## -f, --friendly-name
Friendly name to advertise. Usually, you want your renderer show up in your
controller under a nice name. This is the option to set that name.

## -u, --uuid
UUID to advertise. Usually, gmediarender comes with a built-in static id, that
is advertised and used by controllers to distinguish different renderers.
If you have multiple renderers running in your network, they will all share the
same static ID.
With this option, you can give each renderer its own id.
Best way is to create a UUID once by running the `uuidgen` tool:

    $ sudo apt-get install uuid-runtime
    $ uuidgen
    a07e8dfe-26a4-11e2-9dd1-5404a632c90e

You take different generated numbers and hard-code it in each script
starting an instance of gmediarender.

Also, you can do this already at compile time, when running configure

    ./configure CPPFLAGS="-DGMRENDER_UUID='\"`uuidgen`\"'"

## --gstout-audiosink and --gstout-audiodevice
You can set the audio sink and audio device with these commandline
options. Say you have an alsa device `iec958` you want to use, then you can
add these options:

    gmediarenderer --gstout-audiosink=alsasink --gstout-audiodevice=iec958

The options are described via

    gmediarender --help-gstout

There are other ways to define the default gstreamer output devices via
some global system settings, but in particular if you are on some embedded
device, setting these directly via a commandline option is the very best.

In this example, for the alsa sink to work, you need the package:

    sudo apt-get install gstreamer0.10-alsa

## Running as daemon

If you want to run gmediarender as daemon, the follwing two options are for
you:

    -d, --daemon                      Run as daemon.
    -P, --pid-file                    File the process ID should be written to.


# GStreamer 1.0
gmrender-resurrect is prepared to compile with gstreamer 1.0, already available
on newer distributions. This is the preferred version as the older 0.10 version
is not supported anymore. Instead of the 0.10 versions above, just install
these:

    sudo aptitude install libgstreamer1.0-dev \
             gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
             gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly \
             gstreamer1.0-libav \
             gstreamer1.0-pulseaudio gstreamer1.0-alsa

(pulseaudio or alsa depending on what output you prefer)
