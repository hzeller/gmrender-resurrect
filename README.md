I needed a small headless UPnP media renderer for Linux but there does 
not seem to be much around.

Found: GMediaRender http://gmrender.nongnu.org/
but it is incomplete, several basic features missing. This is a fork
of these sources found at
http://cvs.savannah.gnu.org/viewvc/gmrender/?root=gmrender

Added so far
  * Support to get duration and position of current stream. This allows
    controllers to show a progress bar.
  * Support basic commands:
     - Pause  : Pause current stream.
     - Seek   : Seek to a particular position.
  * When current track is finisehd, transition to state `STOPPED`
    so that controllers will send us the next song.
  * Initial code to support SetNextAVTransportURI, but still have to find
    a controller that supports it :)

