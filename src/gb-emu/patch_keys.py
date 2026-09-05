import re

with open("src/platform.c", "r") as f:
    content = f.read()

old_menu = """                case KEY_NEXTSONG:
                case KEY_ENTER:
                case KEY_PLAYPAUSE:
                case KEY_SPACE:         k = GB_KEY_SELECT; break;
                case KEY_ESC:"""
new_menu = """                case KEY_ENTER:
                case KEY_PLAYPAUSE:
                case KEY_SPACE:         k = GB_KEY_SELECT; break;
                case KEY_NEXTSONG:
                case KEY_ESC:"""
content = content.replace(old_menu, new_menu)

old_game = """                case KEY_NEXTSONG:
                case KEY_ENTER:
                case KEY_PLAYPAUSE:     platform->key_held[TB_A] = pressed; break;
                case KEY_BACKSPACE:
                case KEY_STOP:          platform->key_held[TB_B] = pressed; break;"""
new_game = """                case KEY_ENTER:
                case KEY_PLAYPAUSE:     platform->key_held[TB_A] = pressed; break;
                case KEY_NEXTSONG:
                case KEY_BACKSPACE:
                case KEY_STOP:          platform->key_held[TB_B] = pressed; break;"""
content = content.replace(old_game, new_game)

with open("src/platform.c", "w") as f:
    f.write(content)
