Esta��o de Monitoramento de Cheias com Raspberry Pi Pico W
Projeto desenvolvido para simular um sistema de monitoramento de cheias utilizando a Raspberry Pi Pico W. O sistema exibe os n�veis simulados de chuva e do rio em tempo real, com alertas visuais e sonoros em caso de risco de enchente.

Objetivo:
Desenvolver um sistema embarcado para monitoramento ambiental, focado na preven��o de enchentes, utilizando sensores simulados via joystick. O projeto permite a exibi��o clara dos dados no display OLED, alerta por LED RGB, sons no buzzer e anima��es na matriz de LEDs em caso de emerg�ncia.

Funcionalidades:
Estado Normal

Chuva < 40% e n�vel do rio < 40%
Display OLED mostra os percentuais com ret�ngulos preenchidos proporcionalmente
Nenhum alerta � acionado
LED RGB Verde ligado
Buzzer permanece inativo

Estado de Aten��o

Qualquer valor entre 40% e os limites de alerta (chuva < 80%, rio < 70%)
Display OLED continua exibindo os gr�ficos em tempo real
LED RGB em amarelo
Buzzer com bips suaves e longos

Estado de Alerta

Chuva ? 80% ou n�vel do rio ? 70%
Display alterna entre gr�ficos e mensagens de alerta
LED RGB em vermelho
Buzzer com sirene de alerta (sons altos e r�pidos)
Matriz de LEDs exibe anima��es de exclama��es piscantes nas cores vermelho, verde e azul

Componentes e GPIOs Utilizados

Componente - GPIO - Fun��o
Joystick Anal�gico - ADC0/ADC1 - Simula os valores de chuva (X) e n�vel do rio (Y)
Display OLED SSD1306 - GP14/GP15 - Exibe gr�ficos e alertas
LED RGB (PWM) - GP11?13 - Indica��o de estado (verde, amarelo, vermelho)
Buzzer (PWM) - GP21 - Emiss�o de alertas sonoros
Matriz de LEDs 5x5 (PIO) - GP7 - Anima��es de emerg�ncia com PIO customizado

Multitarefa com FreeRTOS

O sistema utiliza FreeRTOS para garantir execu��o paralela e eficiente:
Cada componente possui uma task dedicada
Comunica��o entre tasks via filas (queues)
Resposta em tempo real �s varia��es dos sensores

T�cnicas Implementadas

PWM para controle de brilho dos LEDs e som do buzzer
Filas do FreeRTOS para troca de dados entre tarefas
Mapeamento anal�gico para porcentagem

C�digo e V�deo
Github: https://github.com/Mateus-MDS/Monitoramento_de_Cheias