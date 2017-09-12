if [ "$#" -ne 1 ]; then
    echo "Illegal number of parameters"
    echo "Usage : ./master-test-runner.sh <test#>"
    exit 1
fi

testcase="runtest${1}.py"

MEM_COORDINATOR_PATH=/home/harshit/Desktop/project1/memory/memory_coordinator 

python $testcase ; echo "Launched test case ${1}" ; sleep 10 ; $MEM_COORDINATOR_PATH 3
