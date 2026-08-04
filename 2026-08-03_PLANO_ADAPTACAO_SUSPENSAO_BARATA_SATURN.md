# Plano de implementação — contato e suspensão arcade de baixo custo no Sega Saturn

## Estado da implementação

Primeira fatia implementada em 03/08/2026:

- Fase 0: auditor offline criado e executado nos segmentos 22–42;
- 2.471 faces dirigíveis/floor-like verificadas;
- 4.182 bordas compartilhadas verificadas, sem desnível acima de 0,25 unidade;
- 36 mudanças de normal acima de 8 graus catalogadas para inspeção;
- Fase 1: contrato rico de contato, hint `(segmento, face)` separado por roda e confirmação de trocas verticalmente grandes integrados;
- mantidas duas consultas de solo por quadro;
- nenhuma alocação dinâmica adicionada ao hot path;
- Slave SH2 permanece com o produtor assíncrono da pista nesta etapa, pois o runtime possui uma única função/fila Slave e uma segunda submissão concorrente sobrescreveria esse trabalho;
- build SH2, testes host e validadores de headers aprovados;
- ISO cresceu um setor; `.bss` permaneceu inalterada.

Próxima fatia: integrar o solver de mola/gravidade atrás de `PHYS_CHASSIS_SPRING_V2`, mantendo a consulta e o produtor da pista nos seus proprietários atuais.

## 1. Objetivo

Fazer o carro acompanhar aclives, declives, cambagem e irregularidades da pista sem:

- descer as faces como uma escada;
- saltar ou inclinar violentamente ao trocar de face/segmento;
- atravessar o asfalto;
- transmitir ruído de cada triângulo para a câmera;
- aumentar o limite atual de duas consultas de solo por quadro no perfil Saturn;
- criar alocações por quadro ou pressionar Low/High Work RAM.

Não será implementado um motor de corpo rígido completo. A solução será um modelo arcade determinístico: contatos persistentes por roda, mola/amortecedor vertical e momentos simplificados de pitch/roll.

## 2. Diagnóstico do vídeo de 21:33:13

O trecho crítico começa aproximadamente aos 13 segundos. Entre cerca de 14 e 21 segundos aparecem quatro falhas distintas:

1. a altura da carroceria muda bruscamente na entrada da descida;
2. pitch e roll recebem correções grandes em poucos quadros;
3. algumas rodas perdem ou trocam o suporte de maneira incompatível com a face anterior;
4. quando a carroceria fica abaixo ou excessivamente inclinada, a câmera também cruza o plano do asfalto.

O problema não deve ser tratado somente com um filtro mais lento. Um filtro pode suavizar um contato correto, mas apenas atrasa um contato que escolheu a face errada.

## 3. Estado atual relevante

- O build usa `PHYS_SATURN_LOW_COST=1`.
- O perfil barato força o modo reduzido e realiza duas consultas por quadro, alternando as diagonais FL/RR e FR/RL.
- `ArcadeSuspensionState` já mantém alvo, altura filtrada, velocidade, segmento e idade para quatro rodas em até 64 bytes.
- O centro e o pitch podem receber a diagonal bruta do quadro, antes do filtro de contato.
- O plano completo das quatro rodas é publicado em ciclos de dois quadros.
- A consulta calcula a altura exata no triângulo, mas o hint/cache de última face é global no `TrackSystem`, e não individual por roda.
- O FSMAP filtra candidatos por segmento, AABB XZ e tipo de solo, permanecendo na Cart RAM.
- `CornerContactSolver` existe e possui testes, porém ainda não participa do fluxo do carro.
- O visual permite até 16 graus de pitch e 8 graus de roll em um único quadro, valores altos para um sinal que pode conter troca incorreta de face.
- A câmera usa grade/pitch derivados desse mesmo sinal; portanto, um erro de contato pode ser amplificado pela câmera.

## 4. Arquitetura-alvo

