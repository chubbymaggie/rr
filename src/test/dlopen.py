from rrutil import *

send_gdb('handle SIGKILL stop')
send_gdb('c')
expect_gdb('EXIT-SUCCESS')
expect_gdb('SIGKILL')
send_gdb('reverse-continue')
expect_gdb('stopped')

ok()
