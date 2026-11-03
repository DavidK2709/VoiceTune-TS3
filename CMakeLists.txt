cmake_minimum_required(VERSION 3.16)
project(VoiceTune LANGUAGES CXX RC)
configure_file(${CMAKE_SOURCE_DIR}/APP-ICON.png ${CMAKE_BINARY_DIR}/APP-ICON.png COPYONLY) # die .rc bindet es ueber den Include-Pfad ein (Pfade mit Leerzeichen)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type)

# --- libopus (statisch, optional): ohne sie baut alles, nur die Opus-Vorschau fehlt ---------------
set(_old_suffixes ${CMAKE_FIND_LIBRARY_SUFFIXES})
set(CMAKE_FIND_LIBRARY_SUFFIXES .a) # nur die statische libopus.a, keine Import-Bibliothek (sonst haengt das Plugin an einer libopus-DLL)
find_library(OPUS_LIB opus)
set(CMAKE_FIND_LIBRARY_SUFFIXES ${_old_suffixes})
find_path(OPUS_INC opus/opus.h)
if (OPUS_LIB AND OPUS_INC)
    message(STATUS "libopus gefunden: ${OPUS_LIB}")
    add_compile_definitions(VOICETUNE_HAS_OPUS)
    include_directories(${OPUS_INC})
else()
    set(OPUS_LIB "")
    message(WARNING "libopus nicht gefunden (pacman -S mingw-w64-ucrt-x86_64-opus): Opus-Vorschau wird nicht eingebaut")
endif()

set(CORE_SOURCES
    src/app_discovery.cpp
    src/process_capture.cpp
    src/file_player.cpp
    src/i18n.cpp)

# --- TeamSpeak-Plugin -------------------------------------------------------
add_library(VoiceTune SHARED
    ${CORE_SOURCES}
    src/engine.cpp
    src/opus_preview.cpp
    src/settings.cpp
    src/config_dialog.cpp
    src/soundboard.cpp
    src/soundboard_view.cpp
    src/soundboard_window.cpp
    src/voicechanger.cpp
    src/voicechanger_page.cpp
    src/update_check.cpp
    src/mini_window.cpp
    src/tray.cpp
    src/board_file.cpp
    src/commands.cpp
    src/remote.cpp
    src/voicetune.rc
    src/plugin.cpp)
target_include_directories(VoiceTune PRIVATE third_party/ts3sdk src ${CMAKE_BINARY_DIR})
target_link_libraries(VoiceTune PRIVATE ole32 oleaut32 uuid user32 gdi32 gdiplus shell32 shlwapi comdlg32 ${OPUS_LIB} mfuuid ws2_32 winhttp -static -static-libgcc -static-libstdc++)
set_target_properties(VoiceTune PROPERTIES PREFIX "" OUTPUT_NAME "VoiceTune_win64")

# --- Standalone-Test (Capture ohne TeamSpeak) --------------------------------
add_executable(capture_test ${CORE_SOURCES} tools/capture_test.cpp)
target_include_directories(capture_test PRIVATE src)
target_link_libraries(capture_test PRIVATE -municode ole32 oleaut32 uuid user32 mfuuid -static -static-libgcc -static-libstdc++)

# --- Mixer-Selbsttest ---------------------------------------------------------
add_executable(mixer_test tools/mixer_test.cpp)
target_include_directories(mixer_test PRIVATE src)

# --- Equalizer / FFT / Analyse-Selbsttest -----------------------------------------
add_executable(eq_test tools/eq_test.cpp)
target_include_directories(eq_test PRIVATE src)

# --- Opus-Vorschau ----------------------------------------------------------------------
add_executable(opus_test tools/opus_test.cpp src/opus_preview.cpp)
target_include_directories(opus_test PRIVATE src)
target_link_libraries(opus_test PRIVATE ${OPUS_LIB} -static -static-libgcc -static-libstdc++)

