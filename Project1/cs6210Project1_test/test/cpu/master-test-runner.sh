if [ "$#" -ne 1 ]; then
    echo "Illegal number of parameters"
    echo "Usage : ./master-test-runner.sh <test#>"
    exit 1
fi

testcase="runtest${1}.py"

CPU_SCHED_PATH=/home/harshit/Desktop/project1/cpu/vcpu_scheduler 

python $testcase ; echo "Launched test case ${1}" ; sleep 10 ; $CPU_SCHED_PATH 3
