#include "PresetManager.h"

void PresetManager::setActivePlugin(const juce::String& pluginName)
{
    activePluginName = pluginName;
    getPresetFolder().createDirectory();
}

juce::File PresetManager::getPresetFolder() const
{
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                    .getChildFile("VSTHostApp")
                    .getChildFile("Presets")
                    .getChildFile(activePluginName.isNotEmpty() ? activePluginName : "SemPlugin");
    return base;
}

juce::File PresetManager::getPresetFile(const juce::String& presetName) const
{
    return getPresetFolder().getChildFile(presetName + ".preset");
}

bool PresetManager::savePreset(juce::AudioProcessor& processor, const juce::String& presetName)
{
    if (presetName.isEmpty())
        return false;

    getPresetFolder().createDirectory();

    juce::MemoryBlock state;
    processor.getStateInformation(state);

    auto file = getPresetFile(presetName);
    return file.replaceWithData(state.getData(), state.getSize());
}

bool PresetManager::loadPreset(juce::AudioProcessor& processor, const juce::String& presetName)
{
    auto file = getPresetFile(presetName);

    if (!file.existsAsFile())
        return false;

    juce::MemoryBlock state;
    if (!file.loadFileAsData(state))
        return false;

    processor.setStateInformation(state.getData(), (int) state.getSize());
    return true;
}

bool PresetManager::deletePreset(const juce::String& presetName)
{
    auto file = getPresetFile(presetName);
    return file.existsAsFile() && file.deleteFile();
}

juce::StringArray PresetManager::listPresets() const
{
    juce::StringArray names;
    auto folder = getPresetFolder();

    if (!folder.isDirectory())
        return names;

    for (const auto& entry : juce::RangedDirectoryIterator(folder, false, "*.preset"))
        names.add(entry.getFile().getFileNameWithoutExtension());

    names.sort(true);
    return names;
}
