import re

with open("Makefile", "r") as f:
    makefile = f.read()

makefile = makefile.replace("-O2", "-O3 -flto")

with open("Makefile", "w") as f:
    f.write(makefile)

with open("src/platform.c", "r") as f:
    plat = f.read()

plat = plat.replace("unsigned int btime = 50000;", "unsigned int btime = 150000; /* 150 ms buffer for stability */")
plat = plat.replace("unsigned int ptime = 10000;", "unsigned int ptime = 25000; /* 25 ms period */")

with open("src/platform.c", "w") as f:
    f.write(plat)
