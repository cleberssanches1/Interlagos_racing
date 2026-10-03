# Plano de ação — iluminação variada do circuito

## 1. Objetivo

Adicionar iluminação variada e suavemente interpolada à pista e ao cenário do
circuito, gerada offline pelo `build_all_nya_geo_mat.ps1` e consumida pelo jogo
com custo previsível no Sega Saturn.

O resultado deve aproximar o comportamento visual observado no F1 1995:

- trechos claros e sombreados com transições graduais;
- asfalto, grama, zebras e áreas de escape integrados ao mesmo ambiente;
- continuidade entre segmentos e entre LODs;
- ausência de mudança de brilho quando segmentos entram ou saem da câmera;
- preservação do sort atual da pista e do carro;
- nenhuma dependência do `slPutPolygonX` para desenhar a pista.

## 2. Diagnóstico que orienta o plano

O runtime atual configura uma luz direcional, mas remove a iluminação antes do
desenho:

- `main.cxx` mantém `enableSmoothLighting = false`;
- `track_renderer.hpp` remove `UseLight`, `UseGouraud` e `CL_Gouraud`;
- `mesh_renderer.cxx` envia modelos smooth pelo caminho plano;
- a pista é submetida por `slPutPolygon` sem tabela Gouraud ativa.

A tentativa anterior com `slPutPolygonX` foi instável porque a pista segmentada
e o carro compartilhavam o estado e o pool Gouraud do SGL. A quantidade de
faces processadas mudava conforme a janela visível, alterando o sombreamento do
carro.

O exemplo `Samples/VDP1 - 3D - Smooth teapot` demonstra a API de iluminação do
SGL, mas pressupõe um modelo único e uma quantidade estável de polígonos. Essa
arquitetura não deve ser transplantada diretamente para o streaming da pista.

## 3. Decisão de arquitetura

### 3.1 Pista

A pista usará iluminação Gouraud pré-calculada por canto de face:

1. O `build_all` calcula quatro intensidades para cada face.
2. As intensidades são armazenadas no RDR como índices de 0 a 31.
3. O jogo converte os índices por uma rampa RGB555 global.
4. Os quatro valores RGB555 são copiados para uma região reservada da tabela
   Gouraud do VDP1.
5. A pista continua sendo desenhada por `slPutPolygon`.

O runtime não recalculará normais nem iluminação da pista a cada quadro.

### 3.2 Carro

O carro não compartilhará a região Gouraud da pista. A faixa atual iniciada em
`kCarGouraudOffset = 4096` será preservada e formalizada. A reintrodução de
iluminação no carro será uma etapa posterior e isolada.

### 3.3 Regiões iniciais da tabela VDP1

Proposta inicial, sujeita à validação de endereços:

| Região | Entradas | Uso |
|---|---:|---|
| Pista | `0..2047` | faces visíveis da pista |
| Reserva | `2048..4095` | sprites, testes e crescimento |
| Carro | `4096..8191` | carro e outros modelos móveis |

Cada entrada contém quatro cores RGB555 e ocupa 8 bytes. O código deve usar
funções explícitas para converter índice de entrada em endereço `ATTR.Gouraud`
e em posição no ponteiro retornado por `VDP1::GetGouraudTable()`.

## 4. Perfil de iluminação offline

Criar `tools/track_lighting_profile.json` com versão explícita e os seguintes
grupos:

```json
{
  "version": 1,
  "enabled": true,
  "sunDirection": [0.35, -0.15, 0.35],
  "ambient": 0.48,
  "diffuse": 0.42,
  "gamma": 1.1,
  "minimumLevel": 5,
  "maximumLevel": 27,
  "surfaceGain": {
    "asphalt": 0.88,
    "escape_area": 0.94,
    "grass": 0.82,
    "unknown": 0.90
  },
  "zones": []
}
```

Os valores acima são ponto de partida, não calibração final.

As zonas devem permitir sombras e variações amplas sem editar o código:

- nome estável;
- forma em coordenadas mundiais ou intervalo de segmentos;
- multiplicador de intensidade;
- largura de transição (`feather`);
- prioridade quando houver sobreposição;
- opção de afetar somente famílias ou tipos de superfície específicos.

