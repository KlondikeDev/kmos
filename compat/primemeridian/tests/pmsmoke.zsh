#!/bin/zsh -f
# This file has been generated in whole or in part by a Large Language Model

# Copyright (c) 2026 Joseph Jones
# Licensed under the Kunix License v1.0 or later
# See LICENSE for details

# pmsmoke -- zsh and busybox, used the way a person at the prompt uses them.
# Being able to run at all already says #! scripts work. Results go to the
# terminal and /dev/serial; the last line is "pmsmoke: N passed, M failed".

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

# ---- files and /tmp ----
t_tmp_long_name() { print hello > /tmp/a-rather-long-name.txt && eq "$(</tmp/a-rather-long-name.txt)" hello }
t_heredoc()       { eq "$(cat <<EOF
here document
EOF
)" "here document" }
t_herestring()    { eq "$(tr a-z A-Z <<< abc)" ABC }
t_procsubst()     { diff <(print same) <(print same) }
t_devfd()         { [[ -e /dev/fd/0 && -c /dev/stdin ]] }
t_dot_entries()   { ls -a / | grep -qx '\.\.' }
t_cp_mv_rm()      { cp /test/hello.txt /tmp/h1 && mv /tmp/h1 /tmp/h2 && cmp /test/hello.txt /tmp/h2 && rm /tmp/h2 && [[ ! -e /tmp/h2 ]] }
t_fat_write()     { print kunix > /fat/SMOKE.TXT && eq "$(cat /fat/SMOKE.TXT)" kunix && rm /fat/SMOKE.TXT }
t_stat_size()     { eq "$(stat -c %s /test/big.txt)" 3080000 }
t_find()          { eq "$(find /test/docs -name '*.txt' | wc -l)" 2 }
t_tar_gzip()      { cd /tmp && mkdir -p tt && print data > tt/f && tar czf t.tgz tt && rm -r tt &&
                    tar xzf t.tgz && eq "$(<tt/f)" data && rm -r tt t.tgz; local r=$?; cd /; return $r }

# ---- text tools ----
t_wc_big()        { eq "$(wc -l < /test/big.txt)" 110000 }
t_grep_count()    { eq "$(grep -c 'of the big file' /test/big.txt)" 110000 }
t_md5()           { eq "$(md5sum < /test/hello.txt)" "2c286ac24d3b92caea2f1484e17ae2e6  -" }
t_sha256()        { eq "$(sha256sum < /test/hello.txt)" "15c9cdb3737daa276341cf2ec4360e5ec51c3eb4bf68f9c971ba5261fda66307  -" }
t_sort_uniq()     { eq "$(print -l c a b a | sort | uniq | tr '\n' ' ')" "a b c " }
t_sed()           { eq "$(print hello | sed 's/l/L/g')" heLLo }
t_awk()           { eq "$(print 1 2 3 | awk '{ print $1 + $2 + $3 }')" 6 }
t_cut()           { eq "$(cut -d' ' -f2 <<< 'a b c')" b }
t_xargs()         { eq "$(print /test/hello.txt | xargs head -1)" "Hello from UFS2!" }
t_seq_tail()      { eq "$(seq 1 1000 | tail -1)" 1000 }
t_dd()            { eq "$(dd if=/dev/zero bs=4096 count=10 2>/dev/null | wc -c)" 40960 }
t_gzip_big()      { eq "$(gzip -c /test/big.txt | gunzip | wc -l)" 110000 }
t_printf()        { eq "$(printf '%05d|%-3s|' 42 ab)" "00042|ab |" }
t_expr()          { eq "$(expr 6 \* 7)" 42 }

# ---- the system ----
t_id()            { eq "$(id -u)" 0 && eq "$(whoami)" root }
t_date()          { (( $(date +%Y) >= 2025 )) }
t_uname()         { eq "$(uname -s)" Linux }
t_ps()            { ps | grep -q zsh }
t_free()          { free | grep -q Mem: }
t_df()            { df / }
t_mounts()        { grep -q '^tmpfs /tmp' /proc/mounts }
t_env()           { env | grep -q '^PATH=' }

# ---- the shell ----
t_background()    { sleep 0.2 & wait $! }
t_subshell()      { (exit 3); eq $? 3 }
t_trap()          { local got=0; trap 'got=1' USR1; kill -USR1 $$; trap - USR1; eq $got 1 }
t_read_pipe()     { local a b; print "x y" | read a b; eq $b y }
t_arrays()        { local -a a=(3 1 2); local -a s=(${(o)a}); eq "$s" "1 2 3" }
t_glob_qual()     { local -a f=(/test/docs/*(.)); (( $#f == 1 )) }
t_arith()         { (( 2 ** 10 == 1024 && 17 % 5 == 2 )) }
t_cmd_subst()     { eq "$(print $(print nested))" nested }
t_kill0()         { kill -0 $$ }

say "pmsmoke: starting (pid $$)"
for t in ${(ok)functions[(I)t_*]}; do
    check ${t#t_} $t
done
say "pmsmoke: $passed passed, $failed failed"
exit $failed
