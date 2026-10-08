idf_component_register(
    SRCS "main.c"
         "incense_model.c"
         "incense_store.c"
         "incense_audio.c"
         "incense_ui.c"
    INCLUDE_DIRS "."
    REQUIRES bsp nvs_flash esp_timer
)

# 焚香计时器中文字库子集（生成物，见 assets/fonts/generate_fonts.sh）。
# 只用 target_sources 这一种方式注册，避免重复编译同一字体。
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/app_font_48.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/app_font_24.c"
)
