# Análise do vídeo e plano de ação — entrada do declive

## Escopo

Vídeo analisado:
`Yabause v0.9.14 2026-08-02 22-23-25.mp4`, 32,881 s, 640×528,
aproximadamente 29,3 quadros efetivos por segundo.

Esta análise começou como diagnóstica. Após autorização, a contenção descrita nas
fases 1 e 2 foi implementada e compilada.

> **Implementação Saturn-safe atual:** a flag `PHYS_WHEEL_STRICT_SURFACE=1`
> permanece ativa, mas o runtime usa somente dois probes centrais de eixo. Os
> dois precisam ser válidos; numa costura, o último suporte é preservado. A
> telemetria persistente e o consenso de quatro rodas foram retirados após
> causarem regressão no orçamento disponível para pista/background.

## Linha do tempo observada

- 0–13 s: reta; carro estável e apoiado.
- 13–15,2 s: entrada da curva/declive; suporte ainda coerente.
- ~15,4 s: primeiro mergulho parcial no asfalto; recuperação até ~15,6 s.
- ~19,4 s: novo mergulho curto; recuperação no intervalo seguinte.
- ~23,2–23,8 s: evento mais grave; carro e câmera atravessam o plano, revelando
  a face inferior/branca da pista; recuperação por volta de 24,0 s.
- ~25,4–25,8 s: nova penetração; recuperação por volta de 26,0 s.
- 27–32 s: retorno a suporte visualmente estável.

O padrão é descontínuo, curto e reversível. Não se comporta como uma escada
geométrica fixa: uma laje real produziria uma mudança persistente de altura. O
vídeo mostra uma decisão de contato inválida que é substituída poucos frames
depois.

Folhas de contato geradas durante a análise:

- `BuildDrop/video_analysis_20260802/timeline_1fps.png`
- `BuildDrop/video_analysis_20260802/detail_13_20.png`
- `BuildDrop/video_analysis_20260802/detail_20_27.png`

## Correlação com o código atual

### 1. Fallback extrapola planos fora da face

`FindSurfaceYByFamilySet` testa se o XZ está dentro dos dois triângulos do quad.
Quando não está, calcula mesmo assim o Y do plano e classifica o resultado como
fallback. Esse valor pode ficar muito distante do asfalto real quando o probe
passa por uma costura, triângulo fino ou quad não planar.

### 2. Aceite do fallback é permissivo

`TryProbeSurfaceY` tenta a consulta estrita, mas, se ela falhar, aceita a consulta
soft quando o segmento retornado tem afinidade com o seed. Não existe ali limite
de distância até a borda, limite de salto vertical ou continuidade de face.

### 3. Uma amostra isolada pode mover o chassi

`HasProbeSupport` considera um eixo/lado válido quando apenas uma das duas rodas
possui hit. Depois, todas as rodas válidas são promediadas. Um único fallback
errado pode, portanto, deslocar `surfaceYTarget` e o centro do carro.

### 4. O snap amplifica o erro de consulta

Uma diferença descendente de apenas 0,5 unidade pode copiar imediatamente o
target para `surfaceYFiltered`. A recuperação seguinte também pode ser brusca
pela proteção anti-penetração. Isso explica a sequência vídeo: normal → abaixo
da pista → normal em poucos frames.

### 5. Cache e seed não representam continuidade topológica

Existe um único cache de face para todas as consultas e um seed de segmento, mas
não existe identidade persistente de suporte por roda nem relação face→vizinha.
Proximidade de segmento não impede a seleção de outra camada no mesmo XZ.

## Hipóteses priorizadas

1. **Alta:** fallback de plano externo produz Y inválido numa borda; o snap aplica
   esse Y no mesmo frame.
2. **Alta:** ausência de consenso entre rodas permite que um outlier determine a
   altura do chassi.
3. **Média/alta:** seed/cache troca entre faces ou camadas durante slide da janela
   de segmentos.
4. **Média:** quad não planar/triangulação do runtime diverge da superfície visual
   em pontos específicos.
5. **Baixa para 27–42:** desnível real entre segmentos; a auditoria offline mediu
   as 15 junções principais como soldadas.

## Plano de ação

### Fase 1 — observabilidade orientada a eventos

1. Criar um buffer circular pequeno, sem `Print` por frame.
2. Registrar somente quando ocorrer uma destas condições:
   - mudança de segmento/face de uma roda;
   - strict miss seguido de soft hit;
   - `|targetY - filteredY| > 0,25`;
   - menos de três rodas válidas;
   - mudança de componente/camada;
   - snap vertical.
3. Cada evento deve conter frame, X/Y/Z, velocidade, segmento/face, máscara das
   rodas, Y de FL/FR/RL/RR, strict/soft e número de faces varridas.
4. Exibir no HUD apenas o último evento congelado, evitando custo e ilegibilidade.
5. Reproduzir a mesma rota e correlacionar com 15,4; 19,4; 23,2 e 25,4 s.

### Fase 2 — teste mínimo de contenção

1. Adicionar uma flag A/B que desabilita fallback soft somente nos quatro probes
   de roda; não alterar ainda a consulta de grip ou contato auxiliar.
2. Em strict miss, manter por poucos frames o último plano válido conectado.
3. Proibir snap quando o target veio de fallback ou de menos de três rodas.
4. Resultado esperado:
   - o carro pode flutuar minimamente numa costura;
   - jamais deve mergulhar ou atravessar a pista.
