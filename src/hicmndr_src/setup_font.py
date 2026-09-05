import urllib.request
import os

print("Downloading stb_truetype.h...")
urllib.request.urlretrieve("https://raw.githubusercontent.com/nothings/stb/master/stb_truetype.h", "stb_truetype.h")

print("Downloading Roboto-Regular.ttf...")
urllib.request.urlretrieve("https://github.com/googlefonts/roboto/raw/main/src/hinted/Roboto-Regular.ttf", "roboto.ttf")

print("Generating C header...")
with open("roboto.ttf", "rb") as f:
    data = f.read()

with open("roboto_ttf.h", "w") as f:
    f.write("#pragma once\n")
    f.write("const unsigned char roboto_ttf[] = {\n")
    chunk_size = 20
    for i in range(0, len(data), chunk_size):
        chunk = data[i:i+chunk_size]
        f.write(",".join([str(b) for b in chunk]) + ",\n")
    f.write("};\n")
    f.write(f"const unsigned int roboto_ttf_len = {len(data)};\n")

print("Done.")
