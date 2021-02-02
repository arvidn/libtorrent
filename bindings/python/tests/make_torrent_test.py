
import unittest
import subprocess as sub
import sys


class test_example_client(unittest.TestCase):

    def test_execute_make_torrent(self):
        process = sub.Popen(
            [sys.executable, "make_torrent.py", "url_seed_multi.torrent",
             "http://test.com/test"], stdout=sub.PIPE, stderr=sub.PIPE)
        returncode = process.wait()
        # python2 has no Popen.wait() timeout
        err = process.stderr.read().decode("utf-8")
        self.assertEqual('', err, 'process throw errors: \n' + err)
        # in case of error return: output stdout if nothing was on stderr
        if returncode != 0:
            print("stdout:\n" + process.stdout.read().decode("utf-8"))
        self.assertEqual(returncode, 0, "returncode: " + str(returncode) + "\n"
                         + "stderr: empty\n"
                         + "some configuration does not output errors like missing module members,"
                         + "try to call it manually to get the error message\n")
