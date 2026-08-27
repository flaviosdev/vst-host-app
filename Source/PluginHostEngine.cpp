#include "PluginHostEngine.h"

PluginHostEngine::ParallelPluginProcessor::ParallelPluginProcessor(PluginHostEngine& ownerToUse)
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      owner(ownerToUse)
{
}

void PluginHostEngine::ParallelPluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    const juce::ScopedLock lock(owner.pluginListLock);
    for (auto& loadedPtr : owner.loadedPlugins)
    {
        auto& loaded = *loadedPtr;
        loaded.sampleRate = sampleRate;
        loaded.blockSize = samplesPerBlock;
        loaded.scratchBuffer.setSize(2, samplesPerBlock, false, false, true);
        loaded.instance->prepareToPlay(sampleRate, samplesPerBlock);
    }
}

void PluginHostEngine::ParallelPluginProcessor::releaseResources()
{
    const juce::ScopedLock lock(owner.pluginListLock);
    for (auto& loadedPtr : owner.loadedPlugins)
        loadedPtr->instance->releaseResources();
}

void PluginHostEngine::ParallelPluginProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                                               juce::MidiBuffer& midiMessages)
{
    owner.processPlugins(buffer, midiMessages);
}

void PluginHostEngine::ParallelPluginProcessor::processBlock(juce::AudioBuffer<double>& buffer,
                                                               juce::MidiBuffer& midiMessages)
{
    buffer.clear();
    juce::ignoreUnused(midiMessages);
}

//==============================================================================
PluginHostEngine::PluginHostEngine()
    : parallelProcessor(*this)
{
    formatManager.addDefaultFormats();
    loadPluginSettings();

    auto savedState = loadAudioDeviceState();
    deviceManager.initialise(0, 2, savedState.get(), true);
    deviceManager.addChangeListener(this);
    audioProcessorPlayer.setProcessor(&parallelProcessor);
    deviceManager.addAudioCallback(&audioProcessorPlayer);

    connectMidiInputs();
}

PluginHostEngine::~PluginHostEngine()
{
    deviceManager.removeChangeListener(this);
    deviceManager.removeAudioCallback(&audioProcessorPlayer);
    audioProcessorPlayer.setProcessor(nullptr);
    unloadPlugins();
    saveAudioDeviceState();
    savePluginSettings();
    deviceManager.closeAudioDevice();
}

void PluginHostEngine::addListener(Listener* listenerToAdd) { listeners.add(listenerToAdd); }
void PluginHostEngine::removeListener(Listener* listenerToRemove) { listeners.remove(listenerToRemove); }

//==============================================================================
void PluginHostEngine::connectMidiInputs()
{
    for (const auto& midiInput : juce::MidiInput::getAvailableDevices())
    {
        const bool isEnabled = deviceManager.isMidiInputDeviceEnabled(midiInput.identifier);
        deviceManager.removeMidiInputDeviceCallback(midiInput.identifier, &audioProcessorPlayer);
        if (isEnabled)
            deviceManager.addMidiInputDeviceCallback(midiInput.identifier, &audioProcessorPlayer);
    }
}

void PluginHostEngine::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &deviceManager)
    {
        connectMidiInputs();
        listeners.call([](Listener& l) { l.audioDeviceChanged(); });
    }
}

juce::File PluginHostEngine::getSettingsFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("VSTHostApp").getChildFile("AudioSettings.xml");
}

std::unique_ptr<juce::XmlElement> PluginHostEngine::loadAudioDeviceState() const
{
    auto file = getSettingsFile();
    if (!file.existsAsFile()) return nullptr;
    return juce::XmlDocument::parse(file);
}

void PluginHostEngine::saveAudioDeviceState()
{
    auto xml = deviceManager.createStateXml();
    if (xml == nullptr) return;
    auto file = getSettingsFile();
    file.getParentDirectory().createDirectory();
    xml->writeTo(file);
}

//==============================================================================
juce::File PluginHostEngine::getPluginPathsFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("VSTHostApp").getChildFile("PluginPaths.xml");
}

juce::File PluginHostEngine::getKnownPluginsFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("VSTHostApp").getChildFile("KnownPlugins.xml");
}