5. Se os quatro eventos desaparecerem, a causa fica comprovada antes de qualquer
   refatoração maior.

Bitfield `ev` exibido no HUD por 30 frames após um evento:

- `1`: topology drop;
- `2`: pelo menos um strict miss;
- `4`: soft fallback usado (esperado somente no A/B desligado);
- `8`: consenso insuficiente;
- `16`: alturas das rodas incoerentes;
- `32`: último suporte preservado;
- `128`: modo strict-only ativo.

Exemplos: `128` = normal/strict; `162` = strict miss + suporte preservado;
`170` = strict miss + consenso insuficiente + suporte preservado.

### Fase 3 — fallback geometricamente seguro

Caso o fallback ainda seja necessário:

1. Aceitar projeção externa somente dentro de uma tolerância de borda pequena.
2. Exigir normal dirigível e família/tipo compatível.
3. Limitar o delta vertical pela distância XZ percorrida e pelo grade máximo:
   `|ΔY| <= |ΔXZ| * maxGrade + margem`.
4. Rejeitar faces que atravessem o plano de suporte anterior ou estejam em outra
   camada conectada.
5. Nunca atualizar cache persistente com um fallback não confirmado.

### Fase 4 — consenso robusto das rodas

1. Separar `hit válido` de `hit utilizável para o chassi`.
2. Com quatro hits, ajustar um plano e rejeitar o ponto com maior resíduo se ele
   exceder a tolerância.
3. Com três hits coerentes, resolver o plano normalmente.
4. Com dois hits, aceitar apenas quando pertencem ao mesmo eixo/componente e são
   coerentes com o último plano.
5. Com zero/um hit, conservar temporariamente o último suporte; não recalcular o
   centro pela única amostra.
6. Aplicar snap anti-penetração somente depois dessa validação.

### Fase 5 — continuidade face→face

1. Estender o analisador offline para gerar adjacência das faces dirigíveis,
   inclusive entre segmentos.
2. Manter face/componente de suporte por eixo ou roda.
3. Testar primeiro a face atual e suas vizinhas; busca ampla somente na perda real
   de contato.
4. Avaliar `planeY(X,Z)` diretamente na face conectada. Em faces soldadas, o Y é
   contínuo por construção, sem predição acumulada.
5. Manter pista principal, pit e escape como componentes distintos onde houver
   superfícies empilhadas.

### Fase 6 — posição, atitude e câmera

1. Centro físico do chassi usa somente o plano de suporte validado.
2. Pitch/roll usam o plano robusto das rodas.
3. Filtrar rotação e suspensão visual, não esconder contato incorreto com lerp de Y.
4. Câmera usa um anchor suavizado derivado do chassi validado e nunca participa da
   escolha da superfície.

## Estratégia de entrega segura

1. Telemetria/eventos — passiva.
2. Flag A/B `strict-only` para provar a causa.
3. Gate de snap por qualidade do contato.
4. Consenso robusto de rodas.
5. Adjacência offline e tracker passivo.
6. Substituição gradual: centro/2 eixos primeiro, 4 rodas somente após medir custo.

Cada etapa deve gerar uma ISO testável e pode ser revertida isoladamente.

## Validação da implementação Saturn-safe

- Compilação e link SH2: aprovados.
- ISO/CUE: gerados em `BuildDrop`.
- Cabeçalhos passivos do game loop: aprovados.
- Cabeçalhos de observabilidade: aprovados.
- Testes host de reutilização de frame: aprovados.
- Custo de consulta: 2 probes de eixo no lugar de 4 probes de roda; contando a
  amostra central já existente, são 3 acessos de superfície no lugar de 5 por
  atualização. Com `strict-only`, um miss também deixa de executar o fallback
  soft adicional.
- Estado/telemetria adicionados na tentativa anterior foram removidos do caminho
  crítico. O ELF diminuiu 64 bytes e a ISO diminuiu um setor (2.048 bytes).
- Validador de tamanho da ISO: a imagem atual possui 16.023.552 bytes, mas o
  baseline antigo do script ainda exige 4.134.912 bytes. A compilação, o link e a
  geração da imagem terminaram antes dessa divergência; ela decorre do conjunto
  atual de assets e não do filtro de contato.

### Teste visual dirigido

1. Repetir a rota do vídeo, especialmente aproximadamente 15,4 s, 19,4 s,
   23,2–23,8 s e 25,4–25,8 s.
2. Confirmar desde a largada que pista, segmentos e background voltaram a ser
   renderizados; `td` voltou a representar somente topology drop.
3. O aceite desta etapa é o desaparecimento dos mergulhos. Uma pequena suspensão
   visual momentânea na costura é aceitável e identifica onde a fase de
   continuidade face→face deve atuar.

## Critérios de aceite

- Zero frames com o centro do carro abaixo do plano de suporte.
- Zero soft fallback aceito além da tolerância de borda.
- Zero snap originado por uma única roda ou contato não confirmado.
- Nenhuma troca de componente/camada na rota do vídeo.
- `Y` acompanha `planeY(X,Z)` continuamente nas costuras 27–42.
- Sem aumento de consultas globais; faces examinadas por frame igual ou menor que
  o baseline.
- Câmeras 2 e 3 permanecem acima da pista durante toda a captura.
- Build/link SH2, headers, testes host e comparação visual lado a lado aprovados.