```text
FSMAP + geometria carregada
          |
          v
duas consultas exatas por quadro
          |
          v
4 contatos persistentes por roda
(altura, segmento, face, idade, compressão)
          |
          v
mola/amortecedor + gravidade arcade
          |
          +--> altura física da carroceria
          +--> pitch/roll físicos amortecidos
          +--> curso visual de cada roda
          |
          v
câmera com mola própria + guarda de terreno
```

Há três sinais separados:

1. **Contato geométrico:** resultado exato da face sob cada roda.
2. **Estado físico:** carroceria integrada por mola, amortecimento e gravidade.
3. **Estado visual/câmera:** versões estabilizadas do estado físico, sem consultar novamente a pista.

## 5. Restrições e orçamento

| Recurso | Limite do plano |
|---|---:|
| Consultas de solo | 2 por quadro no perfil Saturn |
| Alocações no hot path | 0 |
| Estado persistente total de contato/chassi | máximo de 96 bytes por carro |
| Trigonometria na física | 0 por quadro |
| Substeps | 1 inicialmente; no máximo 2 substeps matemáticos, sem novas consultas |
| Dados adicionais obrigatórios no FSMAP | 0 bytes na primeira implementação |
| Crescimento da ISO | no máximo 1 setor sem revisão explícita |
| Regressão de tempo da física Master SH2 | alvo menor que 10% sobre o baseline |

O FSMAP continuará na Cart RAM. A primeira implementação deve reutilizar seus AABBs e índices de face, sem duplicar vértices em Low/High Work RAM.

## 6. Fases de implementação

### Fase 0 — baseline reproduzível e auditor de malha

Criar `tools/analyze_track_surface_continuity.py`, inicialmente para os segmentos 22 a 42.

O script deverá:

- ler `S###.GEO` e os materiais correspondentes;
- considerar somente faces dirigíveis e floor-like;
- triangular os quads exatamente como o runtime: ABC e ACD;
- verificar faces degeneradas;
- localizar bordas coincidentes com alturas divergentes;
- detectar faces dirigíveis sobrepostas em XZ com diferença relevante de Y;
- medir mudanças de normal entre faces vizinhas;
- amostrar a linha central e duas linhas deslocadas pela meia bitola do carro;
- gerar JSON/CSV com segmento, face anterior/nova, Y, delta Y e normal;
- destacar automaticamente os maiores saltos dos segmentos 22–42.

Saídas propostas:

- `BuildDrop/physics_surface_audit/continuity_report.json`;
- `BuildDrop/physics_surface_audit/wheel_paths.csv`;
- resumo no terminal com os 20 maiores saltos.

Critério de saída: saber se o degrau vem da geometria exportada, de faces sobrepostas ou somente da seleção runtime.

Essa etapa não altera o jogo e não entra no build completo até possuir cache por hash/data dos GEOs, para não aumentar novamente o tempo de `build_all_nya_geo_mat.ps1`.

### Fase 1 — contrato de contato exato com persistência por roda

Criar um contrato pequeno `WheelSurfaceHit` contendo:

- `surfaceYRaw`;
- `segmentId`;
- `faceIndex`;
- tipo de solo;
- válido/interno à face.

Adicionar uma consulta específica para roda que aceite o hint `(segmentId, faceIndex)` da própria roda. A ordem de busca será:

1. testar a última face daquela roda;
2. testar candidatos do FSMAP no mesmo segmento;
3. testar os segmentos vizinhos permitidos;
4. retornar falha estrita, sem projetar para uma face que não contém o XZ da roda.

Regras de coerência temporal:

- uma face nova só substitui a anterior quando contém o XZ da roda;
- em sobreposição, escolher o Y mais próximo da altura prevista daquela roda, não simplesmente o mais próximo do Y da carroceria;
- manter o contato anterior por no máximo 1–2 quadros quando a roda está sobre uma borda e a nova consulta falha;
- nunca manter um contato antigo se a distância vertical exceder o curso máximo da suspensão;
- armazenar face e segmento separadamente para FL, FR, RL e RR;
- remover a dependência do cache global como fonte de coerência entre rodas.

O escalonamento continuará:

```text
quadro par:   FL + RR
quadro ímpar: FR + RL
```

