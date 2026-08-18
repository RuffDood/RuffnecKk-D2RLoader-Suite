set(RUFFNECKK_SUITE_PLUGIN_DIRECTORIES
    item-durability
    charm-aura-trigger-fix
    enhanced-damage-min-max-fix
    ethereal-item-rules
    repair-costs-cap
    vendor-stock-refresh
    potion-auto-pickup
    mass-identify
    cube-quick-move
    equipped-item-to-cube
    transmute-hotkey
    prevent-merc-death-in-town
    remote-stash
    floating-damage
    larzuk-sockets
    bulk-skill-point-allocation
    progressive-affixes
)

foreach(plugin_directory IN LISTS RUFFNECKK_SUITE_PLUGIN_DIRECTORIES)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${plugin_directory}/CMakeLists.txt")
        add_subdirectory("${plugin_directory}")
    else()
        message(STATUS "Suite component pending: ${plugin_directory}")
    endif()
endforeach()
