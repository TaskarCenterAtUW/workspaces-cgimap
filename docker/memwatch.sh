#!/bin/sh
# memwatch: log loudly when the kernel kills a process in this container for
# running out of memory (and, optionally, when a named process exits for any
# other reason).
#
# Why: when the kernel OOM-kills one process inside a container, the container
# keeps running, so the platform records no restart or OOM event. cgimap's
# parent process also reaps dead workers without logging why. Without this,
# such kills leave no trace beyond a dropped request.
#
# It wraps the container's real command: it starts a small background watcher,
# then execs the command unchanged, so the app keeps the same PID and signal
# handling it had before. Use it as the image's entrypoint:
#
#   ENTRYPOINT ["/bin/sh", "/path/to/memwatch.sh", <original command...>]
#   or: ENTRYPOINT ["/bin/sh", "/path/to/memwatch.sh"] with CMD [<command...>]
#
# Note: a Container App or Kubernetes "command" replaces the image ENTRYPOINT
# and so bypasses this; set the container's "args" instead.
#
# It needs only sh, grep, awk, sort and the usual coreutils, and reads the
# container's own cgroup memory counters (v1 or v2) and /proc.
#
# Settings (environment variables):
#   MEMWATCH_INTERVAL  seconds between checks (default 5)
#   MEMWATCH_PROC      process name, as in /proc/<pid>/comm (first 15 chars),
#                      whose exits are reported even without an OOM kill,
#                      e.g. "openstreetmap-c" for cgimap workers
#   MEMWATCH_HIGH_PCT  warn when process memory reaches this % of the limit
#                      (default 90; 0 turns it off). At most every 10 minutes.
#
# Every line starts with "[<UTC time> memwatch]" followed by one of:
#   started | OOM-KILL | EXIT | HIGH | not running
# Process IDs are written "#<pid>", as in cgimap's own log lines, so a killed
# worker's last request can be found by searching for its "#<pid>".

memwatch_log() { echo "[$(date -u +%Y-%m-%dT%H:%M:%SZ) memwatch] $*"; }

# memwatch_val FILE KEY: print the value from a "KEY value" line, if any
memwatch_val() {
  while read -r k v; do
    if [ "$k" = "$2" ]; then echo "$v"; return 0; fi
  done < "$1"
}

memwatch_mib() { echo "$(( ${1:-0} / 1048576 )) MiB"; }

