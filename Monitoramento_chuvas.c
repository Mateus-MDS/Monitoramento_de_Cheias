/**
 * Sistema de Monitoramento de Chuva e Nível de Água
 * 
 * Hardware:
 * - Raspberry Pi Pico
 * - Display OLED SSD1306 via I2C
 * - Joystick analógico (eixos X e Y)
 * - Matriz de LEDs
 * - Buzzer
 * - LEDs PWM (vermelho e verde)
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "lib/ssd1306.h"
#include "lib/font.h"
#include "hardware/pwm.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <stdio.h>
#include "hardware/pio.h"
#include "math.h"
#include "hardware/clocks.h"
#include "animacoes_led.pio.h" // Animações LEDs PIO
#include "pico/bootrom.h"

// ================= CONFIGURAÇÕES DE HARDWARE =================
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define endereco 0x3C
#define ADC_JOYSTICK_X 26  // Pino ADC para eixo X (volume de chuva)
#define ADC_JOYSTICK_Y 27  // Pino ADC para eixo Y (nível de água)
#define LED_MATRIX_PIN 7   // Pino da matriz de LEDs
#define NUM_PIXELS 25      // Número de LEDs na matriz
#define LED_RED 13         // LED vermelho
#define LED_GREEN 11       // LED verde
#define BUZZER_PIN 21      // Pino do buzzer
#define BUTTON_B 6         // Botão para modo BOOTSEL

// ================= VARIÁVEIS GLOBAIS =================
PIO pio;                   // Controlador PIO para matriz de LEDs
uint sm;                   // State Machine do PIO
int current_pattern;       // Padrão atual da matriz de LEDs

// ================= ESTRUTURAS DE DADOS =================
typedef enum {
    COR_VERMELHO,
    COR_VERDE,
    COR_AZUL,
    COR_AMARELO,
    COR_BRANCO
} CorLED;

typedef struct {
    uint16_t x_volume_chuva;  // Valor do eixo X (volume de chuva)
    uint16_t y_nivel_agua;    // Valor do eixo Y (nível de água)
} Dados_analogicos;

QueueHandle_t xQueueJoystickData;  // Fila para compartilhamento de dados entre tasks

// ================= DEFINIÇÕES DE PADRÕES =================
// Matriz com representações dos padrões para a matriz de LEDs (5x5)
double padroes_led[4][25] = {
    // Padrão 0
    {0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    // Padrão 1
    {0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0},
    // Padrão 2
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    // Todos LEDs apagados
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
};

// ================= FUNÇÕES DE CONTROLE DE LEDS =================

/**
 * Converte valores RGBW em um valor de cor para a matriz de LEDs
 */
uint32_t matrix_rgb(double r, double g, double b, double w) {
    uint8_t red = (uint8_t)(r * 255);
    uint8_t green = (uint8_t)(g * 255);
    uint8_t blue = (uint8_t)(b * 255);
    uint8_t white = (uint8_t)(w * 255);
    return (red << 24) | (green << 16) | (blue << 8) | white;
}

/**
 * Atualiza a matriz de LEDs com o padrão atual e cor especificada
 */
void Desenho_matriz_leds(double r, double g, double b, double w) {
    for (int i = 0; i < NUM_PIXELS; i++) {
        double valor = padroes_led[current_pattern][i];
        uint32_t cor_led = (valor > 0) ? matrix_rgb(r, g, b, w) : matrix_rgb(0, 0, 0, 0);
        pio_sm_put_blocking(pio, sm, cor_led);
    }
}

/**
 * Atualiza a matriz de LEDs com uma cor pré-definida
 */
void Desenho_matriz_leds_cor(CorLED cor) {
    double r = 0, g = 0, b = 0, w = 0;
    switch (cor) {
        case COR_VERMELHO: r = 1.0; break;
        case COR_VERDE: g = 1.0; break;
        case COR_AZUL: b = 1.0; break;
        case COR_AMARELO: r = 1.0; g = 1.0; break;
        case COR_BRANCO: w = 1.0; break;
    }
    Desenho_matriz_leds(r, g, b, w);
}

