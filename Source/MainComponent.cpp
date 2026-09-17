#include "MainComponent.h"

namespace
{
// Layout ABNT2: duas linhas do teclado do computador mapeadas cromaticamente
// para notas MIDI, começando em C2 na tecla "\". A tecla "q" da segunda
// linha toca a mesma nota que "m" da primeira - as duas linhas se completam
// numa escala cromática contínua de C2 até F#4.
//
// O deslocamento é relativo a C2; a nota MIDI final é computerKeyboardBaseNote
// (36 = C2, considerando setOctaveForMiddleC(4)) + esse deslocamento.
const std::vector<std::pair<juce::juce_wchar, int>>& getComputerKeyboardOffsets()
{
    static const std::vector<std::pair<juce::juce_wchar, int>> table = {
        // Linha 1: \azsxcfvgbhnmk,l.;  (C2 até F3)
        { '\\', 0 }, { 'a', 1 }, { 'z', 2 }, { 's', 3 }, { 'x', 4 }, { 'c', 5 },
        { 'f', 6 }, { 'v', 7 }, { 'g', 8 }, { 'b', 9 }, { 'h', 10 }, { 'n', 11 },
        { 'm', 12 }, { 'k', 13 }, { ',', 14 }, { 'l', 15 }, { '.', 16 }, { ';', 17 },

        // Linha 2: q2w3er5t6y7ui9o0p´=  (C3 até F#4 - "q" = mesma nota que "m")
        { 'q', 12 }, { '2', 13 }, { 'w', 14 }, { '3', 15 }, { 'e', 16 }, { 'r', 17 },
        { '5', 18 }, { 't', 19 }, { '6', 20 }, { 'y', 21 }, { '7', 22 }, { 'u', 23 },
        { 'i', 24 }, { '9', 25 }, { 'o', 26 }, { '0', 27 }, { 'p', 28 },
        { 0x00B4, 29 }, { '=', 30 }
    };
    return table;
}
}

