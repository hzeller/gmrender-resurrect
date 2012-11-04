On a typical ubuntu system, you need tools to be able to bootstrap the
compilation configuration:

    sudo aptitude install autoconf automake libtool

.. and the libraries needed for gmrender

    sudo apt-get install libupnp-dev libgstreamer0.10-dev \
                gstreamer0.10-plugins-base gstreamer0.10-plugins-good \
                gstreamer0.10-plugins-bad gstreamer0.10-plugins-ugly


Then configure and build

    cd gmrender-resurrect
    ./autogen.sh
    ./configure
    make

You then can run gmrender directly from the sources. The `-f` option
provides the name under which the UPnP renderer advertises:

    ./src/gmediarender -f "My Renderer"

.. then install it:

    sudo make install

The final binary is in `/usr/local/bin/gmediarender` (unless you changed the
PREFIX in the configure step).