// ================= FUNÇÕES DE CONFIGURAÇÃO DE HARDWARE =================

/**
 * Configura um pino para saída PWM
 */
void config_PWM(uint led_pin, float clk_div, uint wrap) {
    gpio_set_function(led_pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(led_pin);
    uint channel = pwm_gpio_to_channel(led_pin);

    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, clk_div);
    pwm_config_set_wrap(&config, wrap);

    pwm_init(slice, &config, true);
    pwm_set_chan_level(slice, channel, 0);
}

/**
 * Toca um som no buzzer com frequência e duração especificadas
 */
void buzzer_tocar(uint buzzer_pin, uint freq_hz, uint duracao_ms) {
    float clk_div = 125.0f;
    uint wrap = 125000000 / (clk_div * freq_hz);
    
    config_PWM(buzzer_pin, clk_div, wrap);

    uint slice = pwm_gpio_to_slice_num(buzzer_pin);
    uint channel = pwm_gpio_to_channel(buzzer_pin);

    pwm_set_chan_level(slice, channel, wrap / 2);
    vTaskDelay(pdMS_TO_TICKS(duracao_ms));
    pwm_set_chan_level(slice, channel, 0);
}

/**
 * Toca um som de estado de atenção
 */
void Som_estado_atencao() {
    for (int i = 0; i < 3; i++) {
        buzzer_tocar(BUZZER_PIN, 100, 200);  // tom grave
        buzzer_tocar(BUZZER_PIN, 200, 200);  // tom médio
        buzzer_tocar(BUZZER_PIN, 300, 200);  // tom mais agudo
    }
}

/**
 * Toca um som de estado de alerta
 */
void Som_estado_alerta() {
    for (int i = 0; i < 8; i++) {
        buzzer_tocar(BUZZER_PIN, 2500, 80);  // som rápido e agudo
        vTaskDelay(pdMS_TO_TICKS(50));       // intervalo curto
    }
}

// ================= TASKS DO FreeRTOS =================

/**
 * Task para leitura dos valores do joystick (ADC)
 */
void vJoystickTask(void *params) {
    // Inicialização do ADC
    adc_gpio_init(ADC_JOYSTICK_Y);
    adc_gpio_init(ADC_JOYSTICK_X);
    adc_init();

    Dados_analogicos Dados;

    while (true) {
        // Leitura do eixo Y (nível de água)
        adc_select_input(0); // GPIO 26 = ADC0
        Dados.y_nivel_agua = adc_read();

        // Leitura do eixo X (volume de chuva)
        adc_select_input(1); // GPIO 27 = ADC1
        Dados.x_volume_chuva = adc_read();

        // Envia os dados para a fila
        xQueueSend(xQueueJoystickData, &Dados, 0);
        vTaskDelay(pdMS_TO_TICKS(100)); // 10 Hz de leitura
    }
}

/**
 * Exibe um alerta no display OLED
 */
void ExibirAlerta(ssd1306_t *ssd, const char *titulo, const char *linha1, const char *linha2) {
    bool cor = true; // true = branco, false = preto
    
    ssd1306_fill(ssd, !cor);
    ssd1306_draw_string(ssd, titulo, 40, linha2 ? 10 : 20);
    ssd1306_draw_string(ssd, linha1, 15, linha2 ? 20 : 30);
    
    if (linha2 != NULL) {
        ssd1306_draw_string(ssd, linha2, 15, 50);
    }
    
    ssd1306_send_data(ssd);
    sleep_ms(1000);
}

/**
 * Tarefa para exibição no display OLED
 */