As transições devem usar interpolação suave, nunca troca binária entre claro e
escuro.

## 5. Atualizações no `build_all`

### Fase B1 — gerador passivo de iluminação

Criar `tools/generate_track_baked_lighting.py`.

Entradas:

- `segments_map.json` recém-regenerado;
- `track_lighting_profile.json`;
- todos os `S###.SDR` high;
- todos os `S###L.SDR` low, quando existirem.

Saídas intermediárias:

- `S###.LIT` para a geometria high;
- `S###L.LIT` para a geometria low;
- `TLIT.BIN` com a rampa global de 32 cores RGB555;
- `track_lighting_report.json` com cobertura, mínimos, máximos e costuras.

Formato proposto para cada entrada de face no `.LIT`:

```text
uint8 corner0;
uint8 corner1;
uint8 corner2;
uint8 corner3;
```

Isso acrescenta apenas 4 bytes por face antes do alinhamento.

### Fase B2 — cálculo das intensidades

Para cada vértice/canto:

```text
nível = ambiente
      + diffuse * max(0, normal_do_vértice · direção_do_sol)
      + zona_de_luz_ou_sombra
      + ganho_visual_da_superfície
```

O gerador deverá:

1. Ler todas as faces antes de produzir qualquer segmento.
2. Agrupar vértices coincidentes por posição mundial quantizada.
3. Calcular normal por vértice a partir das faces compatíveis.
4. Não suavizar entre chão e parede.
5. Respeitar limite angular e família/tipo de superfície ao acumular normais.
6. Aplicar zonas no espaço mundial, para que high e low recebam a mesma luz.
7. Quantizar o resultado em 32 níveis.
8. Atribuir o mesmo nível a vértices soldados nas fronteiras.

As zonas de sombra são necessárias porque `normal · luz` sozinho deixa o
asfalto plano praticamente uniforme.

### Fase B3 — extensão compatível do RDR1

Não substituir imediatamente o formato inteiro por RDR2. Utilizar a extensão
opcional já permitida pelo cabeçalho RDR1:

- `HeaderV1.flags`: adicionar `kHasBakedLighting`;
- `reserved0`: offset do bloco de iluminação;
- `reserved1`: número de entradas de iluminação;
- bloco após `familyIds`: quatro índices de intensidade por face.

Atualizar `generate_segment_runtime_draw.ps1` para:

1. exigir o `.LIT` correspondente quando a iluminação estiver habilitada;
2. validar `segmentId` e `faceCount`;
3. anexar as entradas ao RDR;
4. definir flag, offset e contagem;
5. falhar quando houver arquivo antigo, incompleto ou incompatível.

O `generate_track_runtime_pack.ps1` continuará empacotando o RDR completo. Ele
deverá apenas validar a extensão e publicar no log o total de bytes de luz.

### Fase B4 — integração no `build_all_nya_geo_mat.ps1`

Adicionar uma etapa antes da geração dos RDR:

```text
Etapa 3.55/7: Gerar iluminação Gouraud offline (high + low)
```

Ordem proposta:

1. reconstruir `segments_map.json` e tipos de solo;
2. gerar GEO/MAT/SDR;
3. remover todos os `.LIT` antigos do diretório temporário;
4. gerar iluminação high e low em uma única leitura global;
5. validar o relatório;
6. gerar RDR com a extensão;
7. gerar `TRKRDR.BIN` e `TRKRDRL.BIN`;
8. copiar `TLIT.BIN` para `cd/data`;
9. validar presença e data de todos os artefatos.

Regra obrigatória: assim como os mapas de textura e solo, a iluminação deverá
ser sempre regenerada a cada execução do `build_all`. Não haverá reutilização
silenciosa de `.LIT` ou `TLIT.BIN` anteriores.

### Fase B5 — validações offline

O build deverá interromper quando ocorrer qualquer condição:

- segmento SDR sem LIT;
- face count diferente;
- segmento high ou low sem cobertura;
- nível fora de `0..31`;
- offset do bloco fora do RDR;
- diferença de iluminação acima da tolerância em vértices soldados;
- zona do manifesto sem nenhuma face atingida;
- relatório ou rampa global ausente;
- artefato anterior à execução atual.

