# Bundled replacement media

`resources.json` contains independently generated Yee replacement resources.
`yee-blank.mp4` is a one-second, silent 32×32 black VP9 video in an MP4 container.
The container and `video/mp4` MIME match its existing redirect aliases. VP9 is
supported by the current Chromium codec configuration.

The Rust resource adapter also uses this payload for the community resource
`noop-1s.mp4`, retaining its canonical name and aliases. Its original H.264 file
remains unchanged in the vendored source archive. Other community redirect
resources retain their original payloads and alias precedence.

The payload was generated with FFmpeg's `libvpx-vp9` encoder:

```sh
ffmpeg -f lavfi -i color=c=black:s=32x32:r=1:d=1 -an \
  -c:v libvpx-vp9 -deadline realtime -cpu-used 8 -crf 63 -b:v 0 \
  -pix_fmt yuv420p -fflags +bitexact -flags:v +bitexact \
  -movflags +faststart blank.mp4
```

Base64-encode the output into that entry's `content`. Preserve its name, MIME
and aliases. Verify both decoding and native redirect delivery in Yee after
rebuilding; metadata loading alone does not prove frame decoding.