As duas rodas não consultadas conservam o contato persistente com idade 1. Nenhum fallback global ou consulta adicional será permitido no caminho normal.

Arquivos principais:

- `src/interfaces.hpp`;
- `src/track_collision_query.hpp`;
- `src/track_system.hpp/.cxx`;
- novo helper passivo `src/wheel_contact_persistence.hpp`;
- `src/car_ground_follower.hpp`.

Critério de saída: em uma rampa planar subdividida em várias faces, os quatro contatos mantêm Y contínuo e face/segmento só mudam quando a roda cruza a borda correspondente.

### Fase 2 — mola, amortecedor e gravidade arcade

Substituir o posicionamento direto da carroceria por um único solver responsável por Y, pitch e roll. O solver atual e o `CornerContactSolver` não devem operar simultaneamente.

Para cada roda:

```text
compressão = clamp(comprimentoRepouso - distânciaÂncoraSolo,
                   0, cursoMáximo)

velocidadeCompressão = compressãoAtual - compressãoAnterior

forçaRoda = rigidez * compressão
          - amortecimento * velocidadeCompressão
```

Acumular em inteiros fixed-point:

```text
forçaVertical = soma(forçaRoda) + gravidade
momentoPitch  = forçasDianteiras - forçasTraseiras
momentoRoll   = forçasDireitas - forçasEsquerdas
```

Usar integração semi-implícita e limites assimétricos apenas como proteção, não como mecanismo normal de aderência.

Regras:

- roda sem contato produz zero força de mola;
- gravidade continua agindo quando há contato parcial ou nenhum contato;
- contato de uma única roda pode produzir pitch e roll progressivos;
- correção antipenetracão só atua acima de um limite duro;
- nenhuma troca normal de face pode fazer snap da carroceria para o alvo;
- o centro da diagonal atual pode corrigir deriva de longo prazo, mas não pode ignorar o filtro/persistência individual das rodas;
- `static_assert` garante o orçamento máximo do estado.

O `CornerContactSolver` existente deve primeiro ser convertido em helper puro e testado com sequências sintéticas. Somente depois será ligado ao `GroundState`, atrás de uma flag.

Flags propostas:

- `PHYS_WHEEL_CONTACT_V2`;
- `PHYS_CHASSIS_SPRING_V2`.

Critério de saída: uma sequência em que RR, RL, FR e FL tocam o solo em quadros diferentes deve produzir compressões e rotação progressivas, sem teleporte e sem atravessar o plano.

### Fase 3 — atitude da carroceria e movimento visual das rodas

O `CarWheelRig` deixará de recalcular a atitude diretamente das alturas brutas. Ele receberá:

- pitch físico filtrado;
- roll físico filtrado;
- compressão/curso das quatro rodas;
- máscara de contato.

Alterações:

- aplicar o curso individual à malha de cada roda;
- manter esterço e rotação do pneu independentes da suspensão;
- eliminar filtros duplicados que introduzem fase/atraso entre física e visual;
- começar com limites de passo conservadores, aproximadamente 2 graus de pitch e 1 grau de roll por quadro, ajustados depois pela telemetria;
- limitar o roll de direção visual separadamente do roll do terreno;
- manter a orientação durante falha de contato curta, retornando suavemente quando o contato expirar.

Critério de saída: as rodas devem se mover em relação à carroceria, enquanto a carroceria reage mais lentamente. Não pode ocorrer inversão rápida de roll como a observada por volta de 18–20 segundos no vídeo.

### Fase 4 — câmera desacoplada da face

A câmera 2 e 3 consumirão somente o estado físico estabilizado, nunca alturas brutas de roda.

Implementar três componentes:

1. mola vertical criticamente amortecida para o alvo da carroceria;
2. pitch de câmera derivado da grade física, com deadzone e limite de aceleração angular;
3. guarda analítica do terreno atrás do carro:

```text
soloAtrásY = centroSuporteY - gradeFiltrada * distânciaAtrás
cameraY <= soloAtrásY - folgaMínima
```

Não haverá novo raycast da câmera no perfil Saturn. A guarda usa o plano de suporte já calculado pelas rodas.

