#!/bin/zsh -f
# This file has been generated in whole or in part by a Large Language Model

# Copyright (c) 2026 Joseph Jones
# Licensed under the Kunix License v1.0 or later
# See LICENSE for details

# xtest -- X on Kunix, end to end: the server comes up on the console, a
# client (xterm) runs and gets the keys typed at it, the mouse moves, and
# when the server goes the console is given back. Keys and mouse motion
# come from the test harness ("monitor:" lines on the serial port). Needs
# X installed (buildutils/x11.sh). The last line is "xtest: N passed, M
# failed".

exec 3>/dev/serial
integer passed=0 failed=0

say() { print -r -- "$1"; print -r -- "$1" >&3 }

check() {
    local name=$1; shift
    if "$@" >/dev/null 2>&1; then
        (( passed++ )); say "PASS $name"
    else
        (( failed++ )); say "FAIL $name"
    fi
}

eq() { [[ $1 == $2 ]] }
monitor() { print -r -- "monitor: $1" >&3 }

# Until the condition holds, for at most n tenths of a second.
wait_until() {
    integer i=0
    until eval "$1"; do
        (( i++ < $2 )) || return 1
        sleep 0.1
    done
}

say "xtest: starting (pid $$)"
if [[ ! -x /usr/bin/Xfbdev ]]; then
    say "FAIL X is installed (buildutils/x11.sh, then build.sh)"
    say "xtest: 0 passed, 1 failed"
    exit 1
fi
rm -f /tmp/xtest.*

Xfbdev :0 -nolisten tcp -keybd keyboard -mouse mouse,,protocol=ps/2 2>/tmp/xtest.server &
integer server=$!
export DISPLAY=:0
check "the server starts and listens on /tmp/.X11-unix/X0" wait_until '[[ -S /tmp/.X11-unix/X0 ]]' 200

# The pointer starts in the middle of the screen. The console's cell size
# is 8x16; place the xterm over that point even on larger framebuffers.
# Without a window manager, keyboard focus follows the pointer.
read rows cols <<< "$(stty size)"
integer window_x=$(( cols * 4 - 200 )) window_y=$(( rows * 8 - 100 ))
xterm -geometry 80x24+${window_x}+${window_y} \
    -e /bin/sh -c 'echo "$TERM" > /tmp/xtest.term; read line; echo "$line" > /tmp/xtest.line' \
    2>/tmp/xtest.client &
integer client=$!
check "xterm starts a shell in its window" wait_until '[[ -s /tmp/xtest.term ]]' 200
check "with TERM=xterm" eq "$(</tmp/xtest.term)" xterm

sleep 1
for k in h e l l o ret; do monitor "sendkey $k"; done
check "keys typed reach the program in the xterm" wait_until '[[ -s /tmp/xtest.line ]]' 100
check "as typed" eq "$(</tmp/xtest.line)" hello
check "the xterm goes when its program does" wait_until '! kill -0 $client 2>/dev/null' 50

for i in {1..12}; do monitor "mouse_move 6 4"; done
sleep 1
check "the server takes the mouse moving" kill -0 $server

kill $server
check "the server exits when told to" wait_until '! kill -0 $server 2>/dev/null' 100
wait $server 2>/dev/null

# The console's keyboard is ours again: characters, not scancodes.
monitor "sendkey x"
monitor "sendkey ret"
line=
read -t 5 line
check "and the console has its keyboard back" eq "$line" x

say "xtest: $passed passed, $failed failed"
exit $failed