void vDisplayTask(void *params)
{
    // Inicialização do hardware I2C e display
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Configuração inicial do display OLED
    ssd1306_t ssd;
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT);
    ssd1306_config(&ssd);
    ssd1306_send_data(&ssd);

    Dados_analogicos Dados; // Dados recebidos do joystick
    bool cor = true; // true = branco (desenhar), false = preto (limpar)
    
    while (true)
    {
        // Aguarda novos dados da fila (bloqueante)
        if (xQueueReceive(xQueueJoystickData, &Dados, portMAX_DELAY) == pdTRUE)
        {
            /* ========== CONFIGURAÇÃO DAS BARRAS DE PROGRESSO ========== */
            // Inicializa todos os segmentos como não preenchidos (true)
            bool cor_ch_100 = true; bool cor_ch_90 = true; bool cor_ch_80 = true; 
            bool cor_ch_70 = true; bool cor_ch_60 = true; bool cor_ch_50 = true; 
            bool cor_ch_40 = true; bool cor_ch_30 = true; bool cor_ch_20 = true; 
            bool cor_ch_10 = true;

            bool cor_ni_100 = true; bool cor_ni_90 = true; bool cor_ni_80 = true; 
            bool cor_ni_70 = true; bool cor_ni_60 = true; bool cor_ni_50 = true; 
            bool cor_ni_40 = true; bool cor_ni_30 = true; bool cor_ni_20 = true; 
            bool cor_ni_10 = true;

            // Valores para aproximar a porcentagem
            const float Percentual[] = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};

            // Converte valores ADC para porcentagem (0-100)
            float x = (Dados.x_volume_chuva * 100) / 4088; // Chuva (eixo X)
            float y = (Dados.y_nivel_agua * 100) / 4088;   // Nível água (eixo Y)

            /* ========== CÁLCULO DO PERCENTUAL MAIS PRÓXIMO ========== */
            float valor_real_chuva = x;
            float valor_real_nivel = y;
            float erro_novo_chuva = 100000;
            float erro_novo_nivel = 100000;
            uint16_t Percentual_chuva, Percentual_nivel;
   
            // Encontra valor mais próximo da porcentagem para chuva
            for (int k = 0; k < 11; k++) {
                float erro_chuva = fabs(valor_real_chuva - Percentual[k]);
                if (erro_chuva < erro_novo_chuva) {
                    erro_novo_chuva = erro_chuva;
                    Percentual_chuva = Percentual[k];
                }
            }

            // Encontra valor mais próximo de porcentagem para nível
            for (int m = 0; m < 11; m++) {
                float erro_nivel = fabs(valor_real_nivel - Percentual[m]);
                if (erro_nivel < erro_novo_nivel) {
                    erro_novo_nivel = erro_nivel;
                    Percentual_nivel = Percentual[m];
                }
            }

            /* ========== PREPARAÇÃO DOS VALORES PARA EXIBIÇÃO ========== */
            char str_chuva[10], str_nivel[10];
            sprintf(str_chuva, "%1.0f", x); // Formata valor da chuva sem decimais
            sprintf(str_nivel, "%1.0f", y); // Formata valor do nível sem decimais

            /* ========== ATUALIZA ESTADO DOS SEGMENTOS DA CHUVA ========== */
            switch (Percentual_chuva) {
                case 10: cor_ch_10 = false; break;
                case 20: cor_ch_10 = false; cor_ch_20 = false; break;
                case 30: cor_ch_10 = false; cor_ch_20 = false; cor_ch_30 = false; break;
                case 40: cor_ch_10 = false; cor_ch_20 = false; cor_ch_30 = false; cor_ch_40 = false; break;
                case 50: cor_ch_10 = false; cor_ch_20 = false; cor_ch_30 = false; cor_ch_40 = false; cor_ch_50 = false; break;
                case 60: cor_ch_10 = false; cor_ch_20 = false; cor_ch_30 = false; cor_ch_40 = false; cor_ch_50 = false; cor_ch_60 = false; break;
                case 70: cor_ch_10 = false; cor_ch_20 = false; cor_ch_30 = false; cor_ch_40 = false; cor_ch_50 = false; cor_ch_60 = false; cor_ch_70 = false; break;
                case 80: cor_ch_10 = false; cor_ch_20 = false; cor_ch_30 = false; cor_ch_40 = false; cor_ch_50 = false; cor_ch_60 = false; cor_ch_70 = false; cor_ch_80 = false; break;
                case 90: cor_ch_10 = false; cor_ch_20 = false; cor_ch_30 = false; cor_ch_40 = false; cor_ch_50 = false; cor_ch_60 = false; cor_ch_70 = false; cor_ch_80 = false; cor_ch_90 = false; break;
                case 100: cor_ch_10 = false; cor_ch_20 = false; cor_ch_30 = false; cor_ch_40 = false; cor_ch_50 = false; cor_ch_60 = false; cor_ch_70 = false; cor_ch_80 = false; cor_ch_90 = false; cor_ch_100 = false; break;
                default: break;
            }

            /* ========== ATUALIZA ESTADO DOS SEGMENTOS DO NÍVEL ========== */
            switch (Percentual_nivel) {
                case 10: cor_ni_10 = false; break;
                case 20: cor_ni_10 = false; cor_ni_20 = false; break;
                case 30: cor_ni_10 = false; cor_ni_20 = false; cor_ni_30 = false; break;
                case 40: cor_ni_10 = false; cor_ni_20 = false; cor_ni_30 = false; cor_ni_40 = false; break;
                case 50: cor_ni_10 = false; cor_ni_20 = false; cor_ni_30 = false; cor_ni_40 = false; cor_ni_50 = false; break;
                case 60: cor_ni_10 = false; cor_ni_20 = false; cor_ni_30 = false; cor_ni_40 = false; cor_ni_50 = false; cor_ni_60 = false; break;
                case 70: cor_ni_10 = false; cor_ni_20 = false; cor_ni_30 = false; cor_ni_40 = false; cor_ni_50 = false; cor_ni_60 = false; cor_ni_70 = false; break;
                case 80: cor_ni_10 = false; cor_ni_20 = false; cor_ni_30 = false; cor_ni_40 = false; cor_ni_50 = false; cor_ni_60 = false; cor_ni_70 = false; cor_ni_80 = false; break;
                case 90: cor_ni_10 = false; cor_ni_20 = false; cor_ni_30 = false; cor_ni_40 = false; cor_ni_50 = false; cor_ni_60 = false; cor_ni_70 = false; cor_ni_80 = false; cor_ni_90 = false; break;
                case 100: cor_ni_10 = false; cor_ni_20 = false; cor_ni_30 = false; cor_ni_40 = false; cor_ni_50 = false; cor_ni_60 = false; cor_ni_70 = false; cor_ni_80 = false; cor_ni_90 = false; cor_ni_100 = false; break;
                default: break;
            }

            /* ========== ATUALIZAÇÃO DO DISPLAY ========== */
            ssd1306_fill(&ssd, !cor); // Limpa a tela

            // Exibe título e valor da chuva
            ssd1306_draw_string(&ssd, "CHUVA", 0, 3);
            ssd1306_draw_string(&ssd, str_chuva, 40, 3);

            // Desenha todos os segmentos da barra de chuva (10 segmentos)
            ssd1306_rect(&ssd, 14, 10, 30, 5, cor, !cor_ch_100);
            ssd1306_rect(&ssd, 19, 10, 30, 5, cor, !cor_ch_90);
            ssd1306_rect(&ssd, 24, 10, 30, 5, cor, !cor_ch_80);
            ssd1306_rect(&ssd, 29, 10, 30, 5, cor, !cor_ch_70);
            ssd1306_rect(&ssd, 34, 10, 30, 5, cor, !cor_ch_60);
            ssd1306_rect(&ssd, 39, 10, 30, 5, cor, !cor_ch_50);
            ssd1306_rect(&ssd, 44, 10, 30, 5, cor, !cor_ch_40);
            ssd1306_rect(&ssd, 49, 10, 30, 5, cor, !cor_ch_30);
            ssd1306_rect(&ssd, 54, 10, 30, 5, cor, !cor_ch_20);
            ssd1306_rect(&ssd, 59, 10, 30, 5, cor, !cor_ch_10);
            
            // Exibe título e valor do nível
            ssd1306_draw_string(&ssd, "NIVEL", 64, 3);
            ssd1306_draw_string(&ssd, str_nivel, 102, 3);

            // Desenha todos os segmentos da barra de nível (10 segmentos)
            ssd1306_rect(&ssd, 14, 75, 30, 5, cor, !cor_ni_100);
            ssd1306_rect(&ssd, 19, 75, 30, 5, cor, !cor_ni_90);
            ssd1306_rect(&ssd, 24, 75, 30, 5, cor, !cor_ni_80);
            ssd1306_rect(&ssd, 29, 75, 30, 5, cor, !cor_ni_70);
            ssd1306_rect(&ssd, 34, 75, 30, 5, cor, !cor_ni_60);
            ssd1306_rect(&ssd, 39, 75, 30, 5, cor, !cor_ni_50);
            ssd1306_rect(&ssd, 44, 75, 30, 5, cor, !cor_ni_40);
            ssd1306_rect(&ssd, 49, 75, 30, 5, cor, !cor_ni_30);
            ssd1306_rect(&ssd, 54, 75, 30, 5, cor, !cor_ni_20);
            ssd1306_rect(&ssd, 59, 75, 30, 5, cor, !cor_ni_10);
            
            ssd1306_send_data(&ssd); // Envia buffer para o display

            /* ========== TRATAMENTO DE ALERTAS ========== */
            // Alerta para chuva intensa E nível elevado
            if ((y >= 70) && (x >= 80)) {
                sleep_ms(1000);
                ssd1306_fill(&ssd, !cor);
                
                // Mensagens de alerta duplo
                ssd1306_draw_string(&ssd, "ALERTA", 40, 10);
                ssd1306_draw_string(&ssd, "CHUVA INTENSA", 15, 20);
                ssd1306_draw_string(&ssd, "ALERTA", 40, 40);
                ssd1306_draw_string(&ssd, "NIVEL ELEVADO", 15, 50);
                
                ssd1306_send_data(&ssd);
                sleep_ms(1000);
            } 
            // Alerta apenas para nível elevado
            else if ((y >= 70) && (x < 80)) {
                sleep_ms(1000);
                ssd1306_fill(&ssd, !cor);
                
                // Mensagem de alerta único
                ssd1306_draw_string(&ssd, "ALERTA", 40, 20);
                ssd1306_draw_string(&ssd, "NIVEL ELEVADO", 15, 30);
                
                ssd1306_send_data(&ssd);
                sleep_ms(1000);
            } 
            // Alerta apenas para chuva intensa
            else if ((y < 70) && (x >= 80)) {
                sleep_ms(1000);
                ssd1306_fill(&ssd, !cor);
                
                // Mensagem de alerta único
                ssd1306_draw_string(&ssd, "ALERTA", 40, 20);
                ssd1306_draw_string(&ssd, "CHUVA INTENSA", 15, 30);
                
                ssd1306_send_data(&ssd);
                sleep_ms(1000);
            }
        }
    }
}