Regras adicionais:

- resetar molas em respawn/teleporte;
- usar parâmetros separados para câmera 2 e câmera 3;
- limitar velocidade e aceleração vertical, não apenas aplicar `lerp`;
- a proteção anticlipping pode levantar imediatamente a câmera, mas o retorno para baixo deve ser amortecido.

Flag proposta: `PHYS_CAMERA_SUPPORT_PLANE_V2`.

Critério de saída: câmera nunca cruza o asfalto na descida e não replica oscilações de uma única roda.

### Fase 5 — otimização e ativação gradual

Executar A/B no emulador após cada flag:

1. contato V2 com solver antigo;
2. contato V2 + solver novo, visual antigo;
3. visual por compressão;
4. câmera pelo plano de suporte;
5. remoção do caminho antigo somente após dois vídeos estáveis.

Se a consulta ainda for cara, avaliar como etapa opcional uma versão 2 do FSMAP com hints de plano/normal quantizados. Essa extensão só será aceita depois de medir o custo real; não deve ser adicionada preventivamente.

## 7. Testes automatizados

### Contato/malha

- rampa única dividida em 20 faces: Y deve permanecer contínuo;
- borda entre dois segmentos com vértices iguais;
- duas faces sobrepostas com alturas diferentes;
- ponto exatamente sobre a diagonal de um quad;
- face degenerada;
- troca de segmento 22→23 e sequência até 42;
- contato persistente de cada roda sem compartilhar hint com outra roda.

### Suspensão

- solo plano;
- degrau real pequeno;
- descida linear contínua;
- contato RR→RL→FR→FL;
- perda de uma roda;
- perda das quatro rodas e queda por gravidade;
- aterrissagem sem overshoot/penetração;
- convergência sem oscilação sustentada.

### Visual/câmera

- pitch e roll não excedem a taxa máxima por quadro;
- roda não ultrapassa o curso visual;
- câmera mantém folga mínima em declive;
- câmera retorna suavemente ao plano;
- respawn não conserva velocidade antiga da mola.

## 8. Telemetria mínima

Reutilizar o HUD/estado de debug existente e evitar strings novas por quadro. Expor, sob flag de diagnóstico:

- máscara das quatro rodas;
- face e segmento de cada contato;
- idade dos contatos;
- compressão das rodas;
- velocidade vertical da carroceria;
- pitch/roll físicos;
- troca de face rejeitada;
- distância estimada câmera–solo;
- consultas, faces examinadas e cache hits do quadro.

Uma captura problemática deve permitir distinguir imediatamente:

- face errada;
- contato expirado;
- mola mal ajustada;
- visual divergente da física;
- câmera divergente do plano físico.

## 9. Gates de validação Saturn

Após cada fase runtime:

1. testes host específicos;
2. build SH2 limpo;
3. `git diff --check`;
4. tamanho de `.text`, `.data` e `.bss`;
5. margem entre o fim de `.bss` e `WorkArea`;
6. margem entre `WorkArea` e `TransList`;
7. tamanho da ISO;
8. boot no Yabause;
9. pista, background, carro, HUD e áudio simultaneamente visíveis;
10. vídeo da reta, entrada do S, descida completa e saída.

Abortar/voltar a flag da fase se:

- pista ou background deixar de renderizar;
- aparecer invalid opcode ou áudio travado;
- o número de consultas ultrapassar duas por quadro;
- houver alocação dinâmica no hot path;
- a margem de Work RAM cair fora do orçamento registrado;
- a descida piorar em relação ao vídeo baseline.

## 10. Ordem recomendada

1. auditor offline dos segmentos 22–42;
2. contato exato com hint separado por roda;
3. testes de continuidade e persistência;
4. solver de mola/gravidade atrás de flag;
5. curso visual das rodas;
6. câmera baseada no plano físico;
7. otimização medida;
8. remoção dos caminhos antigos.

A prioridade é corrigir primeiro **qual face sustenta cada roda**. Somente depois devem ser ajustadas rigidez, amortecimento e câmera. Isso evita usar filtros para mascarar erros geométricos.
