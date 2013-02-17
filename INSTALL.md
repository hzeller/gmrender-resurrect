# Installing

On a typical Ubuntu or Debian system, you need tools to be able to bootstrap the
compilation configuration:

    sudo aptitude install autoconf automake libtool

.. and the libraries needed for gmrender:

    sudo apt-get install libupnp-dev libgstreamer0.10-dev \
                gstreamer0.10-plugins-base gstreamer0.10-plugins-good \
                gstreamer0.10-plugins-bad gstreamer0.10-plugins-ugly \
                gstreamer0.10-pulseaudio gstreamer0.10-ffmpeg


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
But also it is important to have pulseaudio running. Stephen Phillips made
a nice comprehensive blog-post about installing gmrender-resurrect on the
Rapsberry Pi:

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
Best way is to create a UUID once by running the `uuid` tool:

    $ uuid
    a07e8dfe-26a4-11e2-9dd1-5404a632c90e

You take different generated numbers and hard-code it in each script
starting an instance of gmediarender.

## Running as daemon.

If you want to run gmediarender as daemon, the follwing two options are for
you:

    -d, --daemon                      Run as daemon.
    -P, --pid-file                    File the process ID should be written to.

