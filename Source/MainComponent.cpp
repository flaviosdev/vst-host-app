#include "MainComponent.h"

namespace
{
class PluginListModel : public juce::ListBoxModel
{
public:
    explicit PluginListModel(PluginHostEngine& engineToUse) : engine(engineToUse) {}

    int getNumRows() override { return engine.getKnownPlugins().size(); }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        const auto plugins = engine.getKnownPlugins();
        if (row < 0 || row >= plugins.size()) return;

        if (selected)
            g.fillAll(juce::Colours::lightblue);

        g.setColour(juce::Colours::white);
        const auto& p = plugins[row];
        g.drawText(p.name + "  [" + p.pluginFormatName + "]",
                   8, 0, width - 16, height, juce::Justification::centredLeft);
    }

private:
    PluginHostEngine& engine;
};
}

//==============================================================================
// Uma linha representa um plugin que está realmente carregado.
// Cada linha possui suas próprias ações, então abrir interface, trocar
// programa ou mexer nos presets afeta somente aquele plugin.
class MainComponent::PluginRowComponent : public juce::Component
{
public:
    PluginRowComponent(PluginHostEngine& engineToUse,
                       int pluginIdToUse,
                       std::function<void(int)> openEditorCallback,
                       std::function<void(int)> removeCallback,
                       std::function<void(int)> savePresetCallback,
                       std::function<void(const juce::String&)> statusCallback)
        : engine(engineToUse),
          pluginId(pluginIdToUse),
          openEditor(std::move(openEditorCallback)),
          removePlugin(std::move(removeCallback)),
          savePreset(std::move(savePresetCallback)),
          setStatus(std::move(statusCallback))
    {
        addAndMakeVisible(nameLabel);
        nameLabel.setJustificationType(juce::Justification::centredLeft);
        nameLabel.setFont(juce::Font(16.0f, juce::Font::bold));

        addAndMakeVisible(editorButton);
        editorButton.onClick = [this] { openEditor(pluginId); };

        addAndMakeVisible(muteButton);
        muteButton.setClickingTogglesState(true);
        muteButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::orange);
        muteButton.setToggleState(engine.isPluginMuted(pluginId), juce::dontSendNotification);
        muteButton.onClick = [this] { engine.setPluginMuted(pluginId, muteButton.getToggleState()); };

