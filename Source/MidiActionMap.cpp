#include "MidiActionMap.h"

void MidiActionMap::startLearning(MidiTriggerAction action, int targetPluginId)
{
    learning = true;
    pendingAction = action;
    pendingPluginId = targetPluginId;
}

void MidiActionMap::cancelLearning()
{
    learning = false;
    pendingPluginId = -1;
}

bool MidiActionMap::learnFrom(int noteNumber, int midiChannel)
{
    if (!learning)
        return false;

    // Uma tecla só pode ter um binding: remove qualquer um antigo que já
    // usasse essa mesma combinação de nota+canal antes de adicionar o novo.
    removeBinding(noteNumber, midiChannel);

    bindings.push_back({ noteNumber, midiChannel, pendingAction, pendingPluginId });

    learning = false;
    pendingPluginId = -1;

    return true;
}

const MidiTriggerBinding* MidiActionMap::findBinding(int noteNumber, int midiChannel) const
{
    for (const auto& binding : bindings)
        if (binding.noteNumber == noteNumber && binding.midiChannel == midiChannel)
            return &binding;

    return nullptr;
}

void MidiActionMap::removeBinding(int noteNumber, int midiChannel)
{
    bindings.erase(std::remove_if(bindings.begin(), bindings.end(),
                                   [&](const MidiTriggerBinding& b)
                                   {
                                       return b.noteNumber == noteNumber && b.midiChannel == midiChannel;
                                   }),
                   bindings.end());
}

void MidiActionMap::removeBindingsForPlugin(int pluginId)
{
    bindings.erase(std::remove_if(bindings.begin(), bindings.end(),
                                   [pluginId](const MidiTriggerBinding& b)
                                   {
                                       return b.targetPluginId == pluginId;
                                   }),
                   bindings.end());
}

void MidiActionMap::removeBindingForAction(MidiTriggerAction action, int pluginId)
{
    bindings.erase(std::remove_if(bindings.begin(), bindings.end(),
                                   [action, pluginId](const MidiTriggerBinding& b)
                                   {
                                       return b.action == action && b.targetPluginId == pluginId;
                                   }),
                   bindings.end());
}

juce::Array<MidiTriggerBinding> MidiActionMap::getBindingsForPlugin(int pluginId) const
{
    juce::Array<MidiTriggerBinding> result;

    for (const auto& binding : bindings)
        if (binding.targetPluginId == pluginId)
            result.add(binding);

    return result;
}
