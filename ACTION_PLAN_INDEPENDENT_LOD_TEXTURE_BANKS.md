# Plano de ação — bancos de textura independentes por LOD

## Objetivo

Separar as texturas dos três LODs de design em bancos próprios, sem comparar,
substituir ou redimensionar automaticamente uma imagem de um LOD com base em
outro. Isso preserva deliberadamente a qualidade superior de algumas imagens de
`lod_0`.

## Contrato dos arquivos

| Design LOD | Banco no CD | ID no cabeçalho | Slot de runtime | Resolução nominal |
| --- | --- | ---: | ---: | ---: |
| `lod_0` | `TBKLOD0.BIN` | 0 | 3 | 64 |
| `lod_1` | `TBKLOD1.BIN` | 1 | 1 | 64 |
| `lod_2` | `TBKLOD2.BIN` | 2 | 2 | 32 |

O `familyId` continua estável entre os três bancos. Cada banco, porém, incorpora
o TGA originado exclusivamente de seu diretório `ARQ_TGA` correspondente.

## Implementação

1. Remover as exigências de igualdade binária e de UV entre `lod_0` e `lod_1`.
2. Preparar cada textura em `prepared/lod_0`, `prepared/lod_1` ou
   `prepared/lod_2`, preservando suas dimensões e paleta originais. Regras
   explícitas de UV unwrap continuam sendo aplicadas separadamente por LOD.
3. Gerar `TBKLOD0.BIN`, `TBKLOD1.BIN` e `TBKLOD2.BIN`, com manifestos e IDs de
   cabeçalho independentes.
4. Fazer o `build_all_nya_geo_mat.ps1` exigir exatamente esses três bancos.
5. Usar três slots de textura no runtime e selecionar o slot pelo LOD de design
   do segmento, com fallback ordenado entre os outros bancos.
6. Atualizar os testes da política de streaming para as duas fronteiras de LOD.

## Validação

- Verificar sintaxe dos scripts PowerShell alterados.
- Reconstruir o catálogo a partir de `C:\Models\png\sectors\result`.
- Confirmar três bancos, IDs 0/1/2 e uma entrada por família em cada banco.
- Executar testes da política de streaming e o build completo.
- Executar o validador do projeto e registrar qualquer diferença de tamanho da
  ISO como consequência explícita da inclusão do terceiro banco.