        addAndMakeVisible(muteLearnButton);
        muteLearnButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::red);
        muteLearnButton.onClick = [this]
        {
            // Clicar de novo no mesmo Learn que já está aguardando cancela -
            // é o jeito natural de desistir sem apertar tecla nenhuma.
            if (engine.isMidiLearnTarget(MidiTriggerAction::toggleMute, pluginId))
                engine.cancelMidiLearn();
            else
                engine.startMidiLearn(MidiTriggerAction::toggleMute, pluginId);
        };

        addAndMakeVisible(soloButton);
        soloButton.setClickingTogglesState(true);
        soloButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::yellow);
        soloButton.setToggleState(engine.isPluginSolo(pluginId), juce::dontSendNotification);
        soloButton.onClick = [this] { engine.setPluginSolo(pluginId, soloButton.getToggleState()); };

        addAndMakeVisible(soloLearnButton);
        soloLearnButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::red);
        soloLearnButton.onClick = [this]
        {
            if (engine.isMidiLearnTarget(MidiTriggerAction::toggleSolo, pluginId))
                engine.cancelMidiLearn();
            else
                engine.startMidiLearn(MidiTriggerAction::toggleSolo, pluginId);
        };

        addAndMakeVisible(sceneButton);
        sceneButton.setClickingTogglesState(true);
        sceneButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::cyan);
        sceneButton.setToggleState(engine.getActiveScene() == pluginId, juce::dontSendNotification);
        sceneButton.onClick = [this]
        {
            // Um toggle exclusivo: se este plugin já era a cena ativa,
            // desliga (-1); senão, esta vira a cena ativa (troca a
            // anterior, não acumula - diferente do solo).
            engine.setActiveScene(engine.getActiveScene() == pluginId ? -1 : pluginId);
        };

        addAndMakeVisible(removeButton);
        removeButton.onClick = [this] { removePlugin(pluginId); };

        addAndMakeVisible(factoryLabel);
        factoryLabel.setText("Programas de fabrica:", juce::dontSendNotification);

        addAndMakeVisible(factoryBox);
        factoryBox.onChange = [this]
        {
            const int index = factoryBox.getSelectedId() - 1;
            if (index >= 0)
                engine.setCurrentFactoryProgram(pluginId, index);
        };

        addAndMakeVisible(presetLabel);
        presetLabel.setText("Presets:", juce::dontSendNotification);

        addAndMakeVisible(presetBox);

        addAndMakeVisible(savePresetButton);
        savePresetButton.onClick = [this] { savePreset(pluginId); };

        addAndMakeVisible(loadPresetButton);
        loadPresetButton.onClick = [this]
        {
            const auto name = presetBox.getText();
            if (name.isEmpty()) return;

            if (engine.loadUserPreset(pluginId, name))
            {
                setStatus("Preset \"" + name + "\" carregado em " + engine.getPluginName(pluginId) + ".");
                refresh();
            }
            else
                setStatus("Falha ao carregar o preset.");
        };

        addAndMakeVisible(deletePresetButton);
        deletePresetButton.onClick = [this]
        {
            const auto name = presetBox.getText();
            if (name.isEmpty()) return;

            if (engine.deleteUserPreset(pluginId, name))
            {
                setStatus("Preset \"" + name + "\" excluido.");
                refresh();
            }
            else
                setStatus("Falha ao excluir o preset.");
        };

        refresh();
    }

    void refresh()
    {
        nameLabel.setText(engine.getPluginName(pluginId), juce::dontSendNotification);

        factoryBox.clear();
        const auto factoryPrograms = engine.getFactoryProgramNames(pluginId);
        for (int i = 0; i < factoryPrograms.size(); ++i)
            factoryBox.addItem(factoryPrograms[i], i + 1);

        const int currentProgram = engine.getCurrentFactoryProgram(pluginId);
        factoryBox.setSelectedId(currentProgram >= 0 ? currentProgram + 1 : 0,
                                 juce::dontSendNotification);
        factoryBox.setEnabled(!factoryPrograms.isEmpty());

        presetBox.clear();
        const auto presets = engine.getUserPresetNames(pluginId);
        for (int i = 0; i < presets.size(); ++i)
            presetBox.addItem(presets[i], i + 1);

        const bool hasPresets = !presets.isEmpty();
        presetBox.setEnabled(hasPresets);
        loadPresetButton.setEnabled(hasPresets);
        deletePresetButton.setEnabled(hasPresets);

        refreshRoute();
        refreshScene();
        refreshMidiLearn();
    }

    // Só sincroniza os toggles de mute/solo (mais barato que refresh() inteiro).
    // Precisa ser chamado pra QUALQUER linha quando QUALQUER plugin muda de
    // solo, porque ligar o solo de um plugin muda o resultado sonoro de todos
    // os outros (mesmo que o estado deles não tenha mudado).
    void refreshRoute()
    {
        muteButton.setToggleState(engine.isPluginMuted(pluginId), juce::dontSendNotification);
        soloButton.setToggleState(engine.isPluginSolo(pluginId), juce::dontSendNotification);
    }

    // Sincroniza só o botão de Cena. Separado de refreshRoute() porque cena
    // ativa é um conceito à parte de mute/solo manual (ver PluginHostEngine),
    // e precisa ser chamado em TODAS as linhas sempre que a cena mudar,
    // já que ativar uma cena desliga visualmente todas as outras.
    void refreshScene()
    {
        sceneButton.setToggleState(engine.getActiveScene() == pluginId, juce::dontSendNotification);
    }

    // Sincroniza os botões de Learn: mostra se ESTE plugin/ação específico
    // está aguardando uma tecla agora, e o texto do botão passa a mostrar
    // a nota já vinculada (ex.: "Nota 36 / Ch10"), se houver. Precisa ser
    // chamado em TODAS as linhas sempre que o estado de learn mudar, porque
    // iniciar um learn em qualquer botão cancela visualmente todos os outros
    // (só pode haver uma captura em andamento no host inteiro).
    void refreshMidiLearn()
    {
        const auto bindings = engine.getMidiBindingsForPlugin(pluginId);

        auto describe = [&bindings](MidiTriggerAction action) -> juce::String
        {
            for (const auto& binding : bindings)
                if (binding.action == action)
                    return "Nota " + juce::String(binding.noteNumber) + " / Ch" + juce::String(binding.midiChannel);
            return "Learn";
        };

        const bool muteIsTarget = engine.isMidiLearnTarget(MidiTriggerAction::toggleMute, pluginId);
        const bool soloIsTarget = engine.isMidiLearnTarget(MidiTriggerAction::toggleSolo, pluginId);

        muteLearnButton.setToggleState(muteIsTarget, juce::dontSendNotification);
        soloLearnButton.setToggleState(soloIsTarget, juce::dontSendNotification);

        muteLearnButton.setButtonText(muteIsTarget ? "Aguardando..." : describe(MidiTriggerAction::toggleMute));
        soloLearnButton.setButtonText(soloIsTarget ? "Aguardando..." : describe(MidiTriggerAction::toggleSolo));
    }

    int getPluginId() const noexcept { return pluginId; }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);

        auto top = area.removeFromTop(30);
        nameLabel.setBounds(top.removeFromLeft(220));
        removeButton.setBounds(top.removeFromRight(90).reduced(2, 0));
        editorButton.setBounds(top.removeFromRight(170).reduced(2, 0));
        soloButton.setBounds(top.removeFromRight(50).reduced(2, 0));
        muteButton.setBounds(top.removeFromRight(50).reduced(2, 0));
        sceneButton.setBounds(top.removeFromRight(55).reduced(2, 0));

        area.removeFromTop(4);

        auto learnRow = area.removeFromTop(24);
        learnRow.removeFromLeft(220); // alinha embaixo de mute/solo, não do nome
        muteLearnButton.setBounds(learnRow.removeFromLeft(110).reduced(2, 0));
        learnRow.removeFromLeft(4);
        soloLearnButton.setBounds(learnRow.removeFromLeft(110).reduced(2, 0));

        area.removeFromTop(6);

        auto factoryRow = area.removeFromTop(28);
        factoryLabel.setBounds(factoryRow.removeFromLeft(120));
        factoryBox.setBounds(factoryRow);

        area.removeFromTop(6);

        auto presetRow = area.removeFromTop(28);
        presetLabel.setBounds(presetRow.removeFromLeft(60));
        deletePresetButton.setBounds(presetRow.removeFromRight(75).reduced(2, 0));
        loadPresetButton.setBounds(presetRow.removeFromRight(75).reduced(2, 0));
        savePresetButton.setBounds(presetRow.removeFromRight(90).reduced(2, 0));
        presetBox.setBounds(presetRow);
    }

