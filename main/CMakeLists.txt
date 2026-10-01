# 游戏盒（Game Box）应用固件。
#
# 基线参考 demo（main.c、demo_*.c、ui_pixel*.c）保留在本目录作为 BSP 用法参考，
# 并继续由 tools/validate.sh 的 host tests 覆盖，但不编译进本固件：
# 二次开发应用必须使用自己的 UI，开机直接进入游戏选择页。
#
# 接入新游戏：把 main/games/<id>/ 下的 .c 加进下面的 GAME_SRCS，
# 再在 gb_registry.c 里注册。
set(GAME_SRCS
    "games/crane/crane_game.c"
    "games/crane/crane_model.c"
    "games/crane/crane_flow.c"
    "games/crane/crane_sfx.c"
    "games/flappy/flappy_game.c"
    "games/flappy/flappy_model.c"
    "games/flappy/flappy_flow.c"
    "games/flappy/flappy_sfx.c"
    "games/dodge/dodge_game.c"
    "games/dodge/dodge_model.c"
    "games/dodge/dodge_draw.c"
    "games/tap/tap_game.c"
    "games/tap/tap_model.c"
)

idf_component_register(
    SRCS "gb_main.c"
         "gb_app.c"
         "gb_nav.c"
         "gb_registry.c"
         "launcher/gb_launcher.c"
         "launcher/gb_launcher_layout.c"
         "common/gb_canvas.c"
         "common/gb_i4.c"
         "common/gb_idle.c"
         "common/gb_score.c"
         "common/gb_score_key.c"
         "common/gb_sfx.c"
         "common/gb_tone.c"
         "common/gb_ui.c"
         ${GAME_SRCS}
    INCLUDE_DIRS "." "common" "launcher"
    REQUIRES bsp esp_hw_support esp_timer nvs_flash
)

# 共用中文字库，由 tools/gamebox/gen_fonts.py 生成。
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/gb_font_14.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/gb_font_16.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/gb_font_20.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/gb_font_24.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/gb_font_28.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/gb_font_40.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/gb_num_48.c"
)

# 飞吧小鸟的精灵图（约 140KB Flash，常量数据不占 RAM），由 tools/gamebox/gen_flappy_assets.py 生成。
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/images/flappy_sprites.c"
)
