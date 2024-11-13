#!/bin/bash
export address=129.108.156.68
export port=8080
commands=("ls -l" "pwd" "date" "lscpu" "cat")


for i in "${commands[@]}"; do
    echo -e "\033[1;92mRunning command: $i\033[0m\n"
    ./client $address $port "$i"
    pkill -f "./client $address $port"
    echo -e "\n"
    wait
done

