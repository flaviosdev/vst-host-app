#pragma once

#include <JuceHeader.h>
#include <functional>
#include <map>
#include <vector>
#include "PluginHostEngine.h"
#include "PreferencesWindow.h"

class MainComponent : public juce::Component,
                       private PluginHostEngine::Listener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class PluginRowComponent;

    //== PluginHostEngine::Listener ============================================
    void pluginChanged() override;
    void audioDeviceChanged() override;
    void pluginsChanged() override;
    void pluginRouteChanged(int pluginId) override;
    void activeSceneChanged(int pluginId) override;
    void midiLearnStateChanged() override;

    //== UI ====================================================================
    void showPreferences();
    void refreshPluginList();
    void refreshLoadedPlugins();
    void loadSelectedPlugin();
    void openPluginEditor(int pluginId);
    void closePluginEditor(int pluginId);
    void savePresetForPlugin(int pluginId);
    void setStatus(const juce::String& message);

    // Chamado pelo botão Mute/Solo de QUALQUER linha ao ser clicado, pra
    // saber se esse clique deve ser interpretado como "escolher este
    // controle pro MIDI Learn global" em vez de mutar/solar de verdade.
    // Retorna true se consumiu o clique (e já iniciou a captura na Engine).
    bool tryStartLearnFromGlobalArm(MidiTriggerAction action, int pluginId);
    bool tryStartVolumeLearnFromGlobalArm(int pluginId);

    //== Model =================================================================
    PluginHostEngine engine;

    //== Estado da UI ==========================================================
    std::unique_ptr<PreferencesWindow> preferencesWindow;
    std::map<int, std::unique_ptr<juce::AudioProcessorEditor>> pluginEditors;
    std::map<int, std::unique_ptr<juce::DocumentWindow>> pluginEditorWindows;
    std::vector<std::unique_ptr<PluginRowComponent>> pluginRows;

    // true entre clicar no botão global "Learn" e escolher um controle
    // (Mute/Solo de algum plugin) clicando nele - estado puramente de UI,
    // não vive na Engine porque ela só sabe lidar com "aguardar uma tecla
    // para esta ação+plugin específicos" (ver PluginHostEngine::startMidiLearn).
    bool globalLearnArmed = false;

    //== Widgets ================================================================
    juce::TextButton audioSettingsButton { "Preferencias..." };
    juce::ListBox pluginList;
    std::unique_ptr<juce::ListBoxModel> pluginListModel;
    juce::TextButton loadPluginButton { "Carregar Plugin Selecionado" };
    juce::TextButton globalLearnButton { "Learn" };
    juce::Label statusLabel;
    juce::Viewport loadedPluginsViewport;
    juce::Component loadedPluginsContainer;
    juce::MidiKeyboardComponent virtualKeyboard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