namespace
{
/**
    Barra de nível vertical estilo VU meter de DAW: sobe rápido em resposta
    a picos, desce suave (efeito "ballistics" clássico de medidor). Não
    conhece a Engine - só recebe o nível atual via setLevel() de fora,
    chamado por um Timer no componente pai.
*/
class AudioLevelMeter : public juce::Component
{
public:
    void setLevel(float newPeakLevel)
    {
        // Ballistics simples: sobe imediato pro pico novo se ele for maior;
        // decai suavemente (multiplicador por frame) se o pico novo for menor.
        // O decaimento é o que dá aquele efeito "cai devagarinho" de VU meter
        // de verdade, em vez de um número pulando abruptamente pra baixo.
        if (newPeakLevel > displayedLevel)
            displayedLevel = newPeakLevel;
        else
            displayedLevel *= 0.85f;

        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        g.setColour(findColour(juce::ResizableWindow::backgroundColourId).darker(0.15f));
        g.fillRoundedRectangle(bounds, 3.0f);

        const float clamped = juce::jlimit(0.0f, 1.5f, displayedLevel);
        const float filledHeight = bounds.getHeight() * juce::jmin(1.0f, clamped);

        auto filled = bounds.removeFromBottom(filledHeight);

        // Verde na maior parte da faixa, amarelo perto do topo (~0dB),
        // vermelho só na pontinha final (acima de 1.0 = clipping) -
        // convenção visual padrão de medidor de DAW.
        const auto colour = clamped > 1.0f ? juce::Colours::red
                           : clamped > 0.8f ? juce::Colours::yellow
                                             : juce::Colours::limegreen;

        g.setColour(colour);
        g.fillRoundedRectangle(filled, 3.0f);
    }

private:
    float displayedLevel = 0.0f;
};

/**
    Bolinha que pisca quando chega atividade MIDI num plugin, e apaga
    sozinha depois de um tempo fixo curto. Também não conhece a Engine -
    só recebe true/false via flash() e sabe apagar a própria animação.
*/
class MidiActivityIndicator : public juce::Component,
                               private juce::Timer
{
public:
    void flash()
    {
        litUntilMs = juce::Time::getMillisecondCounter() + flashDurationMs;
        if (!isTimerRunning())
            startTimer(30);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const bool lit = juce::Time::getMillisecondCounter() < litUntilMs;
        g.setColour(lit ? juce::Colours::orange
                        : findColour(juce::ResizableWindow::backgroundColourId).darker(0.2f));
        g.fillEllipse(getLocalBounds().toFloat().reduced(1.0f));
    }

private:
    void timerCallback() override
    {
        repaint();

        if (juce::Time::getMillisecondCounter() >= litUntilMs)
            stopTimer();
    }

    static constexpr juce::uint32 flashDurationMs = 120;
    juce::uint32 litUntilMs = 0;
};
}

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
class MainComponent::PluginRowComponent : public juce::Component,
                                           private juce::Timer
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

        addAndMakeVisible(midiActivityIndicator);

        addAndMakeVisible(levelMeter);

        // ~30x por segundo: rápido o bastante pra parecer fluido, sem
        // sobrecarregar a UI. Lê os valores atômicos que a thread de áudio
        // escreve em PluginHostEngine::processPlugins.
        startTimerHz(30);

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
        refreshMidiLearn();
    }

    // Só sincroniza os toggles de mute/solo (mais barato que refresh() inteiro).
    // Precisa ser chamado pra QUALQUER linha quando QUALQUER plugin muda de
    // solo, porque ligar o solo de um plugin muda o resultado sonoro de todos
    // os outros (mesmo que o estado deles não tenha mudado). Também é
    // chamado quando o modo Exclusive liga/desliga (ver
    // MainComponent::exclusiveSoloChanged), pelo mesmo motivo.
    void refreshRoute()
    {
        muteButton.setToggleState(engine.isPluginMuted(pluginId), juce::dontSendNotification);
        soloButton.setToggleState(engine.isPluginSolo(pluginId), juce::dontSendNotification);
        volumeSlider.setValue(engine.getPluginVolume(pluginId), juce::dontSendNotification);
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
    static constexpr int columnWidth = 168; // 150 + espaço do medidor de nível de áudio vertical

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

        auto meterColumn = area.removeFromRight(14);
        area.removeFromRight(4); // respiro entre o slider e o medidor

        auto nameRow = area.removeFromTop(36);
        midiActivityIndicator.setBounds(nameRow.removeFromRight(14).withSizeKeepingCentre(10, 10));
        nameRow.removeFromRight(4);
        nameLabel.setBounds(nameRow);
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
        area.removeFromTop(10);

        // Marca o início da faixa vertical do slider de volume e do medidor
        // de nível - vai até o fundo do botão Remover, calculado mais abaixo.
        const int meterTop = area.getY();

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

        const int meterBottom = removeButton.getBottom();

        // Slider vertical ocupando a faixa toda, exceto os últimos 24px
        // reservados pro botão Learn dele logo abaixo.
        const int volumeLearnHeight = 24;
        volumeSlider.setBounds(volumeColumn.getX(), meterTop,
                               volumeColumn.getWidth(),
                               (meterBottom - meterTop) - volumeLearnHeight - 4);
        volumeLearnButton.setBounds(volumeColumn.getX(), meterBottom - volumeLearnHeight,
                                    volumeColumn.getWidth(), volumeLearnHeight);

        // Medidor de nível: mesma faixa vertical do slider, ao lado dele.
        levelMeter.setBounds(meterColumn.getX(), meterTop,
                             meterColumn.getWidth(), meterBottom - meterTop);
    }

