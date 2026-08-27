#include "MainComponent.h"

namespace
{
class PluginListModel : public juce::ListBoxModel
{
public:
    PluginListModel(PluginHostEngine& engineToUse, std::function<void(int)> doubleClickCallback)
        : engine(engineToUse), onDoubleClick(std::move(doubleClickCallback)) {}

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

    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override
    {
        if (onDoubleClick)
            onDoubleClick(row);
    }

private:
    PluginHostEngine& engine;
    std::function<void(int)> onDoubleClick;
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
                       std::function<void(const juce::String&)> statusCallback,
                       std::function<bool(MidiTriggerAction, int)> tryGlobalLearnCallback,
                       std::function<bool(int)> tryGlobalVolumeLearnCallback)
        : engine(engineToUse),
          pluginId(pluginIdToUse),
          openEditor(std::move(openEditorCallback)),
          removePlugin(std::move(removeCallback)),
          savePreset(std::move(savePresetCallback)),
          setStatus(std::move(statusCallback)),
          tryGlobalLearn(std::move(tryGlobalLearnCallback)),
          tryGlobalVolumeLearn(std::move(tryGlobalVolumeLearnCallback))
    {
        addAndMakeVisible(nameLabel);
        nameLabel.setJustificationType(juce::Justification::centredTop);
        nameLabel.setFont(juce::Font(13.0f, juce::Font::bold));
        nameLabel.setMinimumHorizontalScale(1.0f); // não encolhe a fonte, deixa o texto quebrar de linha

        addAndMakeVisible(editorButton);
        editorButton.onClick = [this] { openEditor(pluginId); };

        addAndMakeVisible(muteButton);
        muteButton.setClickingTogglesState(true);
        muteButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::orange);
        muteButton.setToggleState(engine.isPluginMuted(pluginId), juce::dontSendNotification);
        muteButton.onClick = [this]
        {
            // Se o Learn global estiver aguardando um clique, este clique
            // escolhe o controle em vez de mutar de verdade - o toggle
            // visual é desfeito na hora, porque o clique não foi um mute.
            if (tryGlobalLearn(MidiTriggerAction::toggleMute, pluginId))
            {
                muteButton.setToggleState(engine.isPluginMuted(pluginId), juce::dontSendNotification);
                return;
            }

            engine.setPluginMuted(pluginId, muteButton.getToggleState());
        };

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
        soloButton.onClick = [this]
        {
            if (tryGlobalLearn(MidiTriggerAction::toggleSolo, pluginId))
            {
                soloButton.setToggleState(engine.isPluginSolo(pluginId), juce::dontSendNotification);
                return;
            }

            engine.setPluginSolo(pluginId, soloButton.getToggleState());
        };