void PluginHostEngine::loadPluginSettings()
{
    if (auto xml = juce::XmlDocument::parse(getPluginPathsFile()))
    {
        pluginSearchPaths.clear();
        forEachXmlChildElement(*xml, child)
            if (child->hasTagName("Path"))
                pluginSearchPaths.addIfNotAlreadyThere(child->getStringAttribute("value"));
    }

    if (pluginSearchPaths.isEmpty())
    {
        for (int i = 0; i < formatManager.getNumFormats(); ++i)
        {
            auto* format = formatManager.getFormat(i);
            const auto defaults = format->getDefaultLocationsToSearch();
            for (int pathIndex = 0; pathIndex < defaults.getNumPaths(); ++pathIndex)
                pluginSearchPaths.addIfNotAlreadyThere(defaults[pathIndex].getFullPathName());
        }
    }

    if (auto xml = juce::XmlDocument::parse(getKnownPluginsFile()))
        knownPluginList.recreateFromXml(*xml);
}

void PluginHostEngine::savePluginSettings()
{
    auto directory = getPluginPathsFile().getParentDirectory();
    directory.createDirectory();

    juce::XmlElement paths("PluginPaths");
    for (const auto& path : pluginSearchPaths)
    {
        auto* child = paths.createNewChildElement("Path");
        child->setAttribute("value", path);
    }
    paths.writeTo(getPluginPathsFile());

    if (auto xml = knownPluginList.createXml())
        xml->writeTo(getKnownPluginsFile());
}

juce::StringArray PluginHostEngine::getPluginSearchPaths() const { return pluginSearchPaths; }

void PluginHostEngine::addPluginSearchPath(const juce::File& directory)
{
    if (directory.isDirectory())
    {
        pluginSearchPaths.addIfNotAlreadyThere(directory.getFullPathName());
        savePluginSettings();
    }
}

void PluginHostEngine::removePluginSearchPath(const juce::File& directory)
{
    pluginSearchPaths.removeString(directory.getFullPathName());
    savePluginSettings();
}

void PluginHostEngine::scanPlugins(bool scanNewOnly)
{
    if (!scanNewOnly)
        knownPluginList.clear();

    for (int i = 0; i < formatManager.getNumFormats(); ++i)
    {
        auto* format = formatManager.getFormat(i);
        juce::FileSearchPath searchPath;
        for (const auto& path : pluginSearchPaths)
            searchPath.add(path);

        juce::PluginDirectoryScanner scanner(knownPluginList, *format, searchPath, true,
                                             getKnownPluginsFile().getSiblingFile("DeadMansPedal.xml"));
        juce::String name;
        while (scanner.scanNextFile(scanNewOnly, name)) {}
    }

    knownPluginList.sort(juce::KnownPluginList::sortAlphabetically, true);
    savePluginSettings();
    listeners.call([](Listener& l) { l.pluginsChanged(); });
}

juce::Array<juce::PluginDescription> PluginHostEngine::getKnownPlugins() const
{
    return knownPluginList.getTypes();
}

//==============================================================================
PluginHostEngine::LoadResult PluginHostEngine::loadPlugin(const juce::PluginDescription& description)
{
    juce::String errorMessage;
    auto* device = deviceManager.getCurrentAudioDevice();
    const double sampleRate = device != nullptr ? device->getCurrentSampleRate() : 44100.0;
    const int blockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 512;

    auto newPlugin = formatManager.createPluginInstance(description, sampleRate, blockSize, errorMessage);
    if (newPlugin == nullptr)
    {
        listeners.call([](Listener& l) { l.pluginChanged(); });
        return { false, errorMessage, {}, -1 };
    }

    auto loaded = std::make_unique<LoadedPlugin>();

    loaded->id = nextPluginId++;
    loaded->instance = std::move(newPlugin);
    loaded->sampleRate = sampleRate;
    loaded->blockSize = blockSize;

    loaded->scratchBuffer.setSize(
        2,
        blockSize,
        false,
        false,
        true
    );

    loaded->instance->prepareToPlay(sampleRate, blockSize);

    const auto presetKey = juce::File::createLegalFileName(
        description.name + "_" + juce::String(description.uniqueId)
    );

    loaded->presetManager.setActivePlugin(presetKey);

    const int pluginId = loaded->id;

    {
        const juce::ScopedLock lock(pluginListLock);
        loadedPlugins.push_back(std::move(loaded));
    }

    listeners.call([](Listener& l) { l.pluginChanged(); });
    return { true, {}, description.name, pluginId };
}