Executar duas gerações consecutivas e confirmar hashes idênticos para validar
determinismo.

## 6. Atualizações na aplicação

### Fase A1 — contratos e leitura passiva

Atualizar:

- `src/segment_runtime_draw_format.hpp`;
- `src/segment_runtime_draw_loader.hpp`;
- validadores de formato e testes host.

Adicionar:

- flag de iluminação;
- tipo `BakedLightEntry` de 4 bytes;
- validação opcional de offset e contagem;
- leitura de `TLIT.BIN`;
- fallback para RDR sem iluminação.

Nesta fase os dados serão carregados e auditados, mas não utilizados no desenho.

### Fase A2 — componente de tabela Gouraud

Criar um componente pequeno, fora de `game_loop_system.hpp`, responsável por:

- reservar a região `0..2047` da pista;
- atribuir um bloco a cada renderer/segmento residente;
- preparar as quatro cores de cada face em WRAM;
- marcar faixas sujas quando a janela de segmentos mudar;
- copiar somente faixas sujas para VDP1 em ponto sincronizado;
- manter a submissão final e a escrita VDP1 no Master SH2;
- recusar qualquer faixa que alcance a região do carro.

Não fazer alocações dinâmicas transitórias por face dentro do frame.

### Fase A3 — ativação nos atributos da pista

Quando um segmento possuir iluminação válida:

```text
ATTR.Sort     = sort_atual | UseGouraud
ATTR.Display  = display_atual | CL_Gouraud
ATTR.Gouraud  = 0xe000 + entrada_reservada + índice_da_face
```

Devem permanecer inalterados:

- `SORT_MAX` da pista;
- textura e banco de paleta;
- flags de flip/direção;
- visibilidade de face;
- classificação de solo;
- caminho `slPutPolygon`.

Se não houver iluminação válida ou espaço na tabela, o segmento usa o caminho
flat atual. Isso é fallback, não condição fatal de boot.

### Fase A4 — ativação gradual

Introduzir `TRACK_BAKED_LIGHTING` inicialmente desligado.

Sequência de ativação:

1. apenas segmento inicial high;
2. janela completa high;
3. geometria low usada pelo LOD1;
4. LOD2;
5. volta completa com streaming e troca de LOD.

Depois da validação, tornar o recurso padrão e manter uma opção de diagnóstico
para comparar `flat` e `baked`.

### Fase A5 — iluminação do carro

Somente depois da pista permanecer estável:

1. manter a região do carro a partir da entrada 4096;
2. usar as normais do `SmoothMesh` do carro;
3. transformar a luz mundial para o espaço local do carro;
4. atualizar apenas as entradas do carro;
5. desenhar por `slPutPolygon` com Gouraud pré-preparado;
6. não restaurar `slPutPolygonX` no fluxo principal.

## 7. Compatibilidade com os LODs

O pipeline atual possui dois conjuntos físicos de geometria:

- high (`TRKRDR.BIN`) para LOD0;
- low (`TRKRDRL.BIN`) compartilhado pelos níveis mais distantes.

O gerador deve calcular ambos na mesma execução e com o mesmo perfil mundial.
Assim, geometrias com números de faces diferentes amostram a mesma função de
luz, evitando mudança de tom na transição.

Para validar LOD2 será necessário usar uma configuração com
`TRACK_LOD2_SEGMENTS > 0`; o valor atual do `makefile` é zero.

## 8. Calibração visual

Antes da volta completa, criar uma cena de calibração com quatro faces:

- nível mínimo;
- sombra média;
- nível neutro;
- nível máximo.

Isso confirmará como os valores RGB555 da tabela Gouraud alteram texturas RGB e
paletizadas no VDP1. A rampa final só será fechada depois desse teste.

Primeira calibração sugerida:

- evitar preto absoluto no asfalto;
- limitar altas luzes para não estourar zebras e placas;
- deixar a grama menos luminosa que as zebras;
- usar transições com pelo menos dois segmentos de feather em zonas longas;
- calibrar o céu separadamente, sem usar o back color ciano máximo atual.

