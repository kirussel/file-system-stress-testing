# Brannagh

This is a classic kernel stress test. This example does not use any special
test strategies to reduce the test code to the minimum required to reproduce
the kernel core. We run every test operation randomly waiting for the kernel to
core.  This test may take hours to cause the core and we have no idea what
operations are critical to reproducing the kernel bug.

```
# you have already followed the installation instructions
DIR=${HOME}/fsstf

# run the classic stress test
BRANNAGH=${DIR?}/file-system-stress-testing-framework-read-only/src/brannagh
gmake -C ${BRANNAGH?} check
```

## Dashboard

Run `./brannagh -s` to get a live dashboard on a tty. It shows
run time, total ops and ops/sec, how many workers are alive, the last worker to
die (with its signal and the op it was in), workers stuck in one op for more
than 10 seconds, and the most frequently run ops.

`-s` cannot be combined with `-v`: verbose output from the workers would
corrupt the dashboard, so the combination is rejected with a usage error.
