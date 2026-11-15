#!/bin/bash
# The assembly frames of tools/render_enclosure.py (RENDER_MODE=anim) as:
#   OUT-assembly.gif   the animation, 720 px wide
#   OUT-assembly.mp4   the animation, H.264
#   OUT-teaser.mp4     the animation fading to black in the last second, then
#                      a black end card (TITLE, big and bold, in the centre,
#                      SUBTITLE at the bottom) fading out in CARD seconds;
#                      with MUSIC, the track fades in over 1.5 s and fades
#                      out with the picture
#
# Usage: tools/make_video.sh FRAMES_DIR OUT [MUSIC]
# Environment: FPS (24), TITLE ("COMING SOON"), SUBTITLE ("Available Q1 2027"),
#              CARD (5 s), FONT (DejaVu Sans Bold)
set -e -o pipefail
FRAMES=${1:?usage: $0 FRAMES_DIR OUT [MUSIC]}
OUT=${2:?usage: $0 FRAMES_DIR OUT [MUSIC]}
MUSIC=$3
FPS=${FPS:-24}
TITLE=${TITLE:-COMING SOON}
SUBTITLE=${SUBTITLE:-Available Q1 2027}
CARD=${CARD:-5}
FONT=${FONT:-/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf}
[ -f "${FONT}" ] || { echo "font ${FONT} missing (apt install fonts-dejavu-core)"; exit 1; }

N=$(ls "${FRAMES}"/f*.png | wc -l)
[ "${N}" -gt 0 ] || { echo "no frames in ${FRAMES}"; exit 1; }
SIZE=$(ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=s=x:p=0 "${FRAMES}/f0001.png")
D=$(awk "BEGIN{printf \"%.3f\", ${N}/${FPS}}")              # animation length (s)
FADE_AT=$(awk "BEGIN{printf \"%.3f\", ${D}-1}")
TOTAL=$(awk "BEGIN{printf \"%.3f\", ${D}+${CARD}}")
echo "${N} frames, ${SIZE}, ${D} s at ${FPS} fps"
mkdir -p "$(dirname "${OUT}")"

ffmpeg -v error -y -framerate "${FPS}" -i "${FRAMES}/f%04d.png" \
	-c:v libx264 -preset slow -crf 18 -pix_fmt yuv420p -movflags +faststart "${OUT}-assembly.mp4"
ffmpeg -v error -y -framerate "${FPS}" -i "${FRAMES}/f%04d.png" \
	-vf "fps=15,scale=720:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=160[p];[b][p]paletteuse=dither=bayer:bayer_scale=3" \
	-loop 0 "${OUT}-assembly.gif"

# the end card: both lines fade out together over CARD seconds
esc() { printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e "s/'/\\\\'/g" -e 's/:/\\:/g' -e 's/%/\\%/g'; }
TXT="drawtext=fontfile=${FONT}:text='$(esc "${TITLE}")':fontcolor=white:fontsize=h/6.5:x=(w-text_w)/2:y=(h-text_h)/2:alpha='1-t/${CARD}'"
SUB="drawtext=fontfile=${FONT}:text='$(esc "${SUBTITLE}")':fontcolor=white:fontsize=h/20:x=(w-text_w)/2:y=h-h/9-text_h:alpha='1-t/${CARD}'"
VIDEO="[0:v]fade=t=out:st=${FADE_AT}:d=1,format=yuv420p,setsar=1[a];[1:v]${TXT},${SUB},format=yuv420p,setsar=1[c];[a][c]concat=n=2:v=1:a=0[v]"
if [ -n "${MUSIC}" ]; then
	ffmpeg -v error -y -framerate "${FPS}" -i "${FRAMES}/f%04d.png" \
		-f lavfi -i "color=c=black:s=${SIZE}:r=${FPS}:d=${CARD}" -i "${MUSIC}" \
		-filter_complex "${VIDEO};[2:a]atrim=0:${D},afade=t=in:st=0:d=1.5,afade=t=out:st=${FADE_AT}:d=1,apad[m]" \
		-map "[v]" -map "[m]" -t "${TOTAL}" -c:v libx264 -preset slow -crf 18 -c:a aac -b:a 192k \
		-movflags +faststart "${OUT}-teaser.mp4"
else
	ffmpeg -v error -y -framerate "${FPS}" -i "${FRAMES}/f%04d.png" \
		-f lavfi -i "color=c=black:s=${SIZE}:r=${FPS}:d=${CARD}" \
		-filter_complex "${VIDEO}" -map "[v]" -t "${TOTAL}" -c:v libx264 -preset slow -crf 18 \
		-movflags +faststart "${OUT}-teaser.mp4"
fi
ls -l "${OUT}"-*