PluginHostEngine::LoadResult PluginHostEngine::loadPluginFromFile(const juce::File& file)
{
    juce::OwnedArray<juce::PluginDescription> typesFound;
    for (int i = 0; i < formatManager.getNumFormats(); ++i)
    {
        auto* format = formatManager.getFormat(i);
        knownPluginList.scanAndAddFile(file.getFullPathName(), true, typesFound, *format);
    }

    if (typesFound.isEmpty())
        return { false, "Nao foi possivel identificar um plugin valido nesse arquivo.", {}, -1 };

    return loadPlugin(*typesFound.getFirst());
}

//==============================================================================
juce::Array<int> PluginHostEngine::getLoadedPluginIds() const
{
    const juce::ScopedLock lock(pluginListLock);
    juce::Array<int> ids;
    for (const auto& loaded : loadedPlugins)
        ids.add(loaded->id);
    return ids;
}

PluginHostEngine::LoadedPlugin* PluginHostEngine::findPlugin(int pluginId) noexcept
{
    for (auto& loaded : loadedPlugins)
        if (loaded->id == pluginId)
            return loaded.get();
    return nullptr;
}

const PluginHostEngine::LoadedPlugin* PluginHostEngine::findPlugin(int pluginId) const noexcept
{
    for (const auto& loaded : loadedPlugins)
        if (loaded->id == pluginId)
            return loaded.get();
    return nullptr;
}

bool PluginHostEngine::hasPluginLoaded(int pluginId) const
{
    const juce::ScopedLock lock(pluginListLock);
    return findPlugin(pluginId) != nullptr;
}

juce::String PluginHostEngine::getPluginName(int pluginId) const
{
    const juce::ScopedLock lock(pluginListLock);
    auto* loaded = findPlugin(pluginId);
    return loaded != nullptr ? loaded->instance->getName() : juce::String();
}

void PluginHostEngine::unloadPlugin(int pluginId)
{
    bool sceneWasCleared = false;
    bool pluginHadBindings = false;

    {
        const juce::ScopedLock lock(pluginListLock);
        auto it = std::find_if(loadedPlugins.begin(), loadedPlugins.end(),
                               [pluginId](const std::unique_ptr<LoadedPlugin>& p) { return p->id == pluginId; });
        if (it == loadedPlugins.end())
            return;

        if ((*it)->route.solo)
            --activeSoloCount;

        if (activeSceneId == pluginId)
        {
            activeSceneId = -1;
            sceneWasCleared = true;
        }

        (*it)->instance->releaseResources();
        loadedPlugins.erase(it);

        pluginHadBindings = !midiActionMap.getBindingsForPlugin(pluginId).isEmpty()
                          || midiCcMap.getBindingForPlugin(pluginId) != nullptr;
        midiActionMap.removeBindingsForPlugin(pluginId);
        midiCcMap.removeBindingsForPlugin(pluginId);
    }

    listeners.call([](Listener& l) { l.pluginChanged(); });

    // Notificado fora do escopo acima e depois de pluginChanged(): a cena
    // sendo desativada é um evento à parte, e a UI precisa da lista de
    // plugins já atualizada (sem o que foi removido) pra reagir direito.
    if (sceneWasCleared)
        listeners.call([](Listener& l) { l.activeSceneChanged(-1); });

    if (pluginHadBindings)
        listeners.call([](Listener& l) { l.midiLearnStateChanged(); });
}

void PluginHostEngine::unloadPlugins()
{
    {
        const juce::ScopedLock lock(pluginListLock);
        for (auto& loaded : loadedPlugins)
            loaded->instance->releaseResources();
        loadedPlugins.clear();
        activeSoloCount = 0;
        activeSceneId = -1;
        midiActionMap.clearAll();
        midiCcMap.clearAll();
    }

    listeners.call([](Listener& l) { l.pluginChanged(); });
    listeners.call([](Listener& l) { l.activeSceneChanged(-1); });
    listeners.call([](Listener& l) { l.midiLearnStateChanged(); });
}

float PluginHostEngine::getPluginVolume(int pluginId) const
{
    const juce::ScopedLock lock(pluginListLock);
    auto* loaded = findPlugin(pluginId);
    return loaded != nullptr ? loaded->volume : 1.0f;
}

void PluginHostEngine::setPluginVolume(int pluginId, float volume)
{
    {
        const juce::ScopedLock lock(pluginListLock);
        auto* loaded = findPlugin(pluginId);
        if (loaded == nullptr)
            return;

        loaded->volume = juce::jlimit(0.0f, 2.0f, volume);
    }

    // Notifica pra UI poder sincronizar o slider quando o volume muda "de
    // fora" (via CC de um controlador), não só quando o próprio slider é
    // arrastado. Reaproveita pluginRouteChanged - mesmo evento genérico
    // "algo do roteamento/controle deste plugin mudou".
    listeners.call([pluginId](Listener& l) { l.pluginRouteChanged(pluginId); });
}

