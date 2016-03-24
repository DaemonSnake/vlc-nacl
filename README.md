# (P)NaCl

This project provides additional modules needed to use VLC from within a NaCl or
PNaCl module. With this project, built for PNaCl, VLC is able to play media within
abritrary websites (ie doesn't require use of a Chrome Store app).

## The Build

Building on Windows will likely be impossible without use of MSYS. The author
uses Linux, and while an effort by the author was taken to ensure the most
platform independent instructions, the reader's mileage my vary if their
platform is not Linux. Fortunately, Linux is free and so is VirtualBox (and
PNaCl is platform/arch independent by design).

### Prerequisites

Before we can get started, we need to procure a few things from the Internet:

 * From the NaCl SDK, `pepper_43` or newer.
 * [webports](https://chromium.googlesource.com/webports/)

### Build!

    $ ./configure && ./compile --arch $ARCH --pepper-root $NACL_SDK_ROOT --webports-root $WEBPORTS_ROOT

Where `$ARCH` is one of `le32`, `i686`, `x86_64`, or `arm`, `$NACL_SDK_ROOT`
points to your Pepper SDK root, and `$WEBPORTS_ROOT` points to the root of your
`webports` checkout. `compile` will fetch and build the needed `webports`
packages for you.

# Embedding VLC

    <script async>
        var getVlc = null;
	    var restartVlc = null;
        (function() {
            "use strict";

            var embed = null;
	        var parent = document.getElementById('video');

	        restartVlc = function() {
                if(embed != null) {
	                parent.removeChild(embed);
	            }

                embed = document.createElement('embed');
                embed.setAttribute('id', 'vlc');
	            embed.setAttribute('src', 'vlc-debug.nmf');
	            embed.setAttribute('type', 'application/x-nacl');
                // Comment out the following two lines if debugging VLC
                embed.setAttribute('src', 'vlc-release.nmf');
                embed.setAttribute('type', 'application/x-pnacl');
                embed.setAttribute('width', '100%');
                embed.setAttribute('height', '100%');

                embed.addEventListener('load', function() {
	                var instance = new VlcInstance(embed);
                    getVlc = function() {
	                    return instance;
	                };
                });
                parent.appendChild(embed);
	       };
	       restartVlc();
       })();
    </script>

Place the above after the #video element. It will create a single VLC
instance. Call `restartVlc()` to restart the instance. `getVlc()` will return
the instance object. Info on the API present is documented below.

Additionally, `extra/ppapi-control.js` should be included in the `<head>` of
the HTML document. `ppapi-control.js` is internally versioned (ie all messages
sent to VLC specify which API version to use), so it is safe to copy where ever
needed.

Consult the [nmf documentation][] for info on how to create `vlc-debug.nmf` and
`vlc-release.nmf`.

[nmf documentation]: https://developer.chrome.com/native-client/reference/nacl-manifest-format


# Using the Javascript API

Using `getVlc()` from above (all of these are static, in the C++ class sense):

 * `getVlc().input` -- Stuff apropos to the current input (as defined in
 `libvlccore`).
   - `getVlc().input.state` -- Access to the backend `input_thread_t`
     state. Mostly for debugging; use `getVlc().playlist.state` instead.
   - `getVlc().input.rate` -- Get or set the current playback rate. Must be
   positive.
   - `getVlc().input.item` -- Get the currently playing item or `undefined` if
   nothing is playing.
   - `getVlc().input.position` -- Get or set the current position of the
   media. Range: [0.0f, 1.0f]. Float.
   - `getVlc().input.time` -- Get or set the current time index of the
   media. Array [seconds, nanoseconds].
   - `getVlc().input.length` -- Get the length of the currently playing
     item. Array [seconds, nanoseconds].
   - `getVlc().input.video` -- Stuff relevant to videos only.
     * `getVlc().input.video.nextFrame()` -- Pause media and display next
     frame.
     * `getVlc().input.video.prevFrame()` -- Pause media and display previous
     frame. MASSIVELY slow. Won't function correctly without precise indexes.
 * `getVlc().playlist` -- Stuff apropos to the playlist.
   - `getVlc().playlist.audio` -- Stuff relevant to videos only (the volume
     level is manage via playlist interfaces, so the author put it here).
     * `getVlc().playlist.audio.muted` -- Get or set the mute toggle. Boolean.
     * `getVlc().playlist.audio.volume` -- Get or set the audio volume. Values >
     1.0f will result in amplification. Float.
   - `getVlc().playlist.clear()` -- Clear all items from the playlist.
   - `getVlc().playlist.enqueue()` -- Add item(s) to the playlist. Accepts a
   single URL or an array of URLs. URLs must be absolute and prefixed with
   `http://`.
   - `getVlc().playlist.dequeue()` -- Remove item(s) from the playlist. Accepts
   a single `playlist_item_id` or an array of them. `playlist_item_id` can be
   found in an element of `getVlc().playlist.items`.
   - `getVlc().playlist.items` -- Gets the list of items in the playlist.
   - `getVlc().playlist.next()` -- Stop playing the current item and set the
   current item to the next in the playlist.
   - `getVlc().playlist.prev()` -- Stop playing the current item and set the
   current item to the prev in the playlist.
   - `getVlc().playlist.play()` -- Start playlist playback.
   - `getVlc().playlist.pause()` -- Pause playback.
   - `getVlc().playlist.stop()` -- Stop playlist playback. Does not reset the
   playlist to the beginning.
   - `getVlc().playlist.status` -- Get the current playback status. Will be one
   of `getVlc().playlist.STOPPED_STATUS`, `getVlc().playlist.RUNNING_STATUS`, or
   `getVlc().playlist.PAUSED_STATUS`. Convenience functions are
   `getVlc().playlist.isStopped()`, `getVlc().playlist.isRunning()`, and
   `getVlc().playlist.isPaused()`, respectively.
   - `getVlc().playlist.repeating` -- If true, repeat the current item.
   - `getVlc().playlist.looping` -- If true, repeat the whole playlist.
 * `getVlc().sys` -- Stuff related to VLC under the hood.
   - `getVlc().sys.log_level` -- Get or set log filtering level. Range: [0, 4].
   - `getVlc().sys.version` -- Get an object with various details about version
   of the VLC instance.
   - `getVlc().sys.purge_cache()` -- Purge `ppapi-access`' media cache. All or
     nothing.

Note: `getVlc().this_is_a_method()` && `getVlc().this_is_a_property`. All
methods accept a callback parameter as the last argument. The callback will be
called asynchronously with the results. Status codes mimic HTTP status codes, ie
a `return_code` with a value of `200` indicates success. If no callback is
provided, any and all errors will be ignored with a warning printed to the
devtools console.


There is a limited selection of event one can listen for on the `playlist` and
`input` objects. Add a callback listener by calling `addEventListener(loc, callback)` and
remove it with `removeEventCallback(loc, callback)` on the object.

`playlist` events (new values are in the `new_value` key, old in `old_value`):

 * `input-current`: signals when the playlist input thread object (and thus the
  item) has changed. If the value is 0, the input was stopped and removed.

`input` events (new values are in the `value` key):

 * `state` -- integer.
 * `dead` -- no value.
 * `rate` -- See `getVlc().input.rate`.
 * `position` -- See `getVlc().input.position`.
 * `length` -- See `getVlc().input.length`.
 * `chapter` -- no value.
 * `program` -- no value.
 * `es` -- no value.
 * `teletext` -- no value.
 * `record` -- no value.
 * `item-meta` -- See `getVlc().input.item`.
 * `item-info` -- no value.
 * `item-name` -- no value.
 * `item-epg` -- no value.
 * `statistics` -- no value.
 * `signal` -- no value.
 * `audio-delay` -- no value.
 * `subtitle-delay` -- no value.
 * `bookmark` -- no value.
 * `cache` -- Double, % buffered.
 * `aout` -- no value.
 * `vout` -- no value.

Example: `getVlc().input.addEventListener('position', function(event) { console.log(event); }`

This JS API is fully versioned, so the included `ppapi-control.js` can be copied
into your projects source control and updated only when desired.

Take a look at `ppapi-control.js` in `extras/ppapi-control.js` for more info.

## Issues

 * XXX: There are no tests!
 * The author hasn't extensively tested `ppapi-access.cpp` on slow connections (ie
   not localhost).
 * NaCl's service runtime/IRT doesn't support clock selection (though getting
   the monotonic time is still possible). Currently, vlc translates absolute
   monotonic time into absolute realtime time when waiting on condition
   vars. This is clearly bad if the system clock time travels.
 * Something takes the address of `memcpy` (which is technically undefined
   behaviour). However, Clang doesn't error on this, and instead inserts a
   wrapper named exactly `memcpy`. LLVM then overwrites the real version of
   memcpy (ie what calls to @llvm.memcpy turn into) with the wrapper that Clang
   inserted, resulting in a infinite loop/stack exhaustion.
 * Although code to manage multiple simultaneous instances is present, the
   author has not tested any of it in any capacity.
 * Use `PPB_VideoDecoder` to decode videos. This will delegate decoding to
   hardware, and should greatly reduce the CPU usage of many videos.
