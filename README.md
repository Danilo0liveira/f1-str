# Projeto STR

Este projeto demonstra a implementação de um sistema multitarefa utilizando FreeRTOS no microcontrolador ESP32. O código foca na coleta de estatísticas de uso de CPU, gerenciamento de periféricos via interrupções (ISR) e sincronização de tarefas através de semáforos.

## Descrição do Projeto

O software simula um sistema de monitoramento de sensor onde os dados podem ser manipulados externamente (injeção de dados). Ele utiliza o modelo de processamento distribuído, dividindo tarefas entre os dois núcleos do ESP32 (Core 0 e Core 1).

## Funcionalidades Técnicas

### 1. Estatísticas de Tempo Real

Através da função `print_real_time_stats`, o sistema monitora o tempo de execução de cada tarefa.

* Captura o estado do sistema antes e depois de um período de atraso.
* Calcula a porcentagem de utilização de CPU por tarefa.
* Identifica tarefas criadas ou deletadas dinamicamente.

### 2. Sincronização e Comunicação

O projeto utiliza algumas funções básicas do FreeRTOS para garantir a integridade dos dados:

* **Semáforos Binários:** Utilizados para sinalizar eventos de botões e sincronizar o início da injeção de dados.
* **Semáforos de Contagem:** Utilizados para gerenciar a inicialização sincronizada de múltiplas tarefas de processamento (`spin_tasks`).

### 3. Gerenciamento de Tarefas (Task Mapping)

* **sensor_task (Core 0):** Monitora continuamente o valor da variável de injeção e aciona alertas visuais.
* **injection_task (Core 1):** Aguarda o disparo de uma interrupção de hardware para iniciar a manipulação dos valores do sensor.

## Mapeamento de Hardware (GPIO)

| Pino | Função | Tipo | Descrição |
| --- | --- | --- | --- |
| **GPIO 12** | ALERT_SYSTEM_GOOD | Saída | Indica que o sistema está alimentado e operando. |
| **GPIO 13** | ALERT_CHEAT_DETECTED | Saída | Ativado quando o sensor detecta um valor acima do padrão. |
| **GPIO 14** | ALERT_CHEAT_ON | Saída | Indica que a tarefa de injeção de dados está ativa. |
| **GPIO 25** | START_CHEAT_BUTTON | Entrada | Botão para iniciar a manipulação de dados (Botão acionado). |
| **GPIO 27** | FORCE_FAULT_BUTTON | Entrada | Botão para introduzir jitter (atraso) na tarefa de injeção. |

## Requisitos de Software

* **ESP-IDF:** Framework oficial da Espressif.
* **Configuração do SDK:** É necessário habilitar as seguintes opções no `menuconfig`:
* `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS`
* `CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS`



## Como o Sistema Opera

1. Ao iniciar, o sistema configura os pinos e instala o serviço de interrupção (ISR).
2. A `sensor_task` monitora a variável `injection_value` (Padrão: 100).
3. Ao pressionar o botão no GPIO 25, a interrupção libera o semáforo que ativa a `injection_task`.
4. A `injection_task` passa a sobrescrever o valor do sensor para 120.
5. Se o botão no GPIO 27 for pressionado, um atraso adicional (`jitter`) é somado à tarefa de injeção, simulando uma falha de temporização.

---

## Digrama de blocos

<img width="1408" height="768" alt="image" src="https://github.com/user-attachments/assets/ffd177d1-bc87-451a-8dc3-513a50e96490" />

Link do Vídeo: 

https://youtu.be/knCfdsCXC2M
