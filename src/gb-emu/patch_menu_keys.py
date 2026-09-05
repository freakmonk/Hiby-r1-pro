import re

with open("src/platform.c", "r") as f:
    content = f.read()

old_menu = """                case KEY_ENTER:
                case KEY_PLAYPAUSE:
                case KEY_SPACE:         k = GB_KEY_SELECT; break;
                case KEY_NEXTSONG:
                case KEY_ESC:"""

new_menu = """                case KEY_NEXTSONG:
                case KEY_ENTER:
                case KEY_PLAYPAUSE:
                case KEY_SPACE:         k = GB_KEY_SELECT; break;
                case KEY_ESC:"""

content = content.replace(old_menu, new_menu)

with open("src/platform.c", "w") as f:
    f.write(content)
