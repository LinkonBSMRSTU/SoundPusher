# Sound Pusher — real-time encoded audio output for OS X
Provides a virtual 5.1-channel audio output device whose contents are real-time encoded to `AC3` and sent as digital `SPDIF` stream to a real audio device output.

**WARNING:** Please turn down the speaker / headphone volume before running this application, as any component misinterpreting a compressed audio stream as "normal" audio signals may result in unexpectedly loud noise.

## Usage
There are two component required to make this work:
1. `LoopbackAudio` — A user-space CoreAudio driver (aka `AudioServerPlugin`) that provides a 5.1 output stream whose contents are mirrored ("looped back") to its own input stream. This allows applications to process the audio output of the system (if it is sent to the `LoopbackAudio` device). `LoopbackAudio.driver` must be installed (copied) into the `/Library/Audio/Plug-Ins/HAL` directory.
2. `SoundPusher` — An application that continuously reads audio from the `LoopbackAudio` device, encodes in into a compressed format, and then sends that compressed stream to a (hopefully) real sound device's digital output stream. This is controlled through its menu bar extra.

## Dependencies
### FFmpeg — libavcodec, libavformat
Here's a script that builds a cut-down version of [FFmpeg](http://www.ffmpeg.org) that includes all that's required for SoundPusher:
```sh
#!/bin/sh
./configure --prefix=$FFMPEG_HOME --cc=clang --disable-static --enable-shared --extra-ldlibflags=-Wl,-unexported_symbol,"_ff_*" --disable-all --disable-doc --disable-everything --disable-pthreads --disable-iconv --disable-securetransport --enable-avutil --enable-avcodec --enable-avformat --enable-encoder=ac3 --enable-muxer=spdif
make -j8
make install
# update shared library install names
cd $FFMPEG_HOME/lib
AVUTIL_ID=`otool -XD libavutil.dylib`
AVCODEC_ID=`otool -XD libavcodec.dylib`
AVFORMAT_ID=`otool -XD libavformat.dylib`
for lib in libavutil libavcodec libavformat
do
    install_name_tool -id @rpath/$lib.dylib -change $AVUTIL_ID @rpath/libavutil.dylib -change $AVCODEC_ID @rpath/libavcodec.dylib -change $AVFORMAT_ID @rpath/libavformat.dylib $lib.dylib
done
```

## Acknowledgements
This software is built on the experience of others:
- `LoopbackAudio` is based on the `NullAudio` user-space driver provided by Apple as example code.
- [TPCircularBuffer](https://github.com/michaeltyson/TPCircularBuffer/) by Michael Tyson is used to move audio and packet data around.
- [FFmpeg](http://www.ffmpeg.org) is used to encode to compressed formats and then mux those packets into an `SPDIF` compliant bit-stream.
