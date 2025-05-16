# iptracking

Infrastructure for tracking PAM sshd events






The PAM exec module executes a program (e.g. a script) with the connection information present in the environment.  If the `iptracking-daemon` is not online then a write to the named pipe will block, and that would suspend the PAM stack for SSH logins.  A better solution is to write to the named pipe with a timeout should the data not be read, e.g.

```
echo "8.8.8.8,6.6.6.6,12345,2,frey,2025-05-16 15:42:00-0400" | timeout -s SIGKILL 5s tee -a iptracking.fifo 
```
