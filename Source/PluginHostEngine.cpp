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
    {
        const juce::ScopedLock lock(pluginListLock);
        auto it = std::find_if(loadedPlugins.begin(), loadedPlugins.end(),
                               [pluginId](const std::unique_ptr<LoadedPlugin>& p) { return p->id == pluginId; });
        if (it == loadedPlugins.end())
            return;

        if ((*it)->route.solo)
            --activeSoloCount;

        (*it)->instance->releaseResources();
        loadedPlugins.erase(it);
    }

    listeners.call([](Listener& l) { l.pluginChanged(); });
}

void PluginHostEngine::unloadPlugins()
{
    {
        const juce::ScopedLock lock(pluginListLock);
        for (auto& loaded : loadedPlugins)
            loaded->instance->releaseResources();
        loadedPlugins.clear();
        activeSoloCount = 0;
    }

    listeners.call([](Listener& l) { l.pluginChanged(); });
}

float PluginHostEngine::getPluginVolume(int pluginId) const
{
    const juce::ScopedLock lock(pluginListLock);
    auto* loaded = findPlugin(pluginId);
    return loaded != nullptr ? loaded->volume : 1.0f;
}

void PluginHostEngine::setPluginVolume(int pluginId, float volume)
{
    const juce::ScopedLock lock(pluginListLock);
    if (auto* loaded = findPlugin(pluginId))
        loaded->volume = juce::jlimit(0.0f, 2.0f, volume);
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

void PluginHostEngine::processPlugins(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    const juce::ScopedLock lock(pluginListLock);
    buffer.clear();

    for (auto& loadedPtr : loadedPlugins)
    {
        auto& loaded = *loadedPtr;
        auto& scratch = loaded.scratchBuffer;
        scratch.clear();

        // Cada plugin recebe o MIDI já filtrado pelo estado de mute/solo dele
        // (e, no futuro, por canal). Ver MidiRouter::route().
        loaded.scratchMidi.clear();
        MidiRouter::route(loaded.route, activeSoloCount > 0, midiMessages,
                           buffer.getNumSamples(), loaded.scratchMidi);

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
