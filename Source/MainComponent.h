#pragma once

#include <JuceHeader.h>
#include "PluginHostEngine.h"
#include "PreferencesWindow.h"

/**
    MainComponent é a View principal: só cuida de widgets e de reagir a
    notificações do PluginHostEngine. Não conhece AudioDeviceManager,
    AudioPluginFormatManager, nem nenhum detalhe de como o áudio/MIDI/plugin
    funciona por baixo dos panos - isso tudo vive na Engine.
*/
class MainComponent : public juce::Component,
                       private PluginHostEngine::Listener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    //== PluginHostEngine::Listener ============================================
    void pluginChanged() override;
    void audioDeviceChanged() override;

    //== Ações da UI (cada uma só delega pro Engine e atualiza widgets) ========
    void showPreferences();
    void loadPlugin();
    void openPluginEditor();
    void closePluginEditorWindow();

    void refreshFactoryProgramBox();
    void refreshUserPresetBox();
    void saveCurrentAsPreset();
    void loadSelectedPreset();
    void deleteSelectedPreset();

    void setStatus(const juce::String& message);

    //== Model (Engine) =========================================================
    PluginHostEngine engine;

    //== Estado que pertence só à UI ===========================================
    std::unique_ptr<PreferencesWindow> preferencesWindow;
    std::unique_ptr<juce::AudioProcessorEditor> pluginEditorComponent;
    std::unique_ptr<juce::DocumentWindow> pluginEditorWindow;

    //== Widgets ================================================================
    juce::TextButton audioSettingsButton   { "Preferencias..." };
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
