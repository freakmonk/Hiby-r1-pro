import re

with open("src/menu.c", "r") as f:
    content = f.read()

# 1. Update the layout defines
old_layout = """#define MENU_ROW_PLAYER       0
#define MENU_ROW_GAMES_HEADER 1
#define MENU_ROW_PALETTE      2
#define MENU_ROW_FIRST_ROM    3"""

new_layout = """#define MENU_ROW_PALETTE      0
#define MENU_ROW_FIRST_ROM    1"""

content = content.replace(old_layout, new_layout)

# 2. Update UTIL_COUNT to 0
content = re.sub(r"#define UTIL_COUNT\s+5", "#define UTIL_COUNT           0", content)

# 3. Remove drawing logic for removed items
old_draw = """        if (index == MENU_ROW_PLAYER) {
            draw_row(platform, y, is_selected, "HIBY PLAYER", COL_PLAYER, width);
        } else if (index == MENU_ROW_GAMES_HEADER) {
            draw_section_header(platform, y, "GAMES");
        } else if (index == MENU_ROW_PALETTE) {"""

new_draw = """        if (index == MENU_ROW_PALETTE) {"""

content = content.replace(old_draw, new_draw)

# 4. Fix skip_header so it doesn't reference MENU_ROW_GAMES_HEADER
old_skip = """static int skip_header(int index, int dir, int total) {
    if (index == MENU_ROW_GAMES_HEADER) {
        index = (index + dir + total) % total;
    }
    return index;
}"""

new_skip = """static int skip_header(int index, int dir, int total) {
    return index;
}"""

content = content.replace(old_skip, new_skip)

# 5. Fix initial selected logic and tap logic
old_selected = """    int selected = (start_index && *start_index < total) ? *start_index : 0;
    if (selected < 0 || selected == MENU_ROW_GAMES_HEADER) selected = 0;"""

new_selected = """    int selected = (start_index && *start_index < total) ? *start_index : 0;
    if (selected < 0) selected = 0;"""

content = content.replace(old_selected, new_selected)

old_tap = """                index >= 0 && index < total && index != MENU_ROW_GAMES_HEADER) {"""
new_tap = """                index >= 0 && index < total) {"""
content = content.replace(old_tap, new_tap)

# 6. Idle timeout action
old_idle = """                if (start_index) *start_index = MENU_ROW_PLAYER;
                result.action = GB_MENU_PLAYER;
                return result;"""
new_idle = """                /* Do nothing on idle timeout now that HIBY PLAYER is removed */
                idle_ms = 0;"""
content = content.replace(old_idle, new_idle)

# 7. Player selection action (just to be safe, though unreachable now)
old_player_action = """            if (selected == MENU_ROW_PLAYER) {
                result.action = GB_MENU_PLAYER;
            } else {"""
new_player_action = """            if (0) {
            } else {"""
content = content.replace(old_player_action, new_player_action)

with open("src/menu.c", "w") as f:
    f.write(content)

