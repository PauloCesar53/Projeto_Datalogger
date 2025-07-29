# Projeto_Datalogger
Repositório criado para versionamento da atividade do datalogger 


## Descrição geral do Funcionamento do programa 
Os LEDs RGB mostram a cor de acordo com a função de gravação que está sendo executada, sendo amarelo para esperar, verde quando pronto para receber dados, vermelho quando em gravação de dados, etc  . No Display da BitDogLab é informado informações úteis de gravação de dados, sistema em espera, quantidade de dados gravadas, etc . Um beep sonoro é emitido pelo buzzer indicando o início da gravação de dados, e um beep duplo no final da gravação. O botão B coloca a placa em modo bootssel.  

## Descrição detalhada do Funcionamento do programa  na BitDogLab
**Botão B→** Utilizado para colocar a placa em modo bootssel.

**LEDs RGB→** RGB→Cor de acordo a função de gravação;

**Display→** Utilizado para mostrar informações úteis de leitura, inicialização e gravação.

**Buzzer→** Utilizado para emitir sinais sonoros de gravação;

**i2C→**  Interface utilizada no sensor e display;


## Compilação e Execução

1. Certifique-se de que o SDK do Raspberry Pi Pico está configurado no seu ambiente.
2. Compile o programa utilizando a extensão **Raspberry Pi Pico Project** no VS Code:
   - Abra o projeto no VS Code, na pasta **PROJETO_DATALOGGER** tem os arquivos necessários para importar 
   o projeto com a extensão **Raspberry Pi Pico Project**.
   - Vá até a extensão do **Raspberry pi pico project** e após importar (escolher sdk de sua escolha) os projetos  clique em **Compile Project**.
3. Coloque a placa em modo BOOTSEL e copie o arquivo `Usar_SSD.uf2`  que está na pasta build, para a BitDogLab conectado via USB.

**OBS: Devem importar o projeto para gerar a pasta build, pois a mesma não foi inserida no repositório**

## Colaboradores
- [PauloCesar53 - Paulo César de Jesus Di Lauro ] (https://github.com/PauloCesar53)
