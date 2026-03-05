#!/bin/bash


if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <A|B|C> [guest_path]"
    exit 1
fi

program="./Version_$1/mini_hypervisor.a"
if [[ ! -d "./Version_$1" ]] || [[ ! -x "$program" ]]; then
    echo -e "\x1b[31m[TEST]\x1b[0m Error: Directory '$1' or program 'mini_hypervisor.a' not found or not executable."
    exit 1
fi

guest="./Test/test$2/guest.img"
if [[ ! -d "./Test/test$2" ]] || [[ ! -x "$guest" ]]; then
    echo -e "\x1b[31m[TEST]\x1b[0m Error: Directory '$1' or program 'guest.img' not found or not executable."
    exit 1
fi

mem_values="2 4 8"
page_values="2 4"

for m in $mem_values; do

    for p in $page_values; do
        $program -m "$m" -p "$p" -g "$guest" 1>/dev/null 2>/dev/null
        ret=$?
        if [[ $ret -eq 0 ]]; then
            color="\x1b[32m"    # green
            result="Passed"
            post=""
        else
            color="\x1b[31m"    # red
            result="Failed"
            post="Error code: $ret"
        fi
        echo -e "$color[TEST]\x1b[0m $result with values -m $m -p $p -g ${guests[@]}. $post"

    done

done