/**
 * Task para controle dos LEDs PWM
 */
void vControle_leds(void *params) {
    Dados_analogicos Dados;
    
    while (true) {
        if (xQueueReceive(xQueueJoystickData, &Dados, portMAX_DELAY) == pdTRUE) {
            uint slice_red = pwm_gpio_to_slice_num(LED_RED);
            uint channel_red = pwm_gpio_to_channel(LED_RED);
            uint slice_green = pwm_gpio_to_slice_num(LED_GREEN);
            uint channel_green = pwm_gpio_to_channel(LED_GREEN);

            // Controle dos LEDs baseado nos valores lidos
            if (Dados.x_volume_chuva > 3271 || Dados.y_nivel_agua > 2862) {
                // Estado de alerta - LED vermelho aceso
                pwm_set_chan_level(slice_green, channel_green, 0);
                pwm_set_chan_level(slice_red, channel_red, 100);
            } else if ((Dados.x_volume_chuva > 1635 && Dados.x_volume_chuva < 3271) || 
                      (Dados.y_nivel_agua > 1635 && Dados.y_nivel_agua < 2862)) {
                // Estado de atenção - ambos LEDs acesos
                pwm_set_chan_level(slice_green, channel_green, 100);
                pwm_set_chan_level(slice_red, channel_red, 100);
            } else {
                // Estado normal - LED verde aceso
                pwm_set_chan_level(slice_green, channel_green, 100);
                pwm_set_chan_level(slice_red, channel_red, 0);
            }

            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

/**
 * Task para controle da matriz de LEDs
 */
void vControle_matriz_leds(void *params) {
    // Inicialização PIO para matriz de LEDs
    pio = pio0;
    uint offset = pio_add_program(pio, &animacoes_led_program);
    sm = pio_claim_unused_sm(pio, true);
    animacoes_led_program_init(pio, sm, offset, LED_MATRIX_PIN);

    Dados_analogicos Dados;

    while (true) {
        if (xQueueReceive(xQueueJoystickData, &Dados, portMAX_DELAY) == pdTRUE) {
            if (Dados.x_volume_chuva > 3271 || Dados.y_nivel_agua > 2862) {
                // Animação de alerta
                current_pattern = 0;
                Desenho_matriz_leds_cor(COR_VERMELHO);
                sleep_ms(500);

                current_pattern = 1;
                Desenho_matriz_leds_cor(COR_VERDE);
                sleep_ms(500);

                current_pattern = 2;
                Desenho_matriz_leds_cor(COR_AZUL);
                sleep_ms(500);
            } else {
                // Estado normal - LEDs apagados
                current_pattern = 3;
                Desenho_matriz_leds(0.0, 0.0, 0.0, 0.0);
                sleep_ms(1000);
            }

            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

/**
 * Task para controle do buzzer
 */
void vControle_buzzer(void *params) {
    Dados_analogicos Dados;

    while (true) {
        if (xQueueReceive(xQueueJoystickData, &Dados, portMAX_DELAY) == pdTRUE) {
            if (Dados.x_volume_chuva > 3271 || Dados.y_nivel_agua > 2862) {
                Som_estado_alerta();
            } else if ((Dados.x_volume_chuva > 1635 && Dados.x_volume_chuva < 3271) || 
                      (Dados.y_nivel_agua > 1635 && Dados.y_nivel_agua < 2862)) {
                Som_estado_atencao();
            } else {
                gpio_put(BUZZER_PIN, 0);
                sleep_ms(100);
            }

            sleep_ms(50);
        }
    }
}

/**
 * Handler para interrupção do botão BOOTSEL
 */
void gpio_irq_handler(uint gpio, uint32_t events) {
    reset_usb_boot(0, 0);
}

// ================= FUNÇÃO PRINCIPAL =================
int main() {
    // Configuração do botão BOOTSEL
    gpio_init(BUTTON_B);
    gpio_set_dir(BUTTON_B, GPIO_IN);
    gpio_pull_up(BUTTON_B);
    gpio_set_irq_enabled_with_callback(BUTTON_B, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    // Configuração dos LEDs
    gpio_init(LED_RED);
    gpio_init(LED_GREEN);
    config_PWM(LED_GREEN, 4.0f, 100);
    config_PWM(LED_RED, 4.0f, 100);

    // Configuração do buzzer
    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);

    // Inicialização da comunicação serial
    stdio_init_all();
    sleep_ms(2000);

    // Criação da fila para compartilhamento de dados
    xQueueJoystickData = xQueueCreate(5, sizeof(Dados_analogicos));

    // Criação das tasks do FreeRTOS
    xTaskCreate(vJoystickTask, "Joystick Task", 256, NULL, 1, NULL);
    xTaskCreate(vDisplayTask, "Display Task", 512, NULL, 1, NULL);
    xTaskCreate(vControle_leds, "LED red Task", 256, NULL, 1, NULL);
    xTaskCreate(vControle_matriz_leds, "Matriz_leds Task", 256, NULL, 1, NULL);
    xTaskCreate(vControle_buzzer, "Buzzer Task", 256, NULL, 1, NULL);

    // Inicia o agendador do FreeRTOS
    vTaskStartScheduler();
    panic_unsupported();
}