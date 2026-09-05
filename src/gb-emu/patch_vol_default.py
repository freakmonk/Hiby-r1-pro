import re

with open("src/platform.c", "r") as f:
    content = f.read()

content = content.replace("static int current_volume = 512; /* 50% default (512/1024) */", "static int current_volume = 51; /* 5% default (51/1024) */")

with open("src/platform.c", "w") as f:
    f.write(content)
