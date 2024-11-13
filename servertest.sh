export port=8080
commands=("ls -l" "pwd" "date" "lscpu" "cat")


for i in "${commands[@]}"; do
    echo -e "\033[1;92mRunning command: $i\033[0m\n"
    ./server $port
    echo -e "\n"
    pkill -f "./server $port"
    wait
done