private:
    // juce::Timer - chamado ~30x/segundo (ver startTimerHz no construtor).
    // Só leitura: consulta os valores atômicos escritos pela thread de
    // áudio e atualiza os dois indicadores visuais.
    void timerCallback() override
    {
        levelMeter.setLevel(engine.getPeakLevel(pluginId));

        if (engine.consumeMidiActivity(pluginId))
            midiActivityIndicator.flash();
    }

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
    AudioLevelMeter levelMeter;
    MidiActivityIndicator midiActivityIndicator;
    juce::TextButton volumeLearnButton { "Learn" };
    juce::TextButton muteButton { "Mute" };
    juce::TextButton soloButton { "Solo" };
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
    : virtualKeyboard(engine.getVirtualKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    engine.addListener(this);

    addAndMakeVisible(virtualKeyboard);
    virtualKeyboard.setAvailableRange(36, 96); // C2 a C7 - faixa generosa pra maioria dos usos
    virtualKeyboard.setOctaveForMiddleC(4);

    // Habilita o teclado do computador como controlador MIDI - ver
    // keyStateChanged() logo abaixo, que escuta as teclas e injeta as notas
    // no mesmo MidiKeyboardState do teclado desenhado na tela.
    setWantsKeyboardFocus(true);
    grabKeyboardFocus();

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

    addAndMakeVisible(exclusiveButton);
    exclusiveButton.setClickingTogglesState(true);
    exclusiveButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::cyan);
    exclusiveButton.setToggleState(engine.isExclusiveSoloEnabled(), juce::dontSendNotification);
    exclusiveButton.onClick = [this]
    {
        engine.setExclusiveSoloEnabled(exclusiveButton.getToggleState());
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

bool MainComponent::keyStateChanged(bool /*isKeyDown*/)
{
    // Nota MIDI 36 = C2 (com setOctaveForMiddleC(4) já configurado no
    // teclado virtual). Canal 1 e velocidade fixa, já que o teclado do
    // computador não tem sensibilidade de toque.
    constexpr int computerKeyboardBaseNote = 36;
    constexpr int computerKeyboardChannel = 1;
    constexpr float computerKeyboardVelocity = 0.85f;

    auto& keyboardState = engine.getVirtualKeyboardState();

    // keyStateChanged() é chamado sempre que QUALQUER tecla muda de estado,
    // sem dizer qual - por isso precisamos varrer todas as teclas mapeadas
    // e comparar com o que sabíamos estar pressionado no ciclo anterior,
    // pra saber quais realmente mudaram (e disparar Note On/Off só nelas).
    for (const auto& [character, offset] : getComputerKeyboardOffsets())
    {
        const bool isDown = juce::KeyPress::isKeyCurrentlyDown((int) character);
        const bool wasDown = computerKeysCurrentlyDown.count(character) > 0;

        if (isDown && !wasDown)
        {
            computerKeysCurrentlyDown.insert(character);
            keyboardState.noteOn(computerKeyboardChannel, computerKeyboardBaseNote + offset,
                                 computerKeyboardVelocity);
        }
        else if (!isDown && wasDown)
        {
            computerKeysCurrentlyDown.erase(character);
            keyboardState.noteOff(computerKeyboardChannel, computerKeyboardBaseNote + offset, 0.0f);
        }
    }

    return false; // não consome o evento - outros atalhos continuam funcionando normalmente
}

//==============================================================================
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
        exclusiveButton.setBounds(row.removeFromRight(90));
        row.removeFromRight(6);
        loadPluginButton.setBounds(row);
    }
    area.removeFromTop(12);

    auto loadedLabelArea = area.removeFromTop(24);
    loadedLabelArea.removeFromLeft(4);

    statusLabel.setBounds(area.removeFromBottom(24));
    area.removeFromBottom(4);

    // Teclado virtual: fixo embaixo, largura total, altura suficiente pra
    // ser clicável com conforto. Fica sempre visível, abaixo da área rolável
    // de plugins - assim dá pra tocar mesmo com muitas colunas abertas.
    virtualKeyboard.setBounds(area.removeFromBottom(90));
    area.removeFromBottom(8);

    loadedPluginsViewport.setBounds(area.removeFromTop(520));
    area.removeFromTop(8);

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

void MainComponent::exclusiveSoloChanged()
{
    // Ligar/desligar o modo Exclusive pode ter mudado qual plugin está em
    // solo (ver PluginHostEngine::setExclusiveSoloEnabled) - sincroniza
    // todas as linhas, mesmo motivo do pluginRouteChanged.
    for (auto& row : pluginRows)
        row->refreshRoute();

    exclusiveButton.setToggleState(engine.isExclusiveSoloEnabled(), juce::dontSendNotification);
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
