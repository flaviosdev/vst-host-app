# VST Host App

Host simples de plugins VSTi (VST3) em C++, usando o framework JUCE.
Suporta:

- Reconhecimento e seleção de dispositivos MIDI conectados
- Carregamento de plugins VST3 e execução em tempo real
- Interface gráfica do plugin embutida
- Gerenciamento de presets: programas de fábrica do plugin + presets
  próprios salvos em disco
- Saída de áudio via driver ASIO (baixa latência)

## Pré-requisitos

1. **Visual Studio 2022** com a carga de trabalho "Desenvolvimento para
   desktop com C++".
2. **CMake** 3.22 ou superior (o instalado junto com o VS 2022 serve).
3. **ASIO SDK** da Steinberg, já baixado. Anote o caminho da pasta raiz,
   por exemplo: `C:/SDKs/asiosdk_2.3.3_2019-06-14`.
   - Essa pasta precisa conter as subpastas `common`, `host` e `host/pc`.
4. **VST3 SDK**: não precisa ser referenciado manualmente — o JUCE já
   inclui internamente o necessário para hospedar (não criar) plugins
   VST3. Você só precisa dele separadamente se um dia quiser also
   *criar* plugins VST3 com o JUCE.
5. **JUCE**: duas opções:
   - Deixe o CMake baixar automaticamente (usa `FetchContent`, exige
     internet na hora de configurar o projeto), **ou**
   - Clone você mesmo dentro da pasta do projeto:
     ```
     git clone --branch 7.0.12 https://github.com/juce-framework/JUCE.git
     ```
     ficando em `VSTHostApp/JUCE`.

## Compilando

Na pasta do projeto (`VSTHostApp`), abra um "Developer PowerShell for VS 2022"
e rode:

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64 -DASIO_SDK_PATH="C:/SDKs/asiosdk_2.3.3_2019-06-14"
cmake --build build --config Release
```

O executável final fica em:

```
build/VSTHostApp_artefacts/Release/VST Host App.exe
```

Você também pode abrir a pasta `build` diretamente no Visual Studio
(arquivo `VSTHostApp.sln`) e compilar/depurar normalmente pela IDE.

## Usando o app

1. Abra o programa e clique em **"Configuracoes de Audio/MIDI..."**.
   - Na aba de saída de áudio, selecione seu driver **ASIO**.
   - Marque os dispositivos **MIDI** que você quer usar (teclado, controlador, etc.).
2. Clique em **"Carregar Plugin VST3..."** e selecione o arquivo/pasta `.vst3`
   do instrumento que deseja tocar.
3. Clique em **"Abrir Interface do Plugin"** para ver a UI própria do plugin
   (opcional — o áudio/MIDI já funcionam mesmo sem abrir a interface).
4. Toque no seu teclado/controlador MIDI: as notas vão direto para o plugin
   e saem pela interface de áudio selecionada.
5. Gerencie presets:
   - **Programas de fábrica**: lista os presets internos do próprio plugin
     (o combo "Programas de fabrica").
   - **Presets salvos**: seus próprios snapshots do estado do plugin.
     Use "Salvar como...", "Carregar" e "Excluir". Ficam gravados em:
     `%APPDATA%/VSTHostApp/Presets/<nome do plugin>/`.

## Próximos passos possíveis

- Adicionar suporte a múltiplos plugins em cadeia (efeitos após o instrumento).
- Adicionar um teclado virtual na tela para tocar sem hardware MIDI.
- Exportar/importar presets como arquivos `.fxp` compatíveis com outros hosts.
- Salvar a última configuração de áudio/MIDI/plugin usada (sessão) em um
  arquivo de projeto próprio.