        addAndMakeVisible(soloLearnButton);
        soloLearnButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::red);
        soloLearnButton.onClick = [this]
        {
            if (engine.isMidiLearnTarget(MidiTriggerAction::toggleSolo, pluginId))
                engine.cancelMidiLearn();
            else
                engine.startMidiLearn(MidiTriggerAction::toggleSolo, pluginId);
        };

        addAndMakeVisible(sceneLearnButton);
        sceneLearnButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::red);
        sceneLearnButton.onClick = [this]
        {
            if (engine.isMidiLearnTarget(MidiTriggerAction::activateScene, pluginId))
                engine.cancelMidiLearn();
            else
                engine.startMidiLearn(MidiTriggerAction::activateScene, pluginId);
        };

        addAndMakeVisible(sceneButton);
        sceneButton.setClickingTogglesState(true);
        sceneButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::cyan);
        sceneButton.setToggleState(engine.getActiveScene() == pluginId, juce::dontSendNotification);
        sceneButton.onClick = [this]
        {
            // Se o Learn global estiver aguardando um clique, este clique
            // escolhe o controle - não troca de cena de verdade.
            if (tryGlobalLearn(MidiTriggerAction::activateScene, pluginId))
                return;

            // Um toggle exclusivo: se este plugin já era a cena ativa,
            // desliga (-1); senão, esta vira a cena ativa (troca a
            // anterior, não acumula - diferente do solo).
            engine.setActiveScene(engine.getActiveScene() == pluginId ? -1 : pluginId);
        };

        addAndMakeVisible(removeButton);
        removeButton.onClick = [this] { removePlugin(pluginId); };

        addAndMakeVisible(volumeSlider);
        volumeSlider.setSliderStyle(juce::Slider::LinearVertical);
        volumeSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 40, 18);
        volumeSlider.setRange(0.0, 2.0, 0.01);
        volumeSlider.setDoubleClickReturnValue(true, 1.0); // duplo clique reseta pra 100%
        volumeSlider.setValue(engine.getPluginVolume(pluginId), juce::dontSendNotification);

        // onDragStart, não onValueChange: um slider dispara onValueChange a
        // cada pixel arrastado, então é o primeiro toque (início do arrasto)
        // que representa "o usuário escolheu este controle" pro Learn global -
        // onValueChange continua existindo só pra mover o volume de verdade.
        volumeSlider.onDragStart = [this]
        {
            if (tryGlobalVolumeLearn(pluginId))
            {
                // Devolve o slider pro valor atual (desfaz qualquer arrasto
                // que já tenha mexido, já que esse gesto não era pra
                // ajustar volume - era só a escolha do controle a aprender).
                volumeSlider.setValue(engine.getPluginVolume(pluginId), juce::dontSendNotification);
            }
        };
        volumeSlider.onValueChange = [this]
        {
            engine.setPluginVolume(pluginId, (float) volumeSlider.getValue());
        };

        addAndMakeVisible(volumeLearnButton);
        volumeLearnButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::red);
        volumeLearnButton.onClick = [this]
        {
            if (engine.isVolumeMidiLearnTarget(pluginId))
                engine.cancelMidiLearn();
            else
                engine.startVolumeMidiLearn(pluginId);
        };

        addAndMakeVisible(factoryLabel);
        factoryLabel.setText("Fabrica:", juce::dontSendNotification);
        factoryLabel.setFont(juce::Font(11.0f));

        addAndMakeVisible(factoryBox);
        factoryBox.onChange = [this]
        {
            const int index = factoryBox.getSelectedId() - 1;
            if (index >= 0)
                engine.setCurrentFactoryProgram(pluginId, index);
        };

        addAndMakeVisible(presetLabel);
        presetLabel.setText("Presets:", juce::dontSendNotification);
        presetLabel.setFont(juce::Font(11.0f));

        addAndMakeVisible(presetBox);

        addAndMakeVisible(savePresetButton);
        savePresetButton.setButtonText("Salvar");
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

        volumeSlider.setValue(engine.getPluginVolume(pluginId), juce::dontSendNotification);

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
        volumeSlider.setValue(engine.getPluginVolume(pluginId), juce::dontSendNotification);
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
        const bool sceneIsTarget = engine.isMidiLearnTarget(MidiTriggerAction::activateScene, pluginId);

        muteLearnButton.setToggleState(muteIsTarget, juce::dontSendNotification);
        soloLearnButton.setToggleState(soloIsTarget, juce::dontSendNotification);
        sceneLearnButton.setToggleState(sceneIsTarget, juce::dontSendNotification);

        muteLearnButton.setButtonText(muteIsTarget ? "Aguardando..." : describe(MidiTriggerAction::toggleMute));
        soloLearnButton.setButtonText(soloIsTarget ? "Aguardando..." : describe(MidiTriggerAction::toggleSolo));
        sceneLearnButton.setButtonText(sceneIsTarget ? "Aguardando..." : describe(MidiTriggerAction::activateScene));

        const bool volumeIsTarget = engine.isVolumeMidiLearnTarget(pluginId);
        volumeLearnButton.setToggleState(volumeIsTarget, juce::dontSendNotification);

        const auto volumeBindingDescription = engine.getVolumeMidiBindingDescription(pluginId);
        volumeLearnButton.setButtonText(volumeIsTarget
                                             ? "Aguardando..."
                                             : (volumeBindingDescription.isNotEmpty() ? volumeBindingDescription : "Learn"));
    }

    // Largura fixa de cada coluna - o container pai (ver MainComponent::
    // refreshLoadedPlugins) usa esse mesmo valor pra posicionar as colunas
    // lado a lado. Mudar aqui é a única coisa que precisa mudar pra ajustar
    // a largura de todas as colunas de uma vez.
    static constexpr int columnWidth = 150; // 120 + espaço do slider de volume vertical

    int getPluginId() const noexcept { return pluginId; }

    void resized() override
    {
        auto area = getLocalBounds().reduced(4);

        // Reserva a faixa da direita para o slider de volume antes de tudo -
        // ele ocupa, verticalmente, exatamente do topo do botão Cena até o
        // fundo do botão Remover (calculado abaixo, depois que os dois
        // existirem). Horizontalmente, fica numa coluna estreita à direita,
        // encolhendo a área disponível pros outros controles.
        auto volumeColumn = area.removeFromRight(28);
        area.removeFromRight(4); // respiro entre os controles e o slider

        nameLabel.setBounds(area.removeFromTop(36));
        area.removeFromTop(4);

        editorButton.setBounds(area.removeFromTop(26));
        area.removeFromTop(6);

        muteButton.setBounds(area.removeFromTop(24));
        area.removeFromTop(2);
        muteLearnButton.setBounds(area.removeFromTop(20));
        area.removeFromTop(6);

        soloButton.setBounds(area.removeFromTop(24));
        area.removeFromTop(2);
        soloLearnButton.setBounds(area.removeFromTop(20));
        area.removeFromTop(6);

        const int sceneTop = area.getY(); // topo do botão Cena - início da faixa do slider

        sceneButton.setBounds(area.removeFromTop(24));
        area.removeFromTop(2);
        sceneLearnButton.setBounds(area.removeFromTop(20));
        area.removeFromTop(10);

        factoryLabel.setBounds(area.removeFromTop(16));
        factoryBox.setBounds(area.removeFromTop(24));
        area.removeFromTop(10);

        presetLabel.setBounds(area.removeFromTop(16));
        presetBox.setBounds(area.removeFromTop(24));
        area.removeFromTop(2);
        savePresetButton.setBounds(area.removeFromTop(22));
        area.removeFromTop(2);
        loadPresetButton.setBounds(area.removeFromTop(22));
        area.removeFromTop(2);
        deletePresetButton.setBounds(area.removeFromTop(22));
        area.removeFromTop(10);

        removeButton.setBounds(area.removeFromTop(24));

        const int sceneToRemoveBottom = removeButton.getBottom(); // fundo do Remover - fim da faixa do slider

        // Slider vertical ocupando a faixa toda, exceto os últimos 24px
        // reservados pro botão Learn dele logo abaixo - a faixa total
        // (topo do Cena até fundo do Remover) continua sendo a pedida;
        // ela só é dividida entre o slider e o botão Learn dele.
        const int volumeLearnHeight = 24;
        volumeSlider.setBounds(volumeColumn.getX(), sceneTop,
                               volumeColumn.getWidth(),
                               (sceneToRemoveBottom - sceneTop) - volumeLearnHeight - 4);
        volumeLearnButton.setBounds(volumeColumn.getX(), sceneToRemoveBottom - volumeLearnHeight,
                                    volumeColumn.getWidth(), volumeLearnHeight);
    }

