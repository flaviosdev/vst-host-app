#pragma once

#include <JuceHeader.h>

/**
    Gerencia presets "de usuário" para o plugin atualmente carregado.

    Presets são salvos como arquivos .preset (estado binário do plugin,
    via AudioProcessor::getStateInformation) dentro de uma pasta própria
    por plugin, em:

        %APPDATA%/VSTHostApp/Presets/<chave do plugin>/

    A "chave do plugin" é um identificador estável (nome + id único do
    formato), definido por quem chama setActivePlugin() - hoje o
    PluginHostEngine. Usar um identificador estável em vez do nome de
    exibição puro evita colisão entre dois plugins diferentes que, por
    coincidência, tenham o mesmo nome.

    Isso é independente dos "programas de fábrica" que o próprio plugin
    já traz (acessados via AudioProcessor::getNumPrograms/setCurrentProgram),
    que também são expostos pelo PluginHostEngine.
*/
class PresetManager
{
public:
    PresetManager() = default;

    // Define para qual plugin (por uma chave estável, ex.: "Nome_uniqueId")
    // os presets serão gerenciados.
    void setActivePlugin(const juce::String& pluginKey);

    // Salva o estado atual do processor como um novo preset com o nome dado.
    // Retorna true em caso de sucesso.
    bool savePreset(juce::AudioProcessor& processor, const juce::String& presetName);

    // Carrega um preset pelo nome, aplicando o estado no processor.
    bool loadPreset(juce::AudioProcessor& processor, const juce::String& presetName);

    // Apaga um preset salvo.
    bool deletePreset(const juce::String& presetName);

    // Lista os nomes dos presets salvos para o plugin ativo.
    juce::StringArray listPresets() const;

private:
    juce::File getPresetFolder() const;
    juce::File getPresetFile(const juce::String& presetName) const;

    juce::String activePluginName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetManager)
};
