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

    //== UI ====================================================================
    void showPreferences();
    void refreshPluginList();
    void refreshLoadedPlugins();
    void loadSelectedPlugin();
    void openPluginEditor(int pluginId);
    void closePluginEditor(int pluginId);
    void savePresetForPlugin(int pluginId);
    void setStatus(const juce::String& message);

    //== Model =================================================================
    PluginHostEngine engine;

    //== Estado da UI ==========================================================
    std::unique_ptr<PreferencesWindow> preferencesWindow;
    std::map<int, std::unique_ptr<juce::AudioProcessorEditor>> pluginEditors;
    std::map<int, std::unique_ptr<juce::DocumentWindow>> pluginEditorWindows;
    std::vector<std::unique_ptr<PluginRowComponent>> pluginRows;

    //== Widgets ================================================================
    juce::TextButton audioSettingsButton { "Preferencias..." };
    juce::ListBox pluginList;
    std::unique_ptr<juce::ListBoxModel> pluginListModel;
    juce::TextButton loadPluginButton { "Carregar Plugin Selecionado" };
    juce::Label statusLabel;
    juce::Viewport loadedPluginsViewport;
    juce::Component loadedPluginsContainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