//==============================================================================
bool PluginHostEngine::isPluginMuted(int pluginId) const
{
    const juce::ScopedLock lock(pluginListLock);
    auto* loaded = findPlugin(pluginId);
    return loaded != nullptr && loaded->route.muted;
}

void PluginHostEngine::setPluginMuted(int pluginId, bool shouldBeMuted)
{
    {
        const juce::ScopedLock lock(pluginListLock);
        auto* loaded = findPlugin(pluginId);
        if (loaded == nullptr || loaded->route.muted == shouldBeMuted)
            return;

        loaded->route.muted = shouldBeMuted;
    }

    listeners.call([pluginId](Listener& l) { l.pluginRouteChanged(pluginId); });
}

bool PluginHostEngine::isPluginSolo(int pluginId) const
{
    const juce::ScopedLock lock(pluginListLock);
    auto* loaded = findPlugin(pluginId);
    return loaded != nullptr && loaded->route.solo;
}

void PluginHostEngine::setPluginSolo(int pluginId, bool shouldBeSolo)
{
    {
        const juce::ScopedLock lock(pluginListLock);
        auto* loaded = findPlugin(pluginId);
        if (loaded == nullptr || loaded->route.solo == shouldBeSolo)
            return;

        loaded->route.solo = shouldBeSolo;
        activeSoloCount += shouldBeSolo ? 1 : -1;
        jassert(activeSoloCount >= 0);
    }

    listeners.call([pluginId](Listener& l) { l.pluginRouteChanged(pluginId); });
}

void PluginHostEngine::setActiveScene(int pluginId)
{
    {
        const juce::ScopedLock lock(pluginListLock);

        // -1 sempre é uma "cena" válida (significa "desativar"). Qualquer
        // outro valor precisa apontar pra um plugin que exista de verdade.
        if (pluginId != -1 && findPlugin(pluginId) == nullptr)
            return;

        if (activeSceneId == pluginId)
            return;

        activeSceneId = pluginId;
    }

    listeners.call([pluginId](Listener& l) { l.activeSceneChanged(pluginId); });
}

//==============================================================================
//  MIDI Learn
//==============================================================================
void PluginHostEngine::startMidiLearn(MidiTriggerAction action, int targetPluginId)
{
    {
        const juce::ScopedLock lock(pluginListLock);
        midiCcMap.cancelLearning(); // só uma captura por vez no host inteiro
        midiActionMap.startLearning(action, targetPluginId);
    }

    listeners.call([](Listener& l) { l.midiLearnStateChanged(); });
}

void PluginHostEngine::cancelMidiLearn()
{
    {
        const juce::ScopedLock lock(pluginListLock);
        if (!midiActionMap.isLearning() && !midiCcMap.isLearning())
            return;

        midiActionMap.cancelLearning();
        midiCcMap.cancelLearning();
    }

    listeners.call([](Listener& l) { l.midiLearnStateChanged(); });
}

bool PluginHostEngine::isMidiLearnTarget(MidiTriggerAction action, int pluginId) const
{
    const juce::ScopedLock lock(pluginListLock);
    return midiActionMap.isLearning()
        && midiActionMap.getPendingAction() == action
        && midiActionMap.getPendingPluginId() == pluginId;
}

void PluginHostEngine::startVolumeMidiLearn(int targetPluginId)
{
    {
        const juce::ScopedLock lock(pluginListLock);
        midiActionMap.cancelLearning(); // só uma captura por vez no host inteiro
        midiCcMap.startLearning(targetPluginId);
    }

    listeners.call([](Listener& l) { l.midiLearnStateChanged(); });
}

bool PluginHostEngine::isVolumeMidiLearnTarget(int pluginId) const
{
    const juce::ScopedLock lock(pluginListLock);
    return midiCcMap.isLearning() && midiCcMap.getPendingPluginId() == pluginId;
}

juce::String PluginHostEngine::getVolumeMidiBindingDescription(int pluginId) const
{
    const juce::ScopedLock lock(pluginListLock);
    if (const auto* binding = midiCcMap.getBindingForPlugin(pluginId))
        return "CC " + juce::String(binding->ccNumber) + " / Ch" + juce::String(binding->midiChannel);

    return {};
}

