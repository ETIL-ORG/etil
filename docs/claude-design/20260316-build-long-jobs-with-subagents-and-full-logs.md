# Long Running Jobs: Subagent usage and log files.

### There is an issue with the long running local jobs ... The following does not apply to CI.

- You tend to monitor shell commands by piping the redirected output to tail or some other method that only catches a small portion of the output. 
- While this is OK for short (< 5 sec) jobs, Long jobs that fail have to be re-run from scratch to find the error. 
- This is *VERY* time comsuming for *ME*, and all that is saved is some disk space. Disk space is cheap.
- You should run 'long' (>= 5 sec) jobs with their complete redirected output saved to a .log file in the /tmp directory.
- These long jobs should be run by background agent(s).
- Your foreground should poll the background agent(s) for completion, and *then* tail the results as they complete.
- This avoids the oops-an-error-I-can't-see-lets-waste-time-re-running-from-the-start problem.
- This should reduce unnecessary context bloat also.
