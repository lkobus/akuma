# Widescreen nativo e descoberta

A edição inglesa do CD entra em 640×480. O executável passa essas dimensões
para o DirectDraw na inicialização (`0x41C303` e `0x41C308`) e volta a usá-las
ao restaurar o modo de vídeo (`0x421B3A` e `0x421B3F`). Também cria uma
superfície auxiliar desse tamanho (`0x41C944` e `0x41C949`). Há laços que
avançam 1280 bytes por linha no desenho de 16 bits, como em `0x40EEE5`, e
a área clicável do HUD também usa limites em pixels, como em `0x40E55F`.
Os endereços valem para a revisão conferida pelo build.

No Slayers, `native/slayers_mod/src/display.c` configura largura e altura.
O `Slayers-og.exe` usa essas dimensões para construir a área de jogo
em `0x40466D` e `0x404692`, com recuos de 202 e 70 pixels na vista padrão.
No Akuma, a apresentação 16:9 copia a superfície lógica original para a
composição do HUD. O cache original desenha 448×448, conforme a rotina
em `0x45BDD1`. O modo de câmera aberta aproveita a faixa restante da
superfície de 640×480 e uma segunda superfície com o mesmo pitch para
renderizar a cena por faixas com até 1088×696 pixels visíveis de terreno.

## Mapas pequenos

O ajuste do Slayers limita a área visível às dimensões do mapa em pixels e
recalcula o enquadramento ao trocar de cenário. No Akuma, o scroll e
os cliques no minimapa limitam a câmera a `0…max(0, mapa − área visível)`.
O HUD verifica as dimensões do mapa antes de ler o terreno à direita.
A câmera inicia em 21 colunas. F8 percorre 27, 30 e 34 colunas e volta
à vista inicial. Se o mapa ou a superfície não comportarem o zoom escolhido,
ela usa a vista que cabe. Abaixo de 19 colunas, usa a largura padrão de 14.

No Akuma deste CD, Mantis Village usa `t2.map` com 80×54 tiles. O mapa de
38×44 tiles é Stone Village (`t3.map`), com 1216×1408 pixels. O carregador
em `0x457450` lê largura e altura dos dois primeiros bytes do arquivo para
os campos `map+0x10` e `map+0x11`. A grade usa linhas de 128 tiles.

Usar Stone Village para o caso de mapa estreito e Mantis Village para
confirmar que a área maior é restaurada. Conferir cliques nas bordas, itens
no chão, indicadores do HUD e os presets 1600×900 e 1920×1080.

## Descoberta e opções

O recorte da renderização, a área revelada no minimapa e o que fica registrado
como explorado precisam ser verificados individualmente no executável. O
viewport maior pode exigir um recorte maior para desenhar o minimapa. Qualquer
mudança no alcance que revela tiles deve respeitar as bordas do mapa e
preservar os efeitos dos bônus de visão. Um teste deve entrar em Stone Village
com uma partida nova, explorar a borda, salvar e conferir o minimapa ao carregar.

O menu Options já é a entrada para os controles originais do jogo. Quando o
motor puder reconstruir as superfícies em outro tamanho, o menu pode compartilhar
o preset de resolução com o launcher. Os filtros do wrapper hoje são lidos
na abertura e precisam de reinicialização do renderizador para mudar em jogo.

## Barra direita

O minimapa flutua sobre o cenário e pode ser arrastado. Os três retratos ficam
empilhados na coluna direita, com os atalhos de personagem, itens, missão,
mapa e menu na divisa. Cada controle reutiliza as coordenadas originais
para ativar o comportamento da campanha.

Tiles, personagens, retratos e ícones mantêm as dimensões originais na
superfície lógica. O build ajusta a textura de pedra e o rodapé de madeira
no espaço adicional. Ao mover o mouse para a margem da área de jogo,
o HUD encaminha a posição correspondente à borda do quadro de 640×480.
A câmera usa os limites nativos do mapa e a campanha usa a descoberta
original.

O minimapa original desenha um pixel por tile no retângulo de 128×128
com origem em (488, 35). A rotina em `0x459150` centraliza o mapa
usando as dimensões dos campos `+0x10/0x11`. O HUD ajusta esse
retângulo à moldura, mantendo a proporção, ou recorta em volta da câmera
no modo de detalhe. O clique percorre a transformação inversa até o
manipulador original de `0x469E20`.

A câmera de 21 colunas mantém a escala original. As vistas de 27, 30 e
34 colunas compõem mais terreno nos dois eixos e reduzem a cena na apresentação.
A escala dos retratos, atalhos e minimapa segue a do HUD.
O cache original continua desenhando as primeiras 14 colunas.
Os ganchos em `0x467539` e `0x467589` desenham as camadas da faixa extra.
O recorte de sprites usa `0x48F478` durante essa passagem. Assim que o
último grupo de entidades termina, em `0x4675AE`, o patch guarda a faixa
e restaura os pixels que a interface usa. O minimapa original é desenhado
em seguida para fornecer seus pixels ao minimapa flutuante.
Os marcadores desenhados junto com as entidades, antes dessa cópia,
são mantidos separadamente nas chamadas de `0x458F59` e `0x458F63`.
As colunas e linhas adicionais são desenhadas em uma superfície separada.
Durante o desenho dos sprites, a origem é deslocada por faixa e depois
restaurada antes da atualização da interface. Seleção e cliques no
cenário usam a projeção inversa da imagem.

A descoberta em `0x45A520` acompanha os dois eixos da câmera e é
limitada pelo tamanho do mapa. Isso mantém a verificação de visibilidade
que o jogo usa ao selecionar uma saída. O desenho em `0x4508A0` mantém
os marcadores amarelos das saídas em todos os quadros do HUD lateral.

Os botões laterais usam os sprites e os estados do jogo, capturados nas
cinco chamadas de desenho entre `0x46AA25` e `0x46AB06`. O ponteiro segue
as coordenadas recebidas do wrapper e é desenhado na composição final.
A posição encaminhada ao motor continua usando o espaço de coordenadas do
controle escolhido. Durante a campanha com HUD lateral, o scroll e
o arraste usam a captura da janela do cnc-ddraw, sem aplicar o retângulo
interno de seleção do jogo de 640×480.

Os arquivos `.map` têm duas referências de 16 bits por célula. As rotinas
de terreno do Akuma compõem as duas camadas no cache e também desenham
parte da segunda camada sobre a cena. O patch preserva a ordem desses
desenhos e copia os pixels dos tiles de cima somente quando o valor RGB565
é diferente de zero. Isso mantém o chão visível sob pedras, telhados
e outras partes transparentes do arquivo `.Til`.
