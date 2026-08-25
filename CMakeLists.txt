cmake_minimum_required(VERSION 3.22)

project(VSTHostApp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------
# CONFIGURAÇÃO OBRIGATÓRIA: caminho do ASIO SDK (baixado da Steinberg)
# Aponte para a pasta raiz do SDK, ex:
#   C:/SDKs/asiosdk_2.3.3_2019-06-14
# Essa pasta deve conter as subpastas "common", "host" e "host/pc"
# ------------------------------------------------------------------
set(ASIO_SDK_PATH "" CACHE PATH "Caminho para a raiz do ASIO SDK da Steinberg")

if(NOT ASIO_SDK_PATH)
    message(FATAL_ERROR
        "Defina ASIO_SDK_PATH apontando para a pasta do ASIO SDK.\n"
        "Exemplo: cmake -B build -DASIO_SDK_PATH=C:/SDKs/asiosdk_2.3.3_2019-06-14")
endif()

# ------------------------------------------------------------------
# CONFIGURAÇÃO OBRIGATÓRIA (para suporte a plugins legados VST2):
# caminho da RAIZ do VST2 SDK. Essa pasta deve conter, em algum lugar
# dentro dela, o caminho relativo:
#   pluginterfaces/vst2.x/aeffectx.h
# Exemplo de valor correto:
#   C:/SDKs/VST2_SDK
# ------------------------------------------------------------------
set(VST2_SDK_PATH "" CACHE PATH "Caminho para a raiz do VST2 SDK")

if(NOT VST2_SDK_PATH)
    message(FATAL_ERROR
        "Defina VST2_SDK_PATH apontando para a pasta raiz do VST2 SDK.\n"
        "Ela deve conter o caminho pluginterfaces/vst2.x/aeffectx.h dentro dela.\n"
        "Exemplo: cmake -B build -DVST2_SDK_PATH=C:/SDKs/VST2_SDK")
endif()

# ------------------------------------------------------------------
# JUCE: tenta usar uma cópia local em ./JUCE, senão baixa via FetchContent
# ------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 7.0.12
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_gui_app(VSTHostApp
    PRODUCT_NAME "VST Host App"
    COMPANY_NAME "MeuEstudio"
)

target_sources(VSTHostApp PRIVATE
    Source/Main.cpp
    Source/PluginHostEngine.cpp
    Source/MainComponent.cpp
    Source/PresetManager.cpp
    Source/PreferencesWindow.cpp
    Source/MidiRouter.cpp
    Source/MidiActionMap.cpp
    Source/MidiRouter.cpp
)

# Gera o arquivo JuceHeader.h (usado pelos #include <JuceHeader.h> no código)
# dentro da pasta de build, e já adiciona essa pasta ao include path do alvo.
juce_generate_juce_header(VSTHostApp)

target_compile_definitions(VSTHostApp PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:VSTHostApp,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:VSTHostApp,JUCE_VERSION>"

    # Habilita hosting de VST3 (SDK já embutido no JUCE)
    JUCE_PLUGINHOST_VST3=1
    # Habilita hosting de VST2 legado (requer VST2_SDK_PATH definido acima).
    # Plugins VST2 SÓ existem como binário nativo de uma arquitetura só
    # (normalmente x86 nesses plugins antigos) - por isso este projeto
    # precisa ser compilado como Win32 (x86) quando essa flag estiver ativa.
    JUCE_PLUGINHOST_VST=1

    # Habilita ASIO
    JUCE_ASIO=1
)

target_include_directories(VSTHostApp PRIVATE
    "${ASIO_SDK_PATH}/common"
    "${ASIO_SDK_PATH}/host"
    "${ASIO_SDK_PATH}/host/pc"
    "${VST2_SDK_PATH}"
)

# Aviso (não bloqueia a configuração) caso alguém tente gerar isso como x64:
# plugins VST2 de 32-bit não vão carregar num host de 64-bit.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(WARNING
        "Este projeto está sendo configurado como 64-bit (x64), mas o VST2 "
        "hosting foi habilitado para carregar plugins legados de 32-bit. "
        "Um processo de 64-bit NÃO consegue carregar uma DLL de 32-bit. "
        "Gere o projeto com '-A Win32' em vez de '-A x64' para essa build.")
endif()

target_link_libraries(VSTHostApp PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_devices
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
