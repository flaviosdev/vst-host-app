#pragma once

#include <JuceHeader.h>
#include <functional>
#include <map>
#include <vector>
#include "PresetManager.h"

/**
    Camada de áudio/MIDI/plugin da aplicação.

    Um detalhe importante: os plugins carregados são independentes e recebem
    o mesmo MIDI. O áudio produzido por cada um é somado no final, formando
    um mixer paralelo simples.
*/
class PluginHostEngine : private juce::ChangeListener
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void pluginChanged() {}
        virtual void audioDeviceChanged() {}
        virtual void pluginsChanged() {}
    };

    struct LoadResult
    {
        bool success = false;
        juce::String errorMessage;
        juce::String pluginName;
        int pluginId = -1;
    };

    PluginHostEngine();
    ~PluginHostEngine() override;

    void addListener(Listener* listenerToAdd);
    void removeListener(Listener* listenerToRemove);

    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
    void saveAudioDeviceState();

    //== Catálogo de plugins ==================================================
    juce::StringArray getPluginSearchPaths() const;
    void addPluginSearchPath(const juce::File& directory);
    void removePluginSearchPath(const juce::File& directory);
    void scanPlugins(bool scanNewOnly);
    juce::Array<juce::PluginDescription> getKnownPlugins() const;

    // Carrega o plugin selecionado adicionando-o aos que já estão abertos.
    LoadResult loadPlugin(const juce::PluginDescription& description);
    LoadResult loadPluginFromFile(const juce::File& file);

    //== Plugins carregados ===================================================
    juce::Array<int> getLoadedPluginIds() const;
    bool hasPluginLoaded(int pluginId) const;
    bool hasPluginLoaded() const noexcept { return !loadedPlugins.empty(); }
    juce::String getPluginName(int pluginId) const;
    void unloadPlugin(int pluginId);
    void unloadPlugins();

    // Preparado para o futuro controle de volume por plugin.
    float getPluginVolume(int pluginId) const;
    void setPluginVolume(int pluginId, float volume);

    juce::AudioProcessorEditor* createPluginEditorIfNeeded(int pluginId);

    //== Programas de fábrica ================================================
    juce::StringArray getFactoryProgramNames(int pluginId) const;
    int getCurrentFactoryProgram(int pluginId) const;
    void setCurrentFactoryProgram(int pluginId, int index);

    //== Presets de usuário ===================================================
    juce::StringArray getUserPresetNames(int pluginId) const;
    bool saveUserPreset(int pluginId, const juce::String& name);
    bool loadUserPreset(int pluginId, const juce::String& name);
    bool deleteUserPreset(int pluginId, const juce::String& name);

private:
    struct LoadedPlugin
    {
        int id = -1;
        std::unique_ptr<juce::AudioPluginInstance> instance;
        PresetManager presetManager;
        juce::AudioBuffer<float> scratchBuffer;
        juce::MidiBuffer scratchMidi;
        double sampleRate = 44100.0;
        int blockSize = 512;
        float volume = 1.0f;

        
    };

    class ParallelPluginProcessor : public juce::AudioProcessor
    {
    public:
        explicit ParallelPluginProcessor(PluginHostEngine& ownerToUse);

        const juce::String getName() const override { return "Parallel Plugin Mixer"; }
        void prepareToPlay(double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return false; }
        bool isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override { return 0.0; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram(int) override {}
        const juce::String getProgramName(int) override { return {}; }
        void changeProgramName(int, const juce::String&) override {}
        void getStateInformation(juce::MemoryBlock&) override {}
        void setStateInformation(const void*, int) override {}

        bool hasEditor() const override { return false; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool silenceInProducesSilenceOut() const override { return false; }  

    private:
        PluginHostEngine& owner;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParallelPluginProcessor)
    };

    void processPlugins(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages);
    void prepareLoadedPlugin(LoadedPlugin& loaded);
    LoadedPlugin* findPlugin(int pluginId) noexcept;
    const LoadedPlugin* findPlugin(int pluginId) const noexcept;

    void connectMidiInputs();
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    juce::File getSettingsFile() const;
    juce::File getPluginPathsFile() const;
    juce::File getKnownPluginsFile() const;
    void loadPluginSettings();
    void savePluginSettings();
    std::unique_ptr<juce::XmlElement> loadAudioDeviceState() const;

    // parallelProcessor é declarado antes do AudioProcessorPlayer para que,
    // na destruição, o player seja destruído primeiro e nunca fique apontando
    // para um processor já destruído.
    ParallelPluginProcessor parallelProcessor;
    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorPlayer audioProcessorPlayer;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;
    juce::StringArray pluginSearchPaths;
    std::vector<std::unique_ptr<LoadedPlugin>> loadedPlugins;
    juce::CriticalSection pluginListLock;
    int nextPluginId = 1;

    juce::ListenerList<Listener> listeners;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginHostEngine)
};
