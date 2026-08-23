#include "PluginHostEngine.h"

//==============================================================================
PluginHostEngine::PluginHostEngine()
{
    formatManager.addDefaultFormats();
    loadPluginSettings();

    auto savedState = loadAudioDeviceState();
    deviceManager.initialise(0, 2, savedState.get(), true);
    deviceManager.addChangeListener(this);
    deviceManager.addAudioCallback(&audioProcessorPlayer);

    connectMidiInputs();
}

PluginHostEngine::~PluginHostEngine()
{
    deviceManager.removeChangeListener(this);
    unloadPlugin();
    saveAudioDeviceState();
    savePluginSettings();
    deviceManager.removeAudioCallback(&audioProcessorPlayer);
    deviceManager.closeAudioDevice();
}

void PluginHostEngine::addListener(Listener* listenerToAdd)
{
    listeners.add(listenerToAdd);
}

void PluginHostEngine::removeListener(Listener* listenerToRemove)
{
    listeners.remove(listenerToRemove);
}

//==============================================================================
//  Áudio / MIDI
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
               .getChildFile("VSTHostApp")
               .getChildFile("AudioSettings.xml");
}

std::unique_ptr<juce::XmlElement> PluginHostEngine::loadAudioDeviceState() const
{
    auto file = getSettingsFile();

    if (!file.existsAsFile())
        return nullptr;

    return juce::XmlDocument::parse(file);
}

void PluginHostEngine::saveAudioDeviceState()
{
    auto xml = deviceManager.createStateXml();
    if (xml == nullptr)
        return;

    auto file = getSettingsFile();
    file.getParentDirectory().createDirectory();
    xml->writeTo(file);
}


//==============================================================================
//  Plugins
//==============================================================================
juce::File PluginHostEngine::getPluginPathsFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("VSTHostApp")
               .getChildFile("PluginPaths.xml");
}

juce::File PluginHostEngine::getKnownPluginsFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("VSTHostApp")
               .getChildFile("KnownPlugins.xml");
}

void PluginHostEngine::loadPluginSettings()
{
    if (auto xml = juce::XmlDocument::parse(getPluginPathsFile()))
    {
        // Paths are kept in a small XML file; defaults are used only when the
        // user has never configured the list.
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

juce::StringArray PluginHostEngine::getPluginSearchPaths() const
{
    return pluginSearchPaths;
}

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

PluginHostEngine::LoadResult PluginHostEngine::loadPlugin(const juce::PluginDescription& description)
{
    unloadPlugin();

    juce::String errorMessage;
    auto* device = deviceManager.getCurrentAudioDevice();
    const double sampleRate = device != nullptr ? device->getCurrentSampleRate() : 44100.0;
    const int blockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 512;

    auto newPlugin = formatManager.createPluginInstance(description, sampleRate, blockSize, errorMessage);
    if (newPlugin == nullptr)
    {
        listeners.call([](Listener& l) { l.pluginChanged(); });
        return { false, errorMessage, {} };
    }

    newPlugin->prepareToPlay(sampleRate, blockSize);
    plugin = std::move(newPlugin);
    audioProcessorPlayer.setProcessor(plugin.get());

    const auto presetKey = juce::File::createLegalFileName(
        description.name + "_" + juce::String(description.uniqueId));
    presetManager.setActivePlugin(presetKey);

    listeners.call([](Listener& l) { l.pluginChanged(); });
    return { true, {}, description.name };
}

//==============================================================================
//  Ciclo de vida do plugin
//==============================================================================
PluginHostEngine::LoadResult PluginHostEngine::loadPluginFromFile(const juce::File& file)
{
    unloadPlugin();

    juce::OwnedArray<juce::PluginDescription> typesFound;

    // Varre o arquivo/bundle selecionado em busca dos formatos registrados
    // (VST3, e VST2 quando habilitado no CMake). Um único arquivo pode
    // conter mais de um plugin.
    for (int i = 0; i < formatManager.getNumFormats(); ++i)
    {
        auto* format = formatManager.getFormat(i);
        knownPluginList.scanAndAddFile(file.getFullPathName(), true, typesFound, *format);
    }

    if (typesFound.isEmpty())
        return { false, "Nao foi possivel identificar um plugin valido nesse arquivo.", {} };

    const auto description = *typesFound.getFirst();

    juce::String errorMessage;
    auto* device = deviceManager.getCurrentAudioDevice();
    const double sampleRate = device != nullptr ? device->getCurrentSampleRate() : 44100.0;
    const int blockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 512;

    auto newPlugin = formatManager.createPluginInstance(description, sampleRate, blockSize, errorMessage);

    if (newPlugin == nullptr)
    {
        listeners.call([](Listener& l) { l.pluginChanged(); });
        return { false, errorMessage, {} };
    }

    newPlugin->prepareToPlay(sampleRate, blockSize);

    plugin = std::move(newPlugin);
    audioProcessorPlayer.setProcessor(plugin.get());

    // Chave estável por plugin (nome + id único do formato), em vez de só o
    // nome de exibição - evita colisão entre dois plugins diferentes que por
    // coincidência tenham o mesmo nome.
    const auto presetKey = juce::File::createLegalFileName(
        description.name + "_" + juce::String(description.uniqueId));
    presetManager.setActivePlugin(presetKey);

    listeners.call([](Listener& l) { l.pluginChanged(); });

    return { true, {}, description.name };
}

void PluginHostEngine::unloadPlugin()
{
    audioProcessorPlayer.setProcessor(nullptr);

    if (plugin != nullptr)
    {
        plugin->releaseResources();
        plugin.reset();
        listeners.call([](Listener& l) { l.pluginChanged(); });
    }
}

juce::String PluginHostEngine::getPluginName() const
{
    return plugin != nullptr ? plugin->getName() : juce::String();
}

juce::AudioProcessorEditor* PluginHostEngine::createPluginEditorIfNeeded()
{
    return plugin != nullptr ? plugin->createEditorIfNeeded() : nullptr;
}

//==============================================================================
//  Programas de fábrica
//==============================================================================
juce::StringArray PluginHostEngine::getFactoryProgramNames() const
{
    juce::StringArray names;

    if (plugin == nullptr)
        return names;

    for (int i = 0; i < plugin->getNumPrograms(); ++i)
    {
        auto name = plugin->getProgramName(i);
        names.add(name.isNotEmpty() ? name : ("Programa " + juce::String(i + 1)));
    }

    return names;
}

int PluginHostEngine::getCurrentFactoryProgram() const
{
    return plugin != nullptr ? plugin->getCurrentProgram() : -1;
}

void PluginHostEngine::setCurrentFactoryProgram(int index)
{
    if (plugin != nullptr && index >= 0)
        plugin->setCurrentProgram(index);
}

//==============================================================================
//  Presets de usuário
//==============================================================================
juce::StringArray PluginHostEngine::getUserPresetNames() const
{
    return presetManager.listPresets();
}

bool PluginHostEngine::saveUserPreset(const juce::String& name)
{
    return plugin != nullptr && presetManager.savePreset(*plugin, name);
}

bool PluginHostEngine::loadUserPreset(const juce::String& name)
{
    return plugin != nullptr && presetManager.loadPreset(*plugin, name);
}

bool PluginHostEngine::deleteUserPreset(const juce::String& name)
{
    return presetManager.deletePreset(name);
}
