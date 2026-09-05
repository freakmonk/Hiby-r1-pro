import re

with open("src/platform.c", "r") as f:
    content = f.read()

content = content.replace("current_volume += 51;", "current_volume += 20; /* ~2% (20/1024) */")
content = content.replace("current_volume -= 51;", "current_volume -= 20; /* ~2% (20/1024) */")

with open("src/platform.c", "w") as f:
    f.write(content)