void PluginHostEngine::clearVolumeMidiBinding(int pluginId)
{
    {
        const juce::ScopedLock lock(pluginListLock);
        midiCcMap.removeBindingsForPlugin(pluginId);
    }

    listeners.call([](Listener& l) { l.midiLearnStateChanged(); });
}

juce::Array<MidiTriggerBinding> PluginHostEngine::getMidiBindingsForPlugin(int pluginId) const
{
    const juce::ScopedLock lock(pluginListLock);
    return midiActionMap.getBindingsForPlugin(pluginId);
}

void PluginHostEngine::clearMidiBinding(MidiTriggerAction action, int pluginId)
{
    {
        const juce::ScopedLock lock(pluginListLock);
        midiActionMap.removeBindingForAction(action, pluginId);
    }

    listeners.call([](Listener& l) { l.midiLearnStateChanged(); });
}

juce::AudioProcessorEditor* PluginHostEngine::createPluginEditorIfNeeded(int pluginId)
{
    const juce::ScopedLock lock(pluginListLock);
    auto* loaded = findPlugin(pluginId);
    return loaded != nullptr ? loaded->instance->createEditorIfNeeded() : nullptr;
}

//==============================================================================
void PluginHostEngine::prepareLoadedPlugin(LoadedPlugin& loaded)
{
    auto* device = deviceManager.getCurrentAudioDevice();
    loaded.sampleRate = device != nullptr ? device->getCurrentSampleRate() : 44100.0;
    loaded.blockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 512;
    loaded.scratchBuffer.setSize(2, loaded.blockSize, false, false, true);
    loaded.instance->prepareToPlay(loaded.sampleRate, loaded.blockSize);
}

void PluginHostEngine::interceptLearnableNotes(juce::MidiBuffer& midiMessages)
{
    // Fora da thread de áudio isso seria só "if (bindings vazio) return",
    // mas aqui rodamos a cada bloco - então esse atalho importa de verdade.
    const bool anyActionActivity = midiActionMap.isLearning() || midiActionMap.hasAnyBindings();
    const bool anyCcActivity = midiCcMap.isLearning() || midiCcMap.hasAnyBindings();

    if (!anyActionActivity && !anyCcActivity)
        return;

    juce::MidiBuffer filtered;

    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();

        if (message.isController())
        {
            const int ccNumber = message.getControllerNumber();
            const int ccValue = message.getControllerValue(); // 0-127
            const int channel = message.getChannel();

            // Modo de captura de volume: o primeiro CC recebido vira o
            // binding e nunca chega em nenhum plugin - a partir daqui só
            // controla volume.
            if (midiCcMap.isLearning())
            {
                if (midiCcMap.learnFrom(ccNumber, channel))
                {
                    juce::MessageManager::callAsync([this]
                    {
                        listeners.call([](Listener& l) { l.midiLearnStateChanged(); });
                    });
                    continue; // consumido - não passa pro filtered
                }
            }

            // CC já vinculado a um plugin: nunca chega no plugin como CC
            // "de verdade" (evita que o plugin também reaja a ele por conta
            // própria); em vez disso empurra o volume direto. Escala 0-127
            // pra 0.0-2.0 sem distinguir se veio de um fader ou de um botão
            // que só manda 0/127 - de propósito, pra manter isso simples.
            if (const auto* binding = midiCcMap.findBinding(ccNumber, channel))
            {
                const auto pluginId = binding->targetPluginId;
                const float volume = (ccValue / 127.0f) * 2.0f;

                juce::MessageManager::callAsync([this, pluginId, volume]
                {
                    setPluginVolume(pluginId, volume);
                });

                continue; // consumido - não passa pro filtered
            }
        }

        if (message.isNoteOn() || message.isNoteOff())
        {
            const int noteNumber = message.getNoteNumber();
            const int channel = message.getChannel();

            // Modo de captura: a primeira nota pressionada vira o binding
            // e nunca chega em nenhum plugin - é só um controle a partir daqui.
            if (midiActionMap.isLearning() && message.isNoteOn())
            {
                if (midiActionMap.learnFrom(noteNumber, channel))
                {
                    juce::MessageManager::callAsync([this]
                    {
                        listeners.call([](Listener& l) { l.midiLearnStateChanged(); });
                    });
                    continue; // consumida - não passa pro filtered
                }
            }

            // Tecla já vinculada a uma ação: nunca soa (nem no Note On, nem
            // no Note Off), e o Note On dispara a ação correspondente.
            if (const auto* binding = midiActionMap.findBinding(noteNumber, channel))
            {
                if (message.isNoteOn())
                {
                    const auto action = binding->action;
                    const auto pluginId = binding->targetPluginId;

                    juce::MessageManager::callAsync([this, action, pluginId]
                    {
                        switch (action)
                        {
                            case MidiTriggerAction::toggleMute:
                                setPluginMuted(pluginId, !isPluginMuted(pluginId));
                                break;

                            case MidiTriggerAction::toggleSolo:
                                setPluginSolo(pluginId, !isPluginSolo(pluginId));
                                break;

                            case MidiTriggerAction::activateScene:
                                // Sempre dispara - nunca "toggle" no sentido de
                                // mute/solo. Apertar de novo o mesmo pad desliga
                                // a cena (mesmo gesto do clique do mouse); apertar
                                // outro pad de cena troca.
                                setActiveScene(getActiveScene() == pluginId ? -1 : pluginId);
                                break;
                        }
                    });
                }

                continue; // consumida - não passa pro filtered
            }
        }

        filtered.addEvent(message, metadata.samplePosition);
    }

    midiMessages.swapWith(filtered);
}