# One line per process in this container: "pid rss_kb name".
# Leaves out this watcher and its helper processes (including this function's
# own grep and awk). grep reads /proc because it skips processes that exit
# mid-read instead of failing.
memwatch_snapshot() {
  read -r snap _ < /proc/self/stat              # PID of the subshell running this
  grep -H -E '^(Name|PPid|VmRSS):' /proc/[0-9]*/status 2>/dev/null |
  awk -v self="$memwatch_self" -v snap="$snap" '
    function flush() {
      if (cf != "" && pid != self && pid != snap && ppid != self && ppid != snap) print pid, rss, name
    }
    {
      i = index($0, ":"); f = substr($0, 1, i - 1); rest = substr($0, i + 1)
      j = index(rest, ":"); k = substr(rest, 1, j - 1); v = substr(rest, j + 1)
      sub(/^[ \t]+/, "", v)
      if (f != cf) {
        flush(); cf = f; pid = f
        sub(/\/status$/, "", pid); sub(/.*\//, "", pid)
        name = ""; ppid = ""; rss = 0
      }
      if (k == "Name") name = v
      else if (k == "PPid") ppid = v
      else if (k == "VmRSS") { rss = v; sub(/ .*/, "", rss) }
    }
    END { flush() }'
}

# "pid rss_kb name" lines -> "#57 openstreetmap-c (1534 MiB), #58 ..."
memwatch_describe() {
  printf '%s\n' "$1" | awk '
    NF >= 2 { n = $3; for (i = 4; i <= NF; i++) n = n " " $i
              out = out sep "#" $1 " " n " (" int($2 / 1024) " MiB)"; sep = ", " }
    END { print (out == "" ? "none seen" : out) }'
}

memwatch_main() {
  iv=${MEMWATCH_INTERVAL:-5}
  proc=${MEMWATCH_PROC:-}
  hipct=${MEMWATCH_HIGH_PCT:-90}
  read -r memwatch_self _ < /proc/self/stat   # this watcher's own PID

  if [ -r /sys/fs/cgroup/memory/memory.oom_control ]; then       # cgroup v1
    cg=/sys/fs/cgroup/memory; cgv=1
    oomf=$cg/memory.oom_control; peakf=$cg/memory.max_usage_in_bytes; anonkey=total_rss
    limit=$(cat "$cg/memory.limit_in_bytes")
  elif [ -r /sys/fs/cgroup/memory.events ]; then                 # cgroup v2
    cg=/sys/fs/cgroup; cgv=2
    oomf=$cg/memory.events; peakf=$cg/memory.peak; anonkey=anon
    limit=$(cat "$cg/memory.max")
  else
    memwatch_log "not running: this container has no cgroup memory counters"
    return 0
  fi
  case $limit in ''|max) limit=0 ;; esac
  [ "$limit" -gt 1099511627776 ] && limit=0      # v1 reports "no limit" as a huge number
  if [ "$limit" -gt 0 ]; then
    lim_s=$(memwatch_mib "$limit"); why="for exceeding this container's $lim_s memory limit"
  else
    lim_s="no"; why="for running out of memory"
  fi

  oom=$(memwatch_val "$oomf" oom_kill); oom=${oom:-0}
  memwatch_log "started (cgroup v$cgv): $lim_s memory limit, $oom OOM kills so far," \
    "checking every ${iv}s${proc:+, reporting any exit of '$proc' processes}"

  prev=$(memwatch_snapshot)
  last_high=0
  while sleep "$iv"; do
    cur=$(memwatch_snapshot)
    curpids=" $(printf '%s\n' "$cur" | cut -d' ' -f1 | tr '\n' ' ')"
    gone=$(printf '%s\n' "$prev" | while read -r pid rss name; do
             [ -n "$pid" ] || continue
             case $curpids in *" $pid "*) ;; *) echo "$pid $rss $name" ;; esac
           done)

    now_oom=$(memwatch_val "$oomf" oom_kill); now_oom=${now_oom:-0}
    if [ "$now_oom" -gt "$oom" ]; then
      memwatch_log "OOM-KILL: the kernel killed $((now_oom - oom)) process(es) $why" \
        "($now_oom since the container started)." \
        "Gone since the last check, with memory at that check: $(memwatch_describe "$gone")." \
        "Process memory now $(memwatch_mib "$(memwatch_val "$cg/memory.stat" "$anonkey")")," \
        "peak $(memwatch_mib "$(cat "$peakf" 2>/dev/null)")."
      oom=$now_oom
    elif [ -n "$proc" ] && [ -n "$gone" ]; then
      exited=$(printf '%s\n' "$gone" | while read -r pid rss name; do
                 if [ "$name" = "$proc" ]; then echo "$pid $rss $name"; fi
               done)
      if [ -n "$exited" ]; then
        memwatch_log "EXIT: $(memwatch_describe "$exited") exited without an OOM kill" \
          "(a crash, or a normal shutdown)."
      fi
    fi

    if [ "$hipct" -gt 0 ] && [ "$limit" -gt 0 ]; then
      anon=$(memwatch_val "$cg/memory.stat" "$anonkey"); anon=${anon:-0}
      pct=$((anon * 100 / limit))
      if [ "$pct" -ge "$hipct" ]; then
        now=$(date +%s)
        if [ $((now - last_high)) -ge 600 ]; then
          memwatch_log "HIGH: process memory is $(memwatch_mib "$anon"), $pct% of the $lim_s limit." \
            "Largest: $(memwatch_describe "$(printf '%s\n' "$cur" | sort -k2,2nr | head -n 3)")."
          last_high=$now
        fi
      fi
    fi
    prev=$cur
  done
}

if [ $# -eq 0 ]; then
  echo "memwatch: no command to run" >&2
  exit 64
fi
memwatch_main &
exec "$@"
