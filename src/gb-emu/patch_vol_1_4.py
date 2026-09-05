import re

with open("src/platform.c", "r") as f:
    content = f.read()

content = content.replace("static int current_volume = 51; /* 5% default (51/1024) */", "static int current_volume = 41; /* 4% default (41/1024) */")
content = content.replace("current_volume += 20; /* ~2% (20/1024) */", "current_volume += 10; /* ~1% (10/1024) */")
content = content.replace("current_volume -= 20; /* ~2% (20/1024) */", "current_volume -= 10; /* ~1% (10/1024) */")

with open("src/platform.c", "w") as f:
    f.write(content)
