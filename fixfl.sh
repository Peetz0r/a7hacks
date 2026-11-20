#!/bin/sh
# fixwatch.sh - fill in the EXIF focal length of new photos from the SteadyShot setting.
# Run:   BK=/usr/bin/bk.elf sh /android/storage/sdcard0/HAX/fixwatch.sh &
# Only looks at the newest few files of the newest folder, so it stays fast however many photos are on the card.
FIXFL=${FIXFL:-/tmp/fixfl}                          # the fixfl binary (the SD card has no executable bit)
BK=${BK:-/usr/bin/bk.elf}                              # camera's Backup reader
BKID=${BKID:-10713ca}                                  # SteadyShot focal length (stills); 10713d6 is the other copy
DCIM=${DCIM:-/android/storage/sdcard0/DCIM}
LOG=${LOG:-/android/storage/sdcard0/HAX/fixfl.log}
POLL=${POLL:-1}                                        # seconds between checks
KEEP=${KEEP:-30}                                       # how many of the newest files to look at

trap '' HUP
NL='
'

say() {  # to the log file and to the terminal; the camera's clock is uptime-based in this mode, so time only
  echo "$(date '+[%T]' 2>/dev/null) $*" >> "$LOG"
  echo "$(date '+[%T]' 2>/dev/null) $*"
}

badread() {  # log the raw output of a failed read, but only when it changes
  [ "$1" = "$lastbad" ] && return
  lastbad=$1; say "bk read failed, raw output: [$1]"
}

read_mm() {  # sets MM to the current SteadyShot focal length in mm (empty on error)
  MM=
  out=$($BK r $BKID 2>&1)                # ignore bk's exit status, it may be non-zero even on success
  set -- $out                            # words: id:010713ca size:2 data: 1C,00,
  [ $# -ge 1 ] || { badread "$out"; return; }
  eval last=\${$#}                       # last word = the bytes
  lo=${last%%,*}; rest=${last#*,}; hi=${rest%%,*}
  case "$hi$lo" in
    ""|*[!0-9A-Fa-f]*) badread "$out"; return ;;
  esac
  [ ${#hi} -eq 2 ] && [ ${#lo} -eq 2 ] || { badread "$out"; return; }
  MM=$((0x$hi$lo)); lastbad=
}

filesize() { set -- $(ls -l "$1" 2>/dev/null); echo "$5"; }

latest_dir() { d=; for x in "$DCIM"/*MSDCF; do [ -d "$x" ] && d=$x; done; echo "$d"; }

newest() {  # the newest $KEEP photo names in folder $1, newest first
  ls -t "$1" 2>/dev/null | { n=0; while read -r f; do
    case "$f" in *.JPG|*.ARW|*.jpg|*.arw) echo "$f"; n=$((n+1)); [ $n -ge $KEEP ] && break ;; esac
  done; }
}

fresh=
insert_sorted() {  # add $1 to the space-separated list $fresh, keeping it sorted by name
  out=; placed=0
  for x in $fresh; do
    if [ $placed -eq 0 ] && [ "$(expr "$1" \< "$x")" = 1 ]; then out="$out$1 "; placed=1; fi
    out="$out$x "
  done
  [ $placed -eq 0 ] && out="$out$1 "
  fresh=$out
}

seen=" "
is_seen() { case "$seen" in *" $1 "*) return 0 ;; esac; return 1; }
add_seen() { seen="$seen$1 "; [ ${#seen} -gt 3000 ] && seen=" ${seen# * }"; }

# Photos already on the card are never touched: remember the newest ones as "seen".
dir=$(latest_dir)
[ -n "$dir" ] || { say "no *MSDCF folder under $DCIM"; exit 1; }
for f in $(newest "$dir"); do add_seen "$dir/$f"; done
say "started; watching $dir, using bk id $BKID"

# Each new photo is queued the moment it first shows up, together with the setting sampled just BEFORE that
# (so changing the setting afterwards, while it is still being written, does not matter). A queued photo is
# patched once its size has stopped changing. Queue records are  path|mm|lastsize  separated by spaces.
queue=
read_mm; prev=$MM
while true; do
  read_mm; now=$MM
  dir=$(latest_dir)

  # 1. discover new files and note the setting that was active when they appeared
  fresh=
  for f in $(newest "$dir"); do
    p="$dir/$f"
    is_seen "$p" && continue
    case " $queue" in *" $p|"*) continue ;; esac
    insert_sorted "$p"
  done
  for p in $fresh; do                   # in name order (ls -t gives an arbitrary order for files from the same second)
    mm=${prev:-$now}
    if [ -z "$mm" ]; then say "$p: could not read setting, skipped"; add_seen "$p"; continue; fi
    queue="$queue$p|$mm| "
  done

  # 2. patch queued files whose size has stopped changing
  newq=
  for rec in $queue; do
    p=${rec%%|*}; r=${rec#*|}; m=${r%%|*}; lastsz=${r#*|}
    cur=$(filesize "$p")
    if [ -n "$cur" ] && [ "$cur" -gt 0 ] && [ "$cur" = "$lastsz" ]; then
      res=$($FIXFL "$p" "$m"); rc=$?
      say "$p: ${m}mm -> $res (exit $rc)"
      add_seen "$p"
    else
      newq="$newq$p|$m|$cur "
    fi
  done
  queue=$newq

  [ -n "$now" ] && prev=$now
  sleep $POLL
done