# --- Klang-Optimierung ---------------------------------------------------------------
add_executable(optimizer_test tools/optimizer_test.cpp)
target_include_directories(optimizer_test PRIVATE src)

# --- Datei-Player (Dekodieren, Springen, Pause) ----------------------------------------
add_executable(player_test tools/player_test.cpp src/file_player.cpp)
target_include_directories(player_test PRIVATE src)
target_link_libraries(player_test PRIVATE ole32 uuid mfuuid -static -static-libgcc -static-libstdc++)

# --- Soundboard (Clips dekodieren, Stimmen mischen) ----------------------------------------
add_executable(soundboard_test tools/soundboard_test.cpp src/soundboard.cpp src/file_player.cpp)
target_include_directories(soundboard_test PRIVATE src)
target_link_libraries(soundboard_test PRIVATE ole32 uuid mfuuid -static -static-libgcc -static-libstdc++)

# --- Engine + Datei-Player ohne Fenster ----------------------------------------------------
add_executable(engine_files_test tools/engine_files_test.cpp src/engine.cpp src/soundboard.cpp src/opus_preview.cpp src/settings.cpp ${CORE_SOURCES})
target_include_directories(engine_files_test PRIVATE src)
target_link_libraries(engine_files_test PRIVATE ole32 oleaut32 uuid user32 winmm mfuuid ${OPUS_LIB} -static -static-libgcc -static-libstdc++)

# --- Wiedergabeliste ----------------------------------------------------------------
add_executable(playlist_test tools/playlist_test.cpp)
target_include_directories(playlist_test PRIVATE src)

# --- INI-Roundtrip (Einstellungen + eigene Presets) -------------------------------
add_executable(settings_test tools/settings_test.cpp src/settings.cpp)
target_include_directories(settings_test PRIVATE src)
target_link_libraries(settings_test PRIVATE -static -static-libgcc -static-libstdc++)

# --- Jitter-Simulation (alt vs. neu) --------------------------------------------
add_executable(jitter_test tools/jitter_test.cpp)
target_include_directories(jitter_test PRIVATE src)

# --- Nachgestelltes TeamSpeak: laedt die Plugin-DLL, treibt die Callbacks, macht Screenshots des Fensters -------------
add_executable(ts3_host_test tools/ts3_host_test.cpp)
target_include_directories(ts3_host_test PRIVATE third_party/ts3sdk)
target_link_libraries(ts3_host_test PRIVATE winmm gdi32 user32 -static -static-libgcc -static-libstdc++)

# --- Now-Playing: Titelbereinigung -------------------------------------------------------
add_executable(nowplaying_test tools/nowplaying_test.cpp)
target_include_directories(nowplaying_test PRIVATE src)

# --- Gemeinsame Befehle (Hotkeys, Stream-Deck-API) ------------------------------------------
add_executable(commands_test tools/commands_test.cpp src/commands.cpp src/engine.cpp src/soundboard.cpp src/opus_preview.cpp src/settings.cpp ${CORE_SOURCES})
target_include_directories(commands_test PRIVATE src)
target_link_libraries(commands_test PRIVATE ole32 oleaut32 uuid user32 winmm mfuuid ${OPUS_LIB} -static -static-libgcc -static-libstdc++)

# --- Uebersetzungstabelle (Deutsch/English) -------------------------------------------------
add_executable(i18n_test tools/i18n_test.cpp src/i18n.cpp)
target_include_directories(i18n_test PRIVATE src)
target_link_libraries(i18n_test PRIVATE -static -static-libgcc -static-libstdc++)

# --- Ducker, Lautheit, Soundboard-Datei (reine Logik) ----------------------------------------------------
add_executable(logic_test tools/logic_test.cpp src/board_file.cpp)
target_include_directories(logic_test PRIVATE src)
target_link_libraries(logic_test PRIVATE -static -static-libgcc -static-libstdc++)
