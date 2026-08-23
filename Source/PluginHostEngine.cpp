#include "PluginHostEngine.h"

//==============================================================================
PluginHostEngine::PluginHostEngine()
{
    formatManager.addDefaultFormats();

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
