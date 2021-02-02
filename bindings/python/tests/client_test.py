
import unittest
import time
import os
import subprocess as sub
import sys


# include terminal interface for travis parallel executions of scripts which use
# terminal features: fix multiple stdin assignment at termios.tcgetattr
if os.name != 'nt':
    import pty


class test_example_client(unittest.TestCase):

    def test_execute_client(self):
        if os.name == 'nt':
            # TODO: fix windows includes of client.py
            return
        my_stdin = sys.stdin
        if os.name != 'nt':
            master_fd, slave_fd = pty.openpty()
            # slave_fd fix multiple stdin assignment at termios.tcgetattr
            my_stdin = slave_fd

        process = sub.Popen(
            [sys.executable, "client.py", "url_seed_multi.torrent"],
            stdin=my_stdin, stdout=sub.PIPE, stderr=sub.PIPE)
        # python2 has no Popen.wait() timeout
        time.sleep(5)
        returncode = process.poll()
        if returncode is None:
            # this is an expected use-case
            process.kill()
        err = process.stderr.read().decode("utf-8")
        self.assertEqual('', err, 'process throw errors: \n' + err)
        # check error code if process did unexpected end
        if returncode is not None:
            # in case of error return: output stdout if nothing was on stderr
            if returncode != 0:
                print("stdout:\n" + process.stdout.read().decode("utf-8"))
            self.assertEqual(returncode, 0, "returncode: " + str(returncode) + "\n"
                             + "stderr: empty\n"
                             + "some configuration does not output errors like missing module members,"
                             + "try to call it manually to get the error message\n")
