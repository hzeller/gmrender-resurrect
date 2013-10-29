Headless UPnP Renderer
----------------------

I needed a small headless UPnP media renderer for Linux (for small footprint-use
in a Raspberry Pi or CuBox), but there was not much available.

Found this old project [GMediaRender][orig-project] - but it
was incomplete, several basic features missing and the project has been
abandoned several years ago.

So this is a fork of those sources to resurrect this renderer and add the
missing features to be useful (Original sources in [savannah cvs][orig-cvs]).
To distinguish this project from the original one, this is called
**[gmrender-resurrect](http://github.com/hzeller/gmrender-resurrect)**.

After many features added, this is now a very usable, headless UPnP
renderer that I would consider **stable**.
If you have tested gmrender resurrect with your control point, **please add
it to the [Compabibility Wiki][compat-wiki] page** (even if there is a problem).
At this point, it should work with all media controllers, if not,
please file a bug so that we can figure out the issue and I can make it work
for you. See [INSTALL.md](./INSTALL.md) how to create a logfile that helps
to narrow down things.

If you're running this on a Raspberry Pi, you might be interested to connect
a little LCD display. Check out the **[upnp-display][]** github project.

Issues with old versions (≤ 0.10) of gstreamer
-----------------------------------------------

Some troubles came up with the underlying gstreamer that manifests in
'not responding' after a couple of hours or days of use; this is with
gstreamer 0.10 up to 1.0.7. There was a [leaking thread bug][gst-bug-699794]
whose fix showed up in GStreamer 1.0.8.
If you are using an older version, then only restart of gmrender-resurrect
helps in these situations.
There is a [bug][gst-bug-698750] before gstreamer 1.2 that affects gapless
playback.

Bottomline: use at least gstreamer 1.2.

If you want to use OpenMAX support, [there are reports][open-max-support], that
this works with a recent version of gstreamer >= 1.0.9.

Installation
------------
For installation instructions, see [INSTALL.md](./INSTALL.md)

You can reach me via <h.zeller@acm.org>.


[orig-project]: http://gmrender.nongnu.org/
[orig-cvs]:http://cvs.savannah.gnu.org/viewvc/gmrender/?root=gmrender
[gst-bug-698750]: https://bugzilla.gnome.org/show_bug.cgi?id=698750
[gst-bug-699794]: https://bugzilla.gnome.org/show_bug.cgi?id=699794
[compat-wiki]: https://github.com/hzeller/gmrender-resurrect/wiki/Comptibility
[upnp-display]: https://github.com/hzeller/upnp-display
[open-max-support]: https://github.com/hzeller/gmrender-resurrect/issues/33#issuecomment-23859699
