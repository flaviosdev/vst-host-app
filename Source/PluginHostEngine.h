#pragma once

#include <JuceHeader.h>
#include "PresetManager.h"

/**
    PluginHostEngine é a camada de "modelo" da aplicação: tudo que é lógica
    de áudio/MIDI/plugin, sem nenhuma dependência de componentes de UI.

    A View (MainComponent) e outras janelas (ex.: PreferencesWindow) só
    conversam com essa classe através da API pública abaixo, ou reagem a
    notificações via Listener. Isso mantém a UI "burra" (só desenha e
    delega) e o motor de áudio testável/reutilizável independente da UI -
    inclusive fora deste app (ex.: um futuro wrapper VST3 que hospeda VST2
    poderia reaproveitar boa parte dessa mesma lógica).

    OBS. sobre extensibilidade futura: hoje esta classe hospeda exatamente
    um plugin por vez (um único slot). Quando for necessário rodar mais de
    um plugin simultaneamente ou trocar de plugin em tempo real, a mudança
    fica isolada aqui dentro (por exemplo, trocando o std::unique_ptr por
    uma lista de "slots" ou por um juce::AudioProcessorGraph) - a View não
    precisa saber como isso é feito por baixo dos panos, só chama a mesma
    API pública (ou uma API pública estendida) e reage às notificações.
*/
class PluginHostEngine : private juce::ChangeListener
{
public:
    /** Interface para quem quiser ser avisado de mudanças no estado do motor. */
    struct Listener
    {
        virtual ~Listener() = default;

        // Chamado sempre que um plugin é carregado ou descarregado (inclusive
        // em caso de falha ao carregar, para a UI poder voltar ao estado "vazio").
        virtual void pluginChanged() {}

        // Chamado quando a configuração de áudio/MIDI muda (ex.: usuário
        // trocou o driver ASIO ou habilitou um dispositivo MIDI).
        virtual void audioDeviceChanged() {}
        virtual void pluginsChanged() {}
    };

    PluginHostEngine();
    ~PluginHostEngine() override;

    void addListener(Listener* listenerToAdd);
    void removeListener(Listener* listenerToRemove);

    //== Áudio / MIDI ==========================================================
    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }

    // Persiste a configuração atual de áudio/MIDI em disco imediatamente.
    // Também é chamado automaticamente no destrutor.
    void saveAudioDeviceState();

    //== Ciclo de vida do plugin ===============================================
    struct LoadResult
    {
        bool success = false;
        juce::String errorMessage;
        juce::String pluginName;
    };

    // Varre o arquivo/bundle informado (.vst3 ou .dll), instancia o primeiro
    // plugin encontrado e o conecta ao áudio/MIDI. Substitui o plugin
    // atualmente carregado, se houver.
    LoadResult loadPluginFromFile(const juce::File& file);

    //== Plugins ===============================================================
    juce::StringArray getPluginSearchPaths() const;
    void addPluginSearchPath(const juce::File& directory);
    void removePluginSearchPath(const juce::File& directory);
    void scanPlugins(bool scanNewOnly);
    juce::Array<juce::PluginDescription> getKnownPlugins() const;
    LoadResult loadPlugin(const juce::PluginDescription& description);

    void unloadPlugin();
    bool hasPluginLoaded() const noexcept { return plugin != nullptr; }
    juce::String getPluginName() const;

    // Ponteiro cru intencional: o AudioPluginInstance é dono de si mesmo
    // dentro do Engine; a View nunca deve assumir posse dele.
    juce::AudioPluginInstance* getProcessor() noexcept { return plugin.get(); }

    // Cria (ou retorna) a interface gráfica própria do plugin, se houver.
    // A posse do editor retornado passa a ser de quem chamou (a View),
    // igual ao contrato normal do JUCE para AudioProcessorEditor.
    juce::AudioProcessorEditor* createPluginEditorIfNeeded();

    //== Programas de fábrica (embutidos no próprio plugin) ====================
    juce::StringArray getFactoryProgramNames() const;
    int getCurrentFactoryProgram() const;
    void setCurrentFactoryProgram(int index);

    //== Presets de usuário (delega para o PresetManager) ======================
    juce::StringArray getUserPresetNames() const;
    bool saveUserPreset(const juce::String& name);
    bool loadUserPreset(const juce::String& name);
    bool deleteUserPreset(const juce::String& name);

private:
    void connectMidiInputs();
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    juce::File getSettingsFile() const;
    juce::File getPluginPathsFile() const;
    juce::File getKnownPluginsFile() const;
    void loadPluginSettings();
    void savePluginSettings();
    std::unique_ptr<juce::XmlElement> loadAudioDeviceState() const;

    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorPlayer audioProcessorPlayer;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;
    juce::StringArray pluginSearchPaths;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    PresetManager presetManager;

    juce::ListenerList<Listener> listeners;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginHostEngine)
};