## 9. Telemetria necessária

Adicionar contadores compactos, sem aumentar permanentemente o HUD:

- faces iluminadas no frame;
- entradas Gouraud usadas e capacidade total;
- uploads de tabela por frame;
- bytes copiados para VDP1;
- segmentos com fallback flat;
- maior índice GRDA emitido;
- violações de região pista/carro.

Os contadores podem ser expostos no overlay existente somente em modo de debug.

## 10. Ordem de implementação e gates

### Entrega 1 — somente build

- perfil JSON;
- gerador `.LIT` e `TLIT.BIN`;
- extensão opcional do RDR;
- relatório e validadores;
- nenhuma mudança visual no jogo.

Gate: `build_all` completo, determinístico e com todos os segmentos cobertos.

### Entrega 2 — leitura passiva

- contratos C++;
- loader da extensão;
- telemetria de cobertura;
- render ainda flat.

Gate: stable build, validadores de headers e boot sem regressão.

### Entrega 3 — um segmento iluminado

- arena Gouraud da pista;
- upload sincronizado;
- segmento inicial high com `UseGouraud`;
- fallback imediato por flag.

Gate: carro e pista sem pulsação durante câmera parada e movimento curto.

### Entrega 4 — streaming high completo

- realocação quando a janela muda;
- atualização somente das faixas sujas;
- volta completa em LOD0.

Gate: nenhuma alteração de brilho causada pela entrada/saída de segmentos.

### Entrega 5 — low/LODs e calibração

- ativar low;
- habilitar configuração de teste com LOD2;
- ajustar zonas, superfície, gamma e rampa RGB555;
- comparar capturas com a referência do F1 1995.

Gate: transições de LOD e emendas sem degraus visíveis de iluminação.

### Entrega 6 — carro, sombra e ambiente

- região Gouraud isolada do carro;
- novo perfil do céu;
- revisão de `SBA.NYA`/sombra;
- ajuste final de contraste e saturação.

Gate: cena completa visualmente integrada e estável.

## 11. Validação técnica obrigatória

Após cada entrega que tocar runtime:

1. executar o validador estável do projeto;
2. recriar os diretórios de validação removidos pelo clean;
3. executar validadores de headers passivos e de observabilidade;
4. verificar o envelope da ISO;
5. testar boot e volta curta no emulador;
6. medir HWR, LWR, VDP1 VRAM e número de polígonos;
7. comparar capturas nos mesmos segmentos e câmeras;
8. testar entrada/saída de segmentos com o carro parado;
9. testar LOD0, LOD1 e LOD2 separadamente;
10. confirmar que o render final permanece no Master SH2.

## 12. Critérios de conclusão

O trabalho será considerado concluído quando:

- todo `build_all` recriar a iluminação sem arquivos antigos;
- todos os RDR high/low tiverem cobertura válida;
- a pista apresentar variação ampla e interpolada de luz;
- não houver cintilação ao mudar a janela visível;
- não houver interferência na iluminação do carro;
- emendas e trocas de LOD não apresentarem degrau tonal;
- o custo de upload permanecer dentro do orçamento medido;
- o jogo mantiver boot, áudio, física, sort e ISO estáveis;
- o modo flat continuar disponível como fallback de diagnóstico.

## 13. Arquivos previstos

### Novos

- `tools/track_lighting_profile.json`
- `tools/generate_track_baked_lighting.py`
- `tools/validate_track_baked_lighting.py`
- `src/track_gouraud_contracts.hpp`
- `src/track_gouraud_table.hpp`

### Alterados

- `tools/build_all_nya_geo_mat.ps1`
- `tools/generate_segment_runtime_draw.ps1`
- `tools/generate_track_runtime_pack.ps1`
- `src/segment_runtime_draw_format.hpp`
- `src/segment_runtime_draw_loader.hpp`
- `src/track_system.hpp`
- `src/track_system.cxx`
- `src/track_renderer.hpp`
- `src/main.cxx`
- validadores host e scripts de stable build aplicáveis

`game_loop_system.hpp` só deverá receber a chamada mínima de sincronização ou
configuração. A lógica e os buffers permanecerão nos novos componentes.
