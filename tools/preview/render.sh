#!/bin/sh
# Usage: tools/preview/render.sh out.(gif|mp4|png) [days] [seconds] [hour] [seed] [shakeAt]
# Builds the host preview of src/scene.cpp and renders it with ffmpeg.
set -e
cd "$(dirname "$0")/../.."
out=$1; shift
c++ -std=c++17 -O2 -Itools/preview -Isim -Isrc -o tools/preview/preview tools/preview/preview.cpp src/scene.cpp sim/terrarium.cpp
case $out in
  *.png) tools/preview/preview "${1:-30}" 0.05 "${3:-12}" "${4:-7}" | ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgb24 -s 960x540 -i - -frames:v 1 "$out" ;;
  *.gif) tools/preview/preview "$@" | ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgb24 -s 960x540 -r 20 -i - \
           -vf "scale=480:270:flags=neighbor,split[a][b];[a]palettegen=max_colors=128[p];[b][p]paletteuse=dither=none" "$out" ;;
  *) tools/preview/preview "$@" | ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgb24 -s 960x540 -r 20 -i - -pix_fmt yuv420p "$out" ;;
esac