private:
    PluginHostEngine& engine;
    const int pluginId;

    std::function<void(int)> openEditor;
    std::function<void(int)> removePlugin;
    std::function<void(int)> savePreset;
    std::function<void(const juce::String&)> setStatus;
    std::function<bool(MidiTriggerAction, int)> tryGlobalLearn;
    std::function<bool(int)> tryGlobalVolumeLearn;

    juce::Label nameLabel;
    juce::TextButton editorButton { "Abrir Interface" };
    juce::TextButton removeButton { "Remover" };
    juce::Slider volumeSlider;
    juce::TextButton volumeLearnButton { "Learn" };
    juce::TextButton muteButton { "Mute" };
    juce::TextButton soloButton { "Solo" };
    juce::TextButton sceneButton { "Cena" };
    juce::TextButton muteLearnButton { "Learn" };
    juce::TextButton soloLearnButton { "Learn" };
    juce::TextButton sceneLearnButton { "Learn" };

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
    pluginListModel = std::make_unique<PluginListModel>(engine, [this](int) { loadSelectedPlugin(); });
    pluginList.setModel(pluginListModel.get());
    pluginList.setRowHeight(28);

    addAndMakeVisible(loadPluginButton);
    loadPluginButton.onClick = [this] { loadSelectedPlugin(); };

    addAndMakeVisible(globalLearnButton);
    globalLearnButton.setClickingTogglesState(true);
    globalLearnButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::red);
    globalLearnButton.onClick = [this]
    {
        // Botão de dois estados: ligar arma o modo "aguardando escolha de
        // controle"; desligar (clicando de novo) desiste sem escolher nada.
        globalLearnArmed = globalLearnButton.getToggleState();
        setStatus(globalLearnArmed
                       ? "MIDI Learn: clique em um botao Mute ou Solo, depois toque a tecla."
                       : "MIDI Learn cancelado.");
    };

    addAndMakeVisible(loadedPluginsViewport);
    loadedPluginsViewport.setViewedComponent(&loadedPluginsContainer, false);
    loadedPluginsViewport.setScrollBarsShown(false, true); // rolagem horizontal, não vertical

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

    {
        auto row = area.removeFromTop(30);
        globalLearnButton.setBounds(row.removeFromRight(90));
        row.removeFromRight(6);
        loadPluginButton.setBounds(row);
    }
    area.removeFromTop(12);

    auto loadedLabelArea = area.removeFromTop(24);
    loadedLabelArea.removeFromLeft(4);

    loadedPluginsViewport.setBounds(area.removeFromTop(520));
    area.removeFromTop(8);

    statusLabel.setBounds(area.removeFromBottom(24));

    // Cada plugin agora é uma coluna estreita e alta (estilo mixer de DAW),
    // lado a lado, em vez de uma linha larga empilhada verticalmente. Isso
    // é o que permite ver 6-8 plugins de uma vez rolando na horizontal, em
    // vez de rolar na vertical pra achar o plugin que você quer.
    const int columnWidth = PluginRowComponent::columnWidth;
    const int columnGap = 6;
    const int totalWidth = (columnWidth + columnGap) * (int) pluginRows.size();

    loadedPluginsContainer.setSize(juce::jmax(totalWidth, loadedPluginsViewport.getMaximumVisibleWidth()),
                                   loadedPluginsViewport.getHeight());

    int x = 0;
    for (auto& row : pluginRows)
    {
        row->setBounds(x, 0, columnWidth, loadedPluginsContainer.getHeight());
        x += columnWidth + columnGap;
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

    // Terminou de aprender (ou foi cancelado por algum Learn por-botão) -
    // desarma o botão global também, pra ele não ficar aceso indefinidamente.
    if (!engine.isMidiLearnActive() && globalLearnArmed)
    {
        globalLearnArmed = false;
        globalLearnButton.setToggleState(false, juce::dontSendNotification);
    }
}

bool MainComponent::tryStartLearnFromGlobalArm(MidiTriggerAction action, int pluginId)
{
    if (!globalLearnArmed)
        return false;

    // Consumido: o próximo passo agora é aguardar a tecla, não mais
    // aguardar a escolha do controle - desarma o botão global e delega
    // pro mesmo mecanismo de captura que o Learn por-botão já usa.
    globalLearnArmed = false;
    globalLearnButton.setToggleState(false, juce::dontSendNotification);

    engine.startMidiLearn(action, pluginId);
    setStatus("MIDI Learn: toque a tecla ou pad agora.");
    return true;
}

bool MainComponent::tryStartVolumeLearnFromGlobalArm(int pluginId)
{
    if (!globalLearnArmed)
        return false;

    globalLearnArmed = false;
    globalLearnButton.setToggleState(false, juce::dontSendNotification);

    engine.startVolumeMidiLearn(pluginId);
    setStatus("MIDI Learn: mexa o fader/botao de controle agora.");
    return true;
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
            [this](const juce::String& message) { setStatus(message); },
            [this](MidiTriggerAction action, int id) { return tryStartLearnFromGlobalArm(action, id); },
            [this](int id) { return tryStartVolumeLearnFromGlobalArm(id); });

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
