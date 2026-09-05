import re

with open("src/platform.c", "r") as f:
    content = f.read()

content = content.replace("{ 0.02f, 0.72f, 0.12f, 0.82f, TB_LEFT  },", "{ 0.01f, 0.69f, 0.16f, 0.84f, TB_LEFT  },")
content = content.replace("{ 0.26f, 0.72f, 0.36f, 0.82f, TB_RIGHT },", "{ 0.23f, 0.69f, 0.38f, 0.84f, TB_RIGHT },")
content = content.replace("{ 0.38f, 0.62f, 0.45f, 0.72f, TB_VOL_MINUS },", "{ 0.35f, 0.52f, 0.49f, 0.72f, TB_VOL_MINUS },")
content = content.replace("{ 0.47f, 0.62f, 0.54f, 0.72f, TB_VOL_PLUS  },", "{ 0.51f, 0.52f, 0.65f, 0.72f, TB_VOL_PLUS  },")

with open("src/platform.c", "w") as f:
    f.write(content)
