"""Makes the guide's movie from numbers: run it from the mod's folder.

python source/make_trailer.py

It writes layers/base/assets/movies/Trailer.webm (AV1, 640x360, 30 frames a second, 8 s: drifting colour rings drawn by
ffmpeg's geq filter) and Trailer.ogg (Ogg Vorbis, 48 kHz: a slow two-note pad). Needs ffmpeg with libaom-av1 and
libvorbis, on the search path or named by FFMPEG.
"""
import os
import subprocess

OUT = os.path.join("layers", "base", "assets", "movies")


def main():
    ffmpeg = os.environ.get("FFMPEG", "ffmpeg")
    os.makedirs(OUT, exist_ok=True)
    video = os.path.join(OUT, "Trailer.webm")
    audio = os.path.join(OUT, "Trailer.ogg")
    rings = ("geq=lum='128+100*sin(hypot(X-W/2-80*sin(T),Y-H/2-50*cos(T*0.7))/9-T*4)':"
             "cb='128+90*sin(T*0.9+X/70)':cr='128+90*cos(T*0.6+Y/60)'")
    subprocess.run([ffmpeg, "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                    "color=c=black:s=640x360:r=30:d=8,format=yuv420p," + rings,
                    "-c:v", "libaom-av1", "-cpu-used", "8", "-crf", "34", "-b:v", "0", "-g", "60", "-pix_fmt", "yuv420p",
                    "-color_range", "tv", "-colorspace", "bt709", "-color_primaries", "bt709", "-color_trc", "bt709",
                    "-an", video], check=True)
    subprocess.run([ffmpeg, "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                    "aevalsrc='0.2*sin(2*PI*220*t)*(0.6+0.4*sin(2*PI*0.25*t))+0.15*sin(2*PI*330*t)*(0.6+0.4*cos(2*PI*0.25*t))':s=48000:d=8",
                    "-c:a", "libvorbis", "-q:a", "3", audio], check=True)
    for path in (video, audio):
        print("%s: %d bytes" % (path.replace(os.sep, "/"), os.path.getsize(path)))


if __name__ == "__main__":
    main()