private:
    PluginHostEngine& engine;
    const int pluginId;

    std::function<void(int)> openEditor;
    std::function<void(int)> removePlugin;
    std::function<void(int)> savePreset;
    std::function<void(const juce::String&)> setStatus;

    juce::Label nameLabel;
    juce::TextButton editorButton { "Abrir Interface" };
    juce::TextButton removeButton { "Remover" };
    juce::TextButton muteButton { "Mute" };
    juce::TextButton soloButton { "Solo" };
    juce::TextButton sceneButton { "Cena" };
    juce::TextButton muteLearnButton { "Learn" };
    juce::TextButton soloLearnButton { "Learn" };

    juce::Label factoryLabel;
    juce::ComboBox factoryBox;

    juce::Label presetLabel;
    juce::ComboBox presetBox;
    juce::TextButton savePresetButton { "Salvar..." };
    juce::TextButton loadPresetButton { "Carregar" };
    juce::TextButton deletePresetButton { "Excluir" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginRowComponent)
};

//==============================================================================
MainComponent::MainComponent()
{
    engine.addListener(this);

    addAndMakeVisible(audioSettingsButton);
    audioSettingsButton.onClick = [this] { showPreferences(); };

    addAndMakeVisible(pluginList);
    pluginListModel = std::make_unique<PluginListModel>(engine);
    pluginList.setModel(pluginListModel.get());
    pluginList.setRowHeight(28);

    addAndMakeVisible(loadPluginButton);
    loadPluginButton.onClick = [this] { loadSelectedPlugin(); };

    addAndMakeVisible(loadedPluginsViewport);
    loadedPluginsViewport.setViewedComponent(&loadedPluginsContainer, false);
    loadedPluginsViewport.setScrollBarsShown(true, false);

    statusLabel.setJustificationType(juce::Justification::centredLeft);
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgreen);
    addAndMakeVisible(statusLabel);

    refreshPluginList();
    refreshLoadedPlugins();

    setSize(760, 620);
}

