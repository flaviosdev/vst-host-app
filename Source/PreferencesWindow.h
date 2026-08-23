#pragma once

#include <JuceHeader.h>
#include "PluginHostEngine.h"

/**
    Aba de MIDI: lista todos os dispositivos MIDI de entrada detectados,
    cada um com um checkbox pra habilitar/desabilitar. Fica de olho
    (via Timer) em dispositivos plugados/desplugados a quente e atualiza
    a lista sozinha.
*/
class MidiSettingsComponent : public juce::Component,
                               private juce::Timer
{
public:
    explicit MidiSettingsComponent(juce::AudioDeviceManager& deviceManagerToUse);
    ~MidiSettingsComponent() override;

    void resized() override;

private:
    void rebuildDeviceListIfChanged();
    void timerCallback() override;

    juce::AudioDeviceManager& deviceManager;
    juce::Label infoLabel;
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::StringArray lastKnownDeviceIds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiSettingsComponent)
};

/**
    Aba de plugins: mantém os diretórios de busca e controla o scanner.
*/
class PluginSettingsComponent : public juce::Component
{
public:
    explicit PluginSettingsComponent(PluginHostEngine& engineToUse);
    void resized() override;

private:
    void refreshPaths();
    void addPath();
    void removePath();
    void scan(bool newOnly);

    PluginHostEngine& engine;
    juce::Label infoLabel;
    juce::ListBox pathList;
    juce::TextButton addButton { "Adicionar..." };
    juce::TextButton removeButton { "Remover" };
    juce::TextButton scanButton { "Scan" };
    juce::TextButton scanNewButton { "Scan New" };
    juce::StringArray paths;
    std::unique_ptr<juce::ListBoxModel> pathModel;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginSettingsComponent)
};

/**
    Janela de Preferências: um DocumentWindow com abas.
    Por enquanto: "Audio" e "MIDI". Novas abas (ex.: "Geral", "Plugins")
    podem ser adicionadas depois só chamando tabs.addTab(...) de novo.
*/
class PreferencesWindow : public juce::DocumentWindow
{
public:
    explicit PreferencesWindow(PluginHostEngine& engineToUse);
    ~PreferencesWindow() override;

    void closeButtonPressed() override;

    // Chamado quando o usuário fecha a janela, pra quem criou (MainComponent)
    // saber que deve soltar o unique_ptr que segura essa janela.
    std::function<void()> onCloseRequested;

private:
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreferencesWindow)
};
