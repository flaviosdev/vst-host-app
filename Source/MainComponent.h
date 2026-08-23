#pragma once

#include <JuceHeader.h>
#include "PresetManager.h"

/**
    Janela principal:
      - Botão para abrir as configurações de Áudio/MIDI (ASIO + dispositivos MIDI)
      - Botão para carregar um plugin VST3
      - Botão para abrir a interface gráfica do plugin
      - Painel de presets (programas de fábrica + presets salvos pelo usuário)
*/
class MainComponent : public juce::Component,
                       private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    //== Áudio / MIDI ==========================================================
    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorPlayer audioProcessorPlayer;

    void showAudioSettings();
    void connectMidiInputs();
    // Chamado pelo AudioDeviceManager quando a configuração muda
    // (ex.: usuário habilitou/desabilitou um dispositivo MIDI)
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    //== Plugin VST3 ===========================================================
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    std::unique_ptr<juce::AudioProcessorEditor> pluginEditorComponent;
    std::unique_ptr<juce::DocumentWindow> pluginEditorWindow;

    void loadPlugin();
    void unloadPlugin();
    void openPluginEditor();
    void closePluginEditorWindow();

    //== Presets ===============================================================
    PresetManager presetManager;

    void refreshFactoryProgramBox();
    void refreshUserPresetBox();
    void saveCurrentAsPreset();
    void loadSelectedPreset();
    void deleteSelectedPreset();

    //== UI ====================================================================
    juce::TextButton audioSettingsButton   { "Configuracoes de Audio/MIDI..." };
    juce::TextButton loadPluginButton      { "Carregar Plugin (VST2/VST3)..." };
    juce::TextButton showEditorButton      { "Abrir Interface do Plugin" };
    juce::Label      pluginNameLabel;
    juce::Label      statusLabel;

    juce::Label      factoryProgramLabel   { {}, "Programas de fabrica:" };
    juce::ComboBox   factoryProgramBox;

    juce::Label      userPresetLabel       { {}, "Presets salvos:" };
    juce::ComboBox   userPresetBox;
    juce::TextButton savePresetButton      { "Salvar como..." };
    juce::TextButton loadPresetButton      { "Carregar" };
    juce::TextButton deletePresetButton    { "Excluir" };

    void setStatus(const juce::String& message);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