MainComponent::~MainComponent()
{
    engine.removeListener(this);

    for (auto& [pluginId, window] : pluginEditorWindows)
        window.reset();
    pluginEditorWindows.clear();
    pluginEditors.clear();

    preferencesWindow.reset();
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(12);

    audioSettingsButton.setBounds(area.removeFromTop(30));
    area.removeFromTop(8);

    pluginList.setBounds(area.removeFromTop(150));
    area.removeFromTop(8);

    loadPluginButton.setBounds(area.removeFromTop(30));
    area.removeFromTop(12);

    auto loadedLabelArea = area.removeFromTop(24);
    loadedLabelArea.removeFromLeft(4);

    loadedPluginsViewport.setBounds(area.removeFromTop(290));
    area.removeFromTop(8);

    statusLabel.setBounds(area.removeFromBottom(24));

    const int rowHeight = 140;
    loadedPluginsContainer.setSize(loadedPluginsViewport.getMaximumVisibleWidth(),
                                   juce::jmax(rowHeight * (int) pluginRows.size(),
                                              loadedPluginsViewport.getHeight()));

    int y = 0;
    for (auto& row : pluginRows)
    {
        row->setBounds(0, y, loadedPluginsContainer.getWidth(), rowHeight);
        y += rowHeight;
    }
}

//==============================================================================
void MainComponent::setStatus(const juce::String& message)
{
    statusLabel.setText(message, juce::dontSendNotification);
}

void MainComponent::pluginChanged()
{
    refreshLoadedPlugins();
}

void MainComponent::audioDeviceChanged()
{
}

void MainComponent::pluginsChanged()
{
    refreshPluginList();
}

void MainComponent::pluginRouteChanged(int)
{
    // Sincroniza TODAS as linhas, não só a do plugin que mudou: ligar o
    // solo de um plugin muda o que se ouve de todos os outros, então os
    // botões deles também precisam refletir isso (mesmo sem terem sido
    // clicados). O parâmetro pluginId não é usado por esse motivo.
    for (auto& row : pluginRows)
        row->refreshRoute();
}

void MainComponent::activeSceneChanged(int)
{
    // Mesma lógica do pluginRouteChanged: ativar uma cena precisa desligar
    // visualmente o botão de Cena de todas as OUTRAS linhas, não só ligar
    // a que foi clicada.
    for (auto& row : pluginRows)
        row->refreshScene();
}

void MainComponent::midiLearnStateChanged()
{
    // Mesmo motivo dos dois acima: iniciar uma captura em qualquer botão
    // Learn precisa apagar visualmente o "Aguardando..." de todos os outros
    // (só uma captura por vez no host inteiro), e aprender/remover um
    // binding muda o texto do botão correspondente em qualquer linha.
    for (auto& row : pluginRows)
        row->refreshMidiLearn();
}

void MainComponent::refreshPluginList()
{
    pluginList.updateContent();
    pluginList.repaint();
}

