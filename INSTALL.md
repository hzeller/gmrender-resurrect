# Installing

On a typical Ubuntu or Debian system, you need tools to be able to bootstrap the
compilation configuration:

    sudo apt-get install autoconf automake libtool pkg-config

.. and the libraries needed for gmrender, most notably gstreamer.
On current Linux distributions, these are typically version 1.0 of gstreamer,
older might have version 0.10 - gmrender-resurrect compiles with both.

```
sudo apt-get update
sudo aptitude install libupnp-dev libgstreamer1.0-dev \
             gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
             gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly \
             gstreamer1.0-libav
```

Then pulseaudio or alsa depending on what output you prefer (personally, I use
alsa)

    sudo aptitude install gstreamer1.0-alsa
    sudo aptitude install gstreamer1.0-pulseaudio


(See below for the Gstreamer 0.10 installation; essentially only the version
number is different, gmrender compiles with both versions).

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

# Running

## Init Script

There is a sample init script in `scripts/init.d/gmediarenderer` that could be
a good start if you install things on your system.

TBD: add systemd config.

(To Linux distribution packagers: please let me know if you have some
common changes that might be useful to have in upstream; other than that, just
do what makes most sense in your distribution)

## Commandline Options

If you write your own init script for your gmediarender, then the following
options are particularly useful.

### -f, --friendly-name
Friendly name to advertise. Usually, you want your renderer show up in your
controller under a nice name. This is the option to set that name.

### -u, --uuid
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
starting an instance of gmediarender (In my init script, I just generate
some stable value based on the ethernet MAC address; see "Init Script" below).

Also, you can do this already at compile time, when running configure

    ./configure CPPFLAGS="-DGMRENDER_UUID='\"`uuidgen`\"'"

### --gstout-audiosink and --gstout-audiodevice
You can set the audio sink and audio device with these commandline
options.
Say, you want to use an ALSA device. You can see the available devices
with `aplay -L`. The main ALSA device is typically called `sysdefault`,
so this is how you select it on the command line:

    gmediarenderer --gstout-audiosink=alsasink --gstout-audiodevice=sysdefault

The options are described with

    gmediarender --help-gstout

There are other ways to configure the default gstreamer output devices via
some global system settings, but in particular if you are on some embedded
device, setting these directly via a commandline option is the very best.

### --gstout-initial-volume-db
This sets the initial volume on startup in decibel. The level 0.0 decibel
is 'full volume', -20db would show on the UPnP controller as '50%'. In the
following table you see the non-linear relationship:

     [decibel]  [level shown in player]
           0db    100     # this is the default if option not set
          -6db    85
         -10db    75
         -20db    50
         -40db    25
         -60db    0

So with --gstout-initial-volume-db=-10 the player would show up as being set
to #75.

Note, as always with the volume level in this renderer, this does not
influence the hardware level (e.g. Alsa), but only the internal attenuation.
So it is advised to always set the hardware output to 100% by system means.

### Running as daemon

If you want to run gmediarender as daemon, the follwing two options are for
you:

    -d, --daemon                      Run as daemon.
    -P, --pid-file                    File the process ID should be written to.


### Misc options

    --logfile <logfile>               Write a logfile.
        If you want this on the terminal use --logfile /dev/stdout
        This can be big over time, so only do it for debugging.

In particular when you file a bug, please always attach the output of such
a logfile; start gmrender-resurrect in foreground mode (without `-d`) on the
commandline and give it a file to log into. Attach that to your bug-report.

The following command makes sure to capture all logs from gmediarender and other
log entries that might come from gstreamer plugins not using the gmediarender
logging, all in one file:

    src/gmediarender -f "MyRender" --logfile=/tmp/gmrender.log >> /tmp/gmrender.log 2>&1


# GStreamer 0.10

If your distribution does not have gstreamer 1.0 yet, you can also compile it with
the old gstreamer.
```
sudo apt-get install libupnp-dev libgstreamer0.10-dev \
                gstreamer0.10-plugins-base gstreamer0.10-plugins-good \
                gstreamer0.10-plugins-bad gstreamer0.10-plugins-ugly \
                gstreamer0.10-ffmpeg \
                gstreamer0.10-pulseaudio gstreamer0.10-alsa
```

# Other installation resources
## Raspberry Pi
If you're installing gmrender-resurrect on the Raspberry Pi, there have
been reports of bad sound quality. For one, the 3.5mm output is very low
quality, so don't expect wonders (though it seems that driver changes improved
this quality a lot).

You can use gmrender-resurrect with Pulseaudio or ALSA (or whatever other output
ways gstreamer supports). Personally, I use ALSA as it is the most simple and
robust way (Pulseaudio would be a layer on top of ALSA anyway).
See flag --gstout-audiosink above how to tell gmediarender to use alsasink.

By default, ALSA seems to attempt some re-sampling apparently; A user
pointed out that this can be fixed by putting this in your `/etc/asound.conf`

    ctl.!default {
      type hw
      card 0
    }

Stephen Phillips wrote a comprehensive blog-post about installing
gmrender-resurrect on the Raspberry Pi (July 2013):

http://blog.scphillips.com/2013/07/playing-music-on-a-raspberry-pi-using-upnp-and-dlna-revisited/

## Arch Linux
There is an Arch package available here
 https://aur.archlinux.org/packages/gmrender-resurrect-git/
