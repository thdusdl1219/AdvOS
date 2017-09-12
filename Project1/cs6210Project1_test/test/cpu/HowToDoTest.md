### Configuration

1. Create 8 virtual machines.
2. Put their name in vmlist.conf
3. Shutdown all of them

### Run testcases

1. Run startallvm.py
2. Run makeall.sh
3. Run assignall.sh
4. For each test case (1-5) do Steps 5 - 8
5. Run the command `script testcase-<testcase#>.log` (where <testcase#> is the current test case (1-5)). In the same terminal, run the command `./monitor.py`.
6. In a separate terminal run `./master-test-runner.sh <testcase#>`. This would launch test workloads in the VMs running. You can see the effect of these workloads in the output of monitor.py (step 5).
7. Observe the output of monitor.py (step 5) to verify expected operation of cpu scheduler. Once satisfied, kill the `monitor.py` execution (Ctrl-^C). Then run command `exit` in that terminal to stop logging.
8. Run command `killall.py` to kill the processes in each VM. Also kill the process `master-test-runner.sh` started in step 6.

### Cleanup
1. Run shutdownallvm.py or destroyallvm.py 
2. Run cleanall.py
