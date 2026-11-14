set(MAP_SEED_SELECTOR_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${MAP_SEED_SELECTOR_GENERATED_DIR}")
set(MAP_SEED_SELECTOR_BUTTON_SPRITE
    "${CMAKE_CURRENT_LIST_DIR}/resources/add-seed-button.sprite")
set(MAP_SEED_SELECTOR_BUTTON_LOWEND_SPRITE
    "${CMAKE_CURRENT_LIST_DIR}/resources/add-seed-button.lowend.sprite")
set(MAP_SEED_SELECTOR_QUESTION_BUTTON_SPRITE
    "${CMAKE_CURRENT_LIST_DIR}/resources/question-button.sprite")
set(MAP_SEED_SELECTOR_QUESTION_BUTTON_LOWEND_SPRITE
    "${CMAKE_CURRENT_LIST_DIR}/resources/question-button.lowend.sprite")
set(MAP_SEED_SELECTOR_MODAL_BUTTON_SPRITE
    "${CMAKE_CURRENT_LIST_DIR}/resources/modal-button.sprite")
set(MAP_SEED_SELECTOR_MODAL_BUTTON_LOWEND_SPRITE
    "${CMAKE_CURRENT_LIST_DIR}/resources/modal-button.lowend.sprite")
set(MAP_SEED_SELECTOR_CURRENT_SEED_PANEL_SPRITE
    "${CMAKE_CURRENT_LIST_DIR}/resources/current-seed-panel.sprite")
set(MAP_SEED_SELECTOR_CURRENT_SEED_PANEL_LOWEND_SPRITE
    "${CMAKE_CURRENT_LIST_DIR}/resources/current-seed-panel.lowend.sprite")
set(MAP_SEED_SELECTOR_CURRENT_SEED_PANEL_BUTTON_SPRITE
    "${CMAKE_CURRENT_LIST_DIR}/resources/current-seed-panel-button.sprite")
set(MAP_SEED_SELECTOR_CURRENT_SEED_PANEL_BUTTON_LOWEND_SPRITE
    "${CMAKE_CURRENT_LIST_DIR}/resources/current-seed-panel-button.lowend.sprite")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${MAP_SEED_SELECTOR_BUTTON_SPRITE}"
    "${MAP_SEED_SELECTOR_BUTTON_LOWEND_SPRITE}"
    "${MAP_SEED_SELECTOR_QUESTION_BUTTON_SPRITE}"
    "${MAP_SEED_SELECTOR_QUESTION_BUTTON_LOWEND_SPRITE}"
    "${MAP_SEED_SELECTOR_MODAL_BUTTON_SPRITE}"
    "${MAP_SEED_SELECTOR_MODAL_BUTTON_LOWEND_SPRITE}"
    "${MAP_SEED_SELECTOR_CURRENT_SEED_PANEL_SPRITE}"
    "${MAP_SEED_SELECTOR_CURRENT_SEED_PANEL_LOWEND_SPRITE}"
    "${MAP_SEED_SELECTOR_CURRENT_SEED_PANEL_BUTTON_SPRITE}"
    "${MAP_SEED_SELECTOR_CURRENT_SEED_PANEL_BUTTON_LOWEND_SPRITE}")
set(MAP_SEED_SELECTOR_BUTTON_HEADER
    "${MAP_SEED_SELECTOR_GENERATED_DIR}/add_seed_button_sprite.hpp")
file(WRITE "${MAP_SEED_SELECTOR_BUTTON_HEADER}"
    "#pragma once\n#include <array>\n#include <cstdint>\nnamespace MapSeedSelector::EmbeddedAssets {\n")
foreach(_asset IN ITEMS
        "AddSeedButtonSprite|${MAP_SEED_SELECTOR_BUTTON_SPRITE}"
        "AddSeedButtonLowEndSprite|${MAP_SEED_SELECTOR_BUTTON_LOWEND_SPRITE}"
        "QuestionButtonSprite|${MAP_SEED_SELECTOR_QUESTION_BUTTON_SPRITE}"
        "QuestionButtonLowEndSprite|${MAP_SEED_SELECTOR_QUESTION_BUTTON_LOWEND_SPRITE}"
        "ModalButtonSprite|${MAP_SEED_SELECTOR_MODAL_BUTTON_SPRITE}"
        "ModalButtonLowEndSprite|${MAP_SEED_SELECTOR_MODAL_BUTTON_LOWEND_SPRITE}"
        "CurrentSeedPanelSprite|${MAP_SEED_SELECTOR_CURRENT_SEED_PANEL_SPRITE}"
        "CurrentSeedPanelLowEndSprite|${MAP_SEED_SELECTOR_CURRENT_SEED_PANEL_LOWEND_SPRITE}"
        "CurrentSeedPanelButtonSprite|${MAP_SEED_SELECTOR_CURRENT_SEED_PANEL_BUTTON_SPRITE}"
        "CurrentSeedPanelButtonLowEndSprite|${MAP_SEED_SELECTOR_CURRENT_SEED_PANEL_BUTTON_LOWEND_SPRITE}")
    string(REPLACE "|" ";" _asset_fields "${_asset}")
    list(GET _asset_fields 0 _asset_name)
    list(GET _asset_fields 1 _asset_path)
    file(READ "${_asset_path}" _asset_hex HEX)
    string(LENGTH "${_asset_hex}" _asset_hex_length)
    math(EXPR _asset_byte_count "${_asset_hex_length} / 2")
    string(REGEX REPLACE "([0-9a-fA-F][0-9a-fA-F])" "0x\\1,"
        _asset_bytes "${_asset_hex}")
    file(APPEND "${MAP_SEED_SELECTOR_BUTTON_HEADER}"
        "inline constexpr std::array<std::uint8_t, ${_asset_byte_count}> ${_asset_name}{${_asset_bytes}};\n")
endforeach()
file(APPEND "${MAP_SEED_SELECTOR_BUTTON_HEADER}" "}\n")
