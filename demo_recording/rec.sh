#!/bin/bash
# rec.sh start <segment-name> | stop | status
# Full-screen capture (screen 0) at 15fps, scaled to 1728 wide. Each segment is a separate MP4;
# start/stop are written to timeline.jsonl so the edit can line segments up with events.
D="$(cd "$(dirname "$0")" && pwd)"
PIDF="$D/.rec.pid"; SEGF="$D/.rec.seg"
case "$1" in
  start)
    "$0" stop >/dev/null 2>&1
    seg="$(date +%H%M%S)-${2:-segment}"
    out="$D/video/$seg.mp4"
    # device indices shift when cameras come and go: look the screen up by name
    idx=$(ffmpeg -f avfoundation -list_devices true -i "" 2>&1 | sed -n 's/.*\[\([0-9]*\)\] Capture screen 0.*/\1/p' | head -1)
    [ -n "$idx" ] || { echo "rec.sh: 'Capture screen 0' not found - not recording (won't guess a device)" >&2; exit 1; }
    nohup ffmpeg -nostdin -y -f avfoundation -capture_cursor 1 -framerate 15 -i "${idx}:none" \
      -vf scale=1728:-2 -c:v libx264 -preset ultrafast -crf 26 -pix_fmt yuv420p \
      "$out" >"$D/logs/ffmpeg-$seg.log" 2>&1 &
    echo $! >"$PIDF"; echo "$seg" >"$SEGF"
    "$D/mark.sh" rec_start "$seg" ;;
  stop)
    [ -f "$PIDF" ] || exit 0
    pid=$(cat "$PIDF"); kill -INT "$pid" 2>/dev/null
    for i in $(seq 50); do kill -0 "$pid" 2>/dev/null || break; sleep 0.2; done
    if kill -0 "$pid" 2>/dev/null; then
      echo "rec.sh: ffmpeg (pid $pid) is still running - kept $PIDF; stop it and re-run 'rec.sh stop'" >&2
      exit 1
    fi
    "$D/mark.sh" rec_stop "$(cat "$SEGF")"; rm -f "$PIDF" "$SEGF" ;;
  status) [ -f "$PIDF" ] && kill -0 "$(cat "$PIDF")" 2>/dev/null && echo "recording $(cat "$SEGF")" || echo "idle" ;;
esac
