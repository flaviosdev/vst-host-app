#include "MidiCcMap.h"

void MidiCcMap::startLearning(int targetPluginId)
{
    learning = true;
    pendingPluginId = targetPluginId;
}

void MidiCcMap::cancelLearning()
{
    learning = false;
    pendingPluginId = -1;
}

bool MidiCcMap::learnFrom(int ccNumber, int midiChannel)
{
    if (!learning)
        return false;

    // Um plugin só tem UM CC de volume por vez - remove qualquer binding
    // antigo desse plugin antes de adicionar o novo (e também qualquer
    // binding antigo que já usasse esse mesmo CC+canal, igual ao MidiActionMap).
    bindings.erase(std::remove_if(bindings.begin(), bindings.end(),
                                   [&](const MidiCcVolumeBinding& b)
                                   {
                                       return b.targetPluginId == pendingPluginId
                                           || (b.ccNumber == ccNumber && b.midiChannel == midiChannel);
                                   }),
                   bindings.end());

    bindings.push_back({ ccNumber, midiChannel, pendingPluginId });

    learning = false;
    pendingPluginId = -1;

    return true;
}

const MidiCcVolumeBinding* MidiCcMap::findBinding(int ccNumber, int midiChannel) const
{
    for (const auto& binding : bindings)
        if (binding.ccNumber == ccNumber && binding.midiChannel == midiChannel)
            return &binding;

    return nullptr;
}

void MidiCcMap::removeBindingsForPlugin(int pluginId)
{
    bindings.erase(std::remove_if(bindings.begin(), bindings.end(),
                                   [pluginId](const MidiCcVolumeBinding& b)
                                   {
                                       return b.targetPluginId == pluginId;
                                   }),
                   bindings.end());
}

const MidiCcVolumeBinding* MidiCcMap::getBindingForPlugin(int pluginId) const
{
    for (const auto& binding : bindings)
        if (binding.targetPluginId == pluginId)
            return &binding;

    return nullptr;
}
