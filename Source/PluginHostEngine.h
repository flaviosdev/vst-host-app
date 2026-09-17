#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <map>
#include <vector>
#include "MidiActionMap.h"
#include "MidiCcMap.h"
#include "MidiRouter.h"
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

        // Chamado quando mute/solo/canal de um plugin específico muda -
        // separado de pluginChanged() pra UI poder atualizar só o botão
        // daquele plugin, sem re-renderizar a linha inteira.
        virtual void pluginRouteChanged(int /*pluginId*/) {}

        // Chamado quando o modo Exclusive liga/desliga, ou quando ativá-lo
        // já dispara a troca de solo internamente. Afeta a aparência de
        // TODAS as linhas de uma vez (o botão Solo delas passa a se
        // comportar diferente), não só uma - por isso não leva pluginId.
        virtual void exclusiveSoloChanged() {}

        // Chamado quando o modo de captura do MIDI Learn liga/desliga, ou
        // quando um binding é aprendido/removido - a UI usa isso pra
        // destacar o botão "Learn" e mostrar quais teclas já têm binding.
        virtual void midiLearnStateChanged() {}
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

    //== Teclado virtual =======================================================
    // Estado compartilhado com o juce::MidiKeyboardComponent da UI: cliques
    // no teclado desenhado na tela viram Note On/Note Off aqui, e são
    // injetados no mesmo processBlock que recebe o MIDI de hardware -
    // atravessando o MidiRouter, mute/solo/cena e o MIDI Learn normalmente,
    // porque entram no fluxo antes de qualquer uma dessas etapas.
    juce::MidiKeyboardState& getVirtualKeyboardState() noexcept { return virtualKeyboardState; }

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

    //== Medidor de áudio e atividade MIDI (para a UI) ==========================
    // Nível de pico do último bloco processado, 0.0-1.0+ (não é dB, é
    // amplitude linear - a UI decide como desenhar). Só leitura, não
    // "consome" nada, porque um medidor de VU deve refletir o estado atual
    // toda vez que é lido, não disparar uma vez e apagar.
    float getPeakLevel(int pluginId) const;

    // Diferente do de volume: "consome" o sinalizador (lê e já zera), porque
    // é usado pra um flash momentâneo ("chegou uma nota agora") em vez de
    // um estado contínuo. Se não for lido, o sinalizador acumula até a
    // próxima leitura - não perde eventos rápidos entre uma leitura e outra.
    bool consumeMidiActivity(int pluginId);

    //== Mute / Solo ===========================================================
    // "Pseudo-mute": não corta o áudio já em processamento, apenas deixa de
    // enviar MIDI pro plugin mutado (ver MidiRouter). Solo tem prioridade
    // sobre mute: um plugin em solo sempre toca, mesmo que também esteja
    // marcado como mutado.
    bool isPluginMuted(int pluginId) const;
    void setPluginMuted(int pluginId, bool shouldBeMuted);
    bool isPluginSolo(int pluginId) const;
    void setPluginSolo(int pluginId, bool shouldBeSolo);

    //== Modo Exclusive =========================================================
    // Muda o comportamento do Solo (não é um conceito à parte): com o modo
    // ligado, ativar o solo de um plugin desativa automaticamente o solo de
    // qualquer outro que estivesse ligado - só um por vez. O botão de cada
    // plugin fica idempotente enquanto o solo dele já está ligado (clicar de
    // novo não faz nada; só clicar em OUTRO solo troca). Desligar o modo
    // Exclusive mantém o último solo que estava ativo (não desliga nada).
    void setExclusiveSoloEnabled(bool shouldBeEnabled);
    bool isExclusiveSoloEnabled() const noexcept { return exclusiveSoloEnabled; }

    //== MIDI Learn =============================================================
    // Liga o modo de captura: a próxima nota (Note On) recebida de QUALQUER
    // dispositivo MIDI habilitado vira o gatilho pra essa ação, nesse plugin.
    // Enquanto ativo, essa nota específica NÃO soa (é interceptada antes de
    // chegar em qualquer plugin) - ela vira só um controle, igual um pad de
    // controlador que só serve pra ligar/desligar algo.
    void startMidiLearn(MidiTriggerAction action, int targetPluginId);
    void cancelMidiLearn();
    bool isMidiLearnActive() const noexcept { return midiActionMap.isLearning() || midiCcMap.isLearning(); }

    // true se ESTE botão específico (ação + plugin) foi quem iniciou a
    // captura em andamento - usado pra manter só o botão certo "aceso"
    // enquanto o host aguarda a tecla.
    bool isMidiLearnTarget(MidiTriggerAction action, int pluginId) const;

    // Bindings já aprendidos para um plugin (pra UI mostrar "Mute: Nota 36, Canal 10").
    juce::Array<MidiTriggerBinding> getMidiBindingsForPlugin(int pluginId) const;
    void clearMidiBinding(MidiTriggerAction action, int pluginId);

    //== MIDI Learn do volume (CC) =============================================
    // Mesmo mecanismo do MIDI Learn de ações, mas pra volume: a próxima
    // mensagem de Control Change recebida vira o binding, e a partir daí
    // esse CC empurra o volume direto, sem passar por Mute/Solo/Cena.
    void startVolumeMidiLearn(int targetPluginId);
    bool isVolumeMidiLearnTarget(int pluginId) const;

    // Descrição do binding de volume de um plugin, se houver ("CC 7 / Ch1").
    // juce::String vazia se não houver binding.
    juce::String getVolumeMidiBindingDescription(int pluginId) const;
    void clearVolumeMidiBinding(int pluginId);

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
        PluginMidiRoute route;

        // Escritos pela thread de áudio a cada bloco (ver processPlugins),
        // lidos pela UI via Timer (ver PluginHostEngine::consumeMeterState).
        // std::atomic é seguro entre threads sem lock - importante aqui,
        // porque a thread de áudio não pode esperar por um mutex sem
        // arriscar um glitch sonoro.
        std::atomic<float> peakLevel { 0.0f };
        std::atomic<bool> midiActivity { false };
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

    // Roda dentro de processPlugins(), na thread de áudio, com pluginListLock
    // já adquirido. Filtra do buffer qualquer nota que esteja sendo aprendida
    // ou que já dispara uma ação - essas notas nunca chegam nos plugins.
    // Efeitos colaterais (aprender um binding, alternar mute/solo) são
    // agendados via MessageManager::callAsync() para rodar na thread de
    // mensagens depois - nunca mexe em Listener/UI direto da thread de áudio.
    void interceptLearnableNotes(juce::MidiBuffer& midiMessages);

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
    juce::MidiKeyboardState virtualKeyboardState;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;
    juce::StringArray pluginSearchPaths;
    std::vector<std::unique_ptr<LoadedPlugin>> loadedPlugins;
    juce::CriticalSection pluginListLock;
    int nextPluginId = 1;
    int activeSoloCount = 0; // protegido por pluginListLock, igual a loadedPlugins
    bool exclusiveSoloEnabled = false; // protegido por pluginListLock, igual ao resto do estado de roteamento

    juce::ListenerList<Listener> listeners;
    MidiActionMap midiActionMap; // protegido por pluginListLock, igual ao resto do estado de roteamento
    MidiCcMap midiCcMap;         // idem, mas para o CC de volume

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginHostEngine)
};
