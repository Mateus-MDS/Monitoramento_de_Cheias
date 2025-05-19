Estação de Monitoramento de Cheias com Raspberry Pi Pico W
Projeto desenvolvido para simular um sistema de monitoramento de cheias utilizando a Raspberry Pi Pico W. O sistema exibe os níveis simulados de chuva e do rio em tempo real, com alertas visuais e sonoros em caso de risco de enchente.

Objetivo:
Desenvolver um sistema embarcado para monitoramento ambiental, focado na prevenção de enchentes, utilizando sensores simulados via joystick. O projeto permite a exibição clara dos dados no display OLED, alerta por LED RGB, sons no buzzer e animações na matriz de LEDs em caso de emergência.

Funcionalidades:
Estado Normal

Chuva < 40% e nível do rio < 40%
Display OLED mostra os percentuais com retângulos preenchidos proporcionalmente
Nenhum alerta é acionado
LED RGB Verde ligado
Buzzer permanece inativo

Estado de Atenção

Qualquer valor entre 40% e os limites de alerta (chuva < 80%, rio < 70%)
Display OLED continua exibindo os gráficos em tempo real
LED RGB em amarelo
Buzzer com bips suaves e longos

Estado de Alerta

Chuva ? 80% ou nível do rio ? 70%
Display alterna entre gráficos e mensagens de alerta
LED RGB em vermelho
Buzzer com sirene de alerta (sons altos e rápidos)
Matriz de LEDs exibe animações de exclamações piscantes nas cores vermelho, verde e azul

Componentes e GPIOs Utilizados

Componente - GPIO - Função
Joystick Analógico - ADC0/ADC1 - Simula os valores de chuva (X) e nível do rio (Y)
Display OLED SSD1306 - GP14/GP15 - Exibe gráficos e alertas
LED RGB (PWM) - GP11?13 - Indicação de estado (verde, amarelo, vermelho)
Buzzer (PWM) - GP21 - Emissão de alertas sonoros
Matriz de LEDs 5x5 (PIO) - GP7 - Animações de emergência com PIO customizado

Multitarefa com FreeRTOS

O sistema utiliza FreeRTOS para garantir execução paralela e eficiente:
Cada componente possui uma task dedicada
Comunicação entre tasks via filas (queues)
Resposta em tempo real às variações dos sensores

Técnicas Implementadas

PWM para controle de brilho dos LEDs e som do buzzer
Filas do FreeRTOS para troca de dados entre tarefas
Mapeamento analógico para porcentagem

Autor: Mateus Moreira da Silva

Código e Vídeo
Github: https://github.com/Mateus-MDS/Monitoramento_de_Cheias