void MainComponent::refreshLoadedPlugins()
{
    const auto ids = engine.getLoadedPluginIds();

    // Fechamos editores que pertencem a plugins que não existem mais.
    for (auto it = pluginEditorWindows.begin(); it != pluginEditorWindows.end();)
    {
        if (!ids.contains(it->first))
        {
            pluginEditors.erase(it->first);
            it = pluginEditorWindows.erase(it);
        }
        else
            ++it;
    }

    pluginRows.clear();

    for (const auto pluginId : ids)
    {
        auto row = std::make_unique<PluginRowComponent>(
            engine,
            pluginId,
            [this](int id) { openPluginEditor(id); },
            [this](int id)
            {
                closePluginEditor(id);
                engine.unloadPlugin(id);
            },
            [this](int id) { savePresetForPlugin(id); },
            [this](const juce::String& message) { setStatus(message); });

        loadedPluginsContainer.addAndMakeVisible(row.get());
        pluginRows.push_back(std::move(row));
    }

    resized();
}

//==============================================================================
void MainComponent::showPreferences()
{
    if (preferencesWindow != nullptr)
    {
        preferencesWindow->toFront(true);
        return;
    }

    preferencesWindow = std::make_unique<PreferencesWindow>(engine);
    preferencesWindow->onCloseRequested = [this]
    {
        engine.saveAudioDeviceState();
        preferencesWindow.reset();
    };
}

void MainComponent::loadSelectedPlugin()
{
    const int row = pluginList.getSelectedRow();
    const auto plugins = engine.getKnownPlugins();

    if (row < 0 || row >= plugins.size())
    {
        setStatus("Selecione um plugin na lista.");
        return;
    }

    const auto result = engine.loadPlugin(plugins[row]);
    setStatus(result.success
                  ? "Plugin \"" + result.pluginName + "\" carregado."
                  : "Erro ao carregar plugin: " + result.errorMessage);
}

//==============================================================================
void MainComponent::openPluginEditor(int pluginId)
{
    if (!engine.hasPluginLoaded(pluginId))
        return;

    if (auto it = pluginEditorWindows.find(pluginId); it != pluginEditorWindows.end())
    {
        it->second->toFront(true);
        return;
    }

    auto* editor = engine.createPluginEditorIfNeeded(pluginId);
    if (editor == nullptr)
    {
        setStatus("Este plugin nao possui interface grafica propria.");
        return;
    }

    pluginEditors[pluginId].reset(editor);

    class EditorWindow : public juce::DocumentWindow
    {
    public:
        EditorWindow(const juce::String& name, std::function<void()> onCloseCb)
            : DocumentWindow(name, juce::Colours::darkgrey, juce::DocumentWindow::closeButton),
              onClose(std::move(onCloseCb))
        {
        }

        void closeButtonPressed() override
        {
            if (onClose)
                onClose();
        }

    private:
        std::function<void()> onClose;
    };

    auto* window = new EditorWindow(engine.getPluginName(pluginId),
                                    [this, pluginId] { closePluginEditor(pluginId); });
    pluginEditorWindows[pluginId].reset(window);

    window->setUsingNativeTitleBar(true);
    window->setContentNonOwned(pluginEditors[pluginId].get(), true);
    window->centreWithSize(pluginEditors[pluginId]->getWidth(),
                           pluginEditors[pluginId]->getHeight());
    window->setResizable(pluginEditors[pluginId]->isResizable(), false);
    window->setVisible(true);
}

void MainComponent::closePluginEditor(int pluginId)
{
    pluginEditorWindows.erase(pluginId);
    pluginEditors.erase(pluginId);
}

void MainComponent::savePresetForPlugin(int pluginId)
{
    if (!engine.hasPluginLoaded(pluginId))
        return;

    auto* window = new juce::AlertWindow("Salvar Preset",
                                         "Digite um nome para o preset:",
                                         juce::AlertWindow::NoIcon);
    window->addTextEditor("presetName", "", "Nome do preset:");
    window->addButton("Salvar", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancelar", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    window->enterModalState(true, juce::ModalCallbackFunction::create([this, window, pluginId](int result)
    {
        std::unique_ptr<juce::AlertWindow> owned(window);

        if (result != 1)
            return;

        auto name = owned->getTextEditorContents("presetName").trim();
        if (name.isEmpty())
            return;

        if (engine.saveUserPreset(pluginId, name))
        {
            setStatus("Preset \"" + name + "\" salvo.");
            refreshLoadedPlugins();
        }
        else
            setStatus("Falha ao salvar o preset.");
    }), true);
}