void PluginHostEngine::processPlugins(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    const juce::ScopedLock lock(pluginListLock);
    buffer.clear();

    interceptLearnableNotes(midiMessages);

    for (auto& loadedPtr : loadedPlugins)
    {
        auto& loaded = *loadedPtr;
        auto& scratch = loaded.scratchBuffer;
        scratch.clear();

        // Cada plugin recebe o MIDI já filtrado pelo estado de mute/solo/cena
        // dele (e, no futuro, por canal). Ver MidiRouter::route().
        loaded.scratchMidi.clear();
        MidiRouter::route(loaded.route, loaded.id, activeSceneId, activeSoloCount > 0,
                           midiMessages, buffer.getNumSamples(), loaded.scratchMidi);

        loaded.instance->processBlock(scratch, loaded.scratchMidi);
        scratch.applyGain(loaded.volume);

        const int channelsToMix = juce::jmin(buffer.getNumChannels(), scratch.getNumChannels());
        for (int channel = 0; channel < channelsToMix; ++channel)
            buffer.addFrom(channel, 0, scratch, channel, 0, buffer.getNumSamples());
    }
}

//==============================================================================
juce::StringArray PluginHostEngine::getFactoryProgramNames(int pluginId) const
{
    const juce::ScopedLock lock(pluginListLock);
    juce::StringArray names;
    auto* loaded = findPlugin(pluginId);
    if (loaded == nullptr) return names;

    for (int i = 0; i < loaded->instance->getNumPrograms(); ++i)
    {
        auto name = loaded->instance->getProgramName(i);
        names.add(name.isNotEmpty() ? name : ("Programa " + juce::String(i + 1)));
    }
    return names;
}

int PluginHostEngine::getCurrentFactoryProgram(int pluginId) const
{
    const juce::ScopedLock lock(pluginListLock);
    auto* loaded = findPlugin(pluginId);
    return loaded != nullptr ? loaded->instance->getCurrentProgram() : -1;
}

void PluginHostEngine::setCurrentFactoryProgram(int pluginId, int index)
{
    const juce::ScopedLock lock(pluginListLock);
    auto* loaded = findPlugin(pluginId);
    if (loaded != nullptr && index >= 0)
        loaded->instance->setCurrentProgram(index);
}

juce::StringArray PluginHostEngine::getUserPresetNames(int pluginId) const
{
    const juce::ScopedLock lock(pluginListLock);
    auto* loaded = findPlugin(pluginId);
    return loaded != nullptr ? loaded->presetManager.listPresets() : juce::StringArray();
}

bool PluginHostEngine::saveUserPreset(int pluginId, const juce::String& name)
{
    const juce::ScopedLock lock(pluginListLock);
    auto* loaded = findPlugin(pluginId);
    return loaded != nullptr && loaded->presetManager.savePreset(*loaded->instance, name);
}

bool PluginHostEngine::loadUserPreset(int pluginId, const juce::String& name)
{
    const juce::ScopedLock lock(pluginListLock);
    auto* loaded = findPlugin(pluginId);
    return loaded != nullptr && loaded->presetManager.loadPreset(*loaded->instance, name);
}

bool PluginHostEngine::deleteUserPreset(int pluginId, const juce::String& name)
{
    const juce::ScopedLock lock(pluginListLock);
    auto* loaded = findPlugin(pluginId);
    return loaded != nullptr && loaded->presetManager.deletePreset(name);
}
