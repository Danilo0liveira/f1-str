# Projeto STR

Este projeto demonstra a utilização do FreeRTOS no ESP32 para gerenciar tarefas concorrentes, manipulação de interrupções (ISR) e sincronização via semáforos. O sistema simula a leitura de um sensor e uma tarefa de "hack" que tenta alterar os valores lidos pelo sistema principal.

---

# Exemplo de Monitoramento de Sensor e Injeção de Dados (FreeRTOS)

Este projeto demonstra a utilização do FreeRTOS no ESP32 para gerenciar tarefas concorrentes, manipulação de interrupções (ISR) e sincronização via semáforos. O sistema simula a leitura de um sensor e uma tarefa de "hack" que tenta alterar os valores lidos pelo sistema principal.

## 🛠 Lógica de Funcionamento

O sistema opera com duas frentes principais:
1.  **Monitoramento Real:** Uma tarefa lê continuamente o valor de uma variável (`injection_value`). Se o valor ultrapassar o padrão, um alerta visual é acionado.
2.  **Intervenção (Hack):** Através de botões físicos, o usuário pode ativar uma tarefa que "sequestra" o valor do sensor, injetando um valor artificial (`INJECTION_HACK_VALUE`) exatamente no momento em que a leitura ocorre.

### Fluxo de Sincronização
Para que a trapaça funcione, a tarefa de injeção precisa saber exatamente quando o sensor vai realizar a leitura. Isso é feito através de um **Semáforo Binário** (`xSemaphoreSensorRead`), que atua como um sinal de sincronismo entre as duas tarefas.

---

## 📝 Descrição das Funções

### 1. `sensor_task`
É a tarefa principal de monitoramento.
* **O que faz:** Exibe o valor atual da injeção no log e verifica se ele excede o limite normal (`100`).
* **Ação:** Se detectar um valor alto, acende o LED `ALERT_CHEAT_DETECTED_LED`.
* **Sincronismo:** Ela libera o semáforo `xSemaphoreSensorRead` para avisar outras tarefas que uma leitura acabou de acontecer e entra em um delay de 30ms.

### 2. `injection_task`
É a tarefa responsável por manipular os dados.
* **O que faz:** Permanece bloqueada até que o botão de "Cheat" seja pressionado.
* **Lógica de "Hack":** Após o primeiro ciclo de ativação, ela tenta sincronizar com a `sensor_task`. Ela espera o sinal do sensor, altera o valor para o nível de "hack" e aguarda um tempo ligeiramente menor que o tempo de amostragem do sensor para garantir que o valor esteja alterado na próxima leitura.

### 3. `isr_callback_start_cheat_pressed_button`
Rotina de Interrupção (ISR) para o botão de início.
* **O que faz:** Libera o semáforo `xStartButtonPressed`, permitindo que a tarefa de injeção saia do estado de bloqueio e comece a rodar.

### 4. `isr_callback_add_jitter_pressed_button`
Rotina de Interrupção para o botão de falha (Force Fault).
* **O que faz:** Introduz um "jitter" (atraso intencional) de 10ms na lógica de tempo da injeção, o que pode causar dessincronização entre o hack e o sensor.

### 5. `app_main`
Ponto de entrada do programa.
* **Configuração:** Inicializa os pinos de saída (LEDs) e entrada (Botões).
* **Interrupções:** Configura as interrupções nos pinos dos botões para a borda de descida (`NEGEDGE`).
* **Criação de Tarefas:** Cria as tarefas `sensor_task` e `injection_task`, fixando-as em núcleos diferentes do ESP32 para otimizar o processamento paralelo.

---

## 📌 Pinagem Utilizada

| Componente | Pino GPIO | Função |
| :--- | :--- | :--- |
| **LED Alerta de Trapaça** | 13 | Aceso quando o valor lido é > 100 |
| **LED Sistema OK** | 12 | Indica que o sistema está ligado |
| **LED Trapaça Ativa** | 14 | Indica que a tarefa de hack está rodando |
| **Botão Iniciar Hack** | 25 | Gatilho para iniciar a `injection_task` |
| **Botão Forçar Jitter** | 27 | Introduz instabilidade no tempo do hack |

---

Deseja que eu explique mais detalhadamente como o semáforo binário impede que a `injection_task` consuma CPU desnecessariamente enquanto o botão não é pressionado?

---

## Digrama de blocos (gerado por IA)

<img width="1408" height="768" alt="image" src="https://github.com/user-attachments/assets/ffd177d1-bc87-451a-8dc3-513a50e96490" />

Link do Vídeo: 

https://youtu.be/knCfdsCXC2M
