Import("env")
import subprocess
import os
import sys

dir_6502 = os.path.join(env.subst("$PROJECT_DIR"), "6502")
print("Building 6502 driver: make -C %s" % dir_6502)
rc = subprocess.call(["make", "-C", dir_6502])
if rc != 0:
    print("ERROR: 6502 driver build failed (make returned %d)" % rc, file=sys.stderr)
    env.Exit(rc)
