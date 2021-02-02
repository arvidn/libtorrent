
import unittest
import time
import subprocess as sub
import sys


class test_example_client(unittest.TestCase):

    def test_execute_simple_client(self):
        process = sub.Popen(
            [sys.executable, "simple_client.py", "url_seed_multi.torrent"],
            stdout=sub.PIPE, stderr=sub.PIPE)
